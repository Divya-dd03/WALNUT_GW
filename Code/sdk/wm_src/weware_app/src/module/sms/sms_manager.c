/**
 * @file sms_manager.c
 * @brief SMS manager implementation (no goto, production ready)
 *
 * Walnut port of the reference firmware's module/sms/sms_manager.c. The
 * business logic - task loop, URC -> read -> validate -> forward-to-CMD ->
 * delete pipeline, +CMGR field parser, SIM-gated modem configuration,
 * statistics, init/deinit ordering - is replicated line for line. Only the
 * SDK-facing calls differ, and every deviation is commented inline.
 *
 * ===========================================================================
 * SDK API mapping (CG "sdk_functionality_sms.h"  ->  walnut "sdk_sms.h")
 * ===========================================================================
 * PRESENT WITH THE SAME SIGNATURE (used verbatim):
 *   sdk_sms_read(UINT8 storage, UINT32 index, void *msgq)
 *   sdk_sms_delete(UINT32 index, void *msgq)
 *   sdk_sms_delete_all(void *msgq)
 *   sdk_sms_set_format(UINT8 format)
 *   sdk_sms_set_charset(UINT8 charset)
 *   sdk_sms_set_new_msg_ind(UINT8 mode, mt, bm, ds, bfr)
 *   sdk_sms_msgq_poll(void *msgq, UINT32 *msg_count)
 *
 * PRESENT BUT DIFFERENT (adapted, see sms_send_internal):
 *   CG:     sdk_sms_send(format_mode, message, message_len, recipient, msgq)
 *           -> asynchronous, result posted to msgq.
 *   WALNUT: sdk_sms_send(number, text)
 *           -> BLOCKING until the network accepts/rejects; text mode only,
 *              NUL-terminated body, no msgq and no format/length arguments.
 *
 * MISSING IN THE WALNUT SDK (worked around here):
 *   sdk_platform_register_sms_ops() / the whole SdkSmsFunctionalityOps
 *           dispatcher - walnut has no functionality layer for SMS; app code
 *           calls the kernel sdk_sms_* symbols directly.
 *   SDK_SMS_MAX_ADDRESS_LENGTH - defined in sms_manager.h instead.
 *   sdk_msg_t / SDK_MSG_URC / SDK_URC_SMS_MASK / SDK_URC_NEW_MSG_IND - walnut
 *           has no generic modem-message type and urc_processor never sees an
 *           SMS URC. The queue element is SdkSmsMessage and the "new message"
 *           discriminator is SdkSmsMessage.type == SDK_SMS_EVT_INCOMING.
 *   sdk_memory_free(msg.arg3) for received text -> sdk_sms_msg_free(&msg).
 *
 * EXTRA WALNUT APIs USED (no CG counterpart):
 *   sdk_sms_init()      - brings up the vendor SMS task/queue and registers the
 *                         incoming-SMS hook. Must be called once before any
 *                         other SMS call; invoked from sms_configure().
 *   sdk_sms_msg_free()  - releases the heap-owned 'text' of a received
 *                         SdkSmsMessage (replaces the CG sdk_memory_free).
 *
 * EXTRA WALNUT APIs AVAILABLE BUT NOT USED:
 *   sdk_sms_get_storage_status(name, name_size, used, total) - AT+CPMS?
 *                         occupancy query. The reference has no equivalent, so
 *                         it is deliberately left out of the port.
 *
 * ===========================================================================
 * Inbound path difference (the one structural adaptation)
 * ===========================================================================
 * Reference: urc_processor pushed the raw "+CMTI: \"SM\",12" line into the SMS
 * module urc_q; the SMS task popped it, parsed the index, then called
 * sdk_sms_read() to fetch the body.
 *
 * Walnut: the kernel reads the message itself and the SDK posts an
 * SdkSmsMessage{type=SDK_SMS_EVT_INCOMING, index, text=<raw +CMGR response>}
 * to whichever queue was last attached via sdk_sms_msgq_poll() - i.e. our
 * g_temp_msg_queue, shared with the read/delete results. sms_flush_temp_queue()
 * therefore parks incoming events into the module urc_q (instead of dropping
 * them like the reference could) and the task drains that urc_q into
 * sms_process_urc() exactly as before. sms_read_message() is retained and is
 * still used whenever an incoming event carries no usable +CMGR text.
 *
 * Walnut logging: LOG_ERRC(ERR_*, ...) has no walnut counterpart (no
 * common/error_codes.h, no LOG_ERRC in module/log/log.h); those calls become
 * sdk_log_error with the reference error-code name kept in the message text. The
 * trailing ERRC markers are preserved so the sites stay greppable.
 */

/*===============================================================
 * Includes
 *==============================================================*/
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

/* SDK Platform Abstraction Layer */
#include "wm_global.h"          /* walnut: TP_TIMED_ACTIVITY task priority */
#include "sdk_platform.h"
#include "sdk_os.h"             /* reference: functionality/sdk_functionality_os.h */
#include "sdk_sms.h"            /* reference: functionality/sdk_functionality_sms.h */
#include "sdk_log.h"

#include "module/sms/sms_manager.h"
#include "module/urc/urc_sms_queue_types.h"
#include "module/sms/sms_config.h"
#include "module/sim/sim.h"     /* reference: system/sim/sim_manager.h */
#include "module/command/command_manager.h"
#include "common/event_manager.h"
#include "module/module_manager.h"
#include "common/queue_manager.h"
#include "common/task_stats.h"
#include "common/utils.h"
/* reference: #include "common/error_codes.h" - MISSING IN WALNUT (see header note) */

/*---------------------------------------------------------------
 * Log Configuration
 *--------------------------------------------------------------*/
#define LOG_TAG "SMS"
#define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

/*===============================================================
 * External
 *==============================================================*/
extern Module *g_modules[];

/*===============================================================
 * Globals (DO NOT MAKE STATIC)
 * Runtime State (MUST BE GLOBAL – ABI DEPENDENCY)
 * @note sms_manager_runtime_t is defined in sms_manager.h
 *==============================================================*/
SmsConfig g_sms_config = {0};  /* SMS module configuration storage */
sms_manager_runtime_t g_sms =
{
    .task_ref = NULL,
    /* reference: .task_stack = {0} - walnut lets the kernel allocate the stack */
    .task_stats = {0},
    .task_priority = TP_TIMED_ACTIVITY,   /* reference: 5 (SIMCOM priority scale) */
    .task_interval_ms = 100,
    .module = NULL,
    .config = &g_sms_config,
    .stats = {0}
};

/* Static buffer for SMS send requests (avoid stack overflow in URC handler) */
static ModuleMessage g_sms_send_buffer;

/* Global temporary message queue for SDK API calls (SDK-agnostic).
 * Walnut: this queue is ALSO the route the SDK uses for asynchronous incoming
 * messages - sdk_sms_msgq_poll() attaches it (see sms_flush_temp_queue). */
static void* g_temp_msg_queue = NULL;

/* TRUE after EVENT_SIM_AVAILABLE until EVENT_SIM_UNAVAILABLE (SIM insert/remove). */
static volatile BOOL g_sms_sim_inserted = FALSE;
/* Latched in sms_configure() after a successful modem setup for current insert (SMS task only). */
static BOOL s_sms_modem_config_applied = FALSE;
/* Walnut addition: attempts spent on the current insert. The reference latched
 * unconditionally, which on walnut would leave a half-configured modem (the
 * storage-dependent AT commands answer ERROR while the SIM's SMS store is
 * still loading) with no retry. Bounded so a permanently failing modem cannot
 * turn the 100 ms task cycle into an AT-command flood. */
static UINT8 s_sms_config_attempts = 0;
#define SMS_CONFIG_MAX_ATTEMPTS 5U

/*===============================================================
 * Forward Declarations
 *==============================================================*/
static void  sms_send_task(void *arg);
static void  sms_on_sim_status(const EventData *event, void *user_data);

static BOOL  sms_read_message(INT32 index, sms_message_t *out);
static void  sms_process_urc(const SdkSmsMessage *msg);
static void  sms_extract_fields(const char *raw, char *sender, char *content);
static void  sms_configure(void);
static Result sms_send_internal(const char *recipient, const char *message, UINT32 message_len);
static void  sms_flush_temp_queue(void);
static void  sms_park_incoming(const SdkSmsMessage *msg);
static BOOL  sms_purge_store(void);

/*===============================================================
 * Utility Helpers
 *==============================================================*/

/**
 * @brief Park an asynchronous incoming SMS event in the module urc_q
 * @note WALNUT ADDITION. The reference got inbound SMS from urc_processor via
 *       the module urc_q; on walnut the SDK delivers it on the same queue as
 *       the read/delete results, so it has to be moved into urc_q before the
 *       queue is flushed. The heap-owned 'text' is copied inline (the ring
 *       buffer must stay heap-free, as in the reference) and the caller frees
 *       the original with sdk_sms_msg_free().
 */
static void sms_park_incoming(const SdkSmsMessage *msg)
{
    if (!msg || msg->type != SDK_SMS_EVT_INCOMING) {
        return;
    }

    if (!g_sms.module || g_sms.module->config.urc_q == NULL) {
        sdk_log_error("ERR_SMS_MISSED: inbound SMS dropped, urc_q unavailable"); /* ERRC */
        return;
    }

    sms_urc_queued_t urc_el;
    memset(&urc_el, 0, sizeof(urc_el));
    urc_el.hdr = *msg;
    urc_el.hdr.text = NULL;   /* heap pointer must not escape into the ring buffer */
    if (msg->text) {
        utils_strncpy_safe(urc_el.arg3_inline, msg->text, sizeof(urc_el.arg3_inline));
    }

    if (queue_push(g_sms.module->config.urc_q,
                   &g_sms.module->config.urc_q_config,
                   &urc_el) != RESULT_SUCCESS) {
        sdk_log_error("ERR_SMS_MISSED: inbound SMS dropped, urc_q push failed"); /* ERRC */
    }
}

/**
 * @brief Flush any pending messages from the temporary message queue
 * @note Properly frees any allocated memory in messages to prevent leaks
 * @note Walnut: incoming messages share this queue, so they are parked in the
 *       module urc_q first instead of being discarded (reference dropped
 *       everything - its queue only ever held operation results).
 *       sdk_sms_msgq_poll() doubles as the "attach this queue as the
 *       incoming-SMS route" call, so it must run before sdk_sms_init().
 */
static void sms_flush_temp_queue(void)
{
    if (g_temp_msg_queue == NULL) {
        return;
    }

    UINT32 msgcount = 0;
    if (sdk_sms_msgq_poll(g_temp_msg_queue, &msgcount) != SDK_RESULT_SUCCESS || msgcount == 0) {
        return;
    }

    while (msgcount > 0) {
        SdkSmsMessage msg = {0};
        if (sdk_msgq_recv(g_temp_msg_queue, &msg, 0) == SDK_RESULT_SUCCESS) {
            sms_park_incoming(&msg);      /* walnut addition: keep inbound SMS */
            /* reference: if (msg.arg3) sdk_memory_free((void*)msg.arg3); */
            sdk_sms_msg_free(&msg);
            msgcount--;
        } else {
            break;
        }
    }
}

/** Upper bound on the per-index delete sweep (SIM SMS stores hold 20-50). */
#define SMS_PURGE_MAX_INDEX 60U

/**
 * @brief Empty the preferred SMS store one index at a time
 * @note WALNUT ADDITION - stands in for the reference's single
 *       sdk_sms_delete_all() call. The prebuilt walnut sdk_sms_delete_all()
 *       issues "AT+CMGD=0,4"; index 0 is out of range for the SIM store
 *       (indices are 1-based), so the modem answers ERROR and the call always
 *       fails on this kernel. This sweep asks AT+CPMS? how many slots are used
 *       (sdk_sms_get_storage_status - a walnut-extra API with no CG
 *       counterpart) and deletes in-range indices until that many are gone.
 * @return TRUE when the store is - or already was - empty; FALSE if it could
 *         not be queried or could not be fully emptied.
 */
static BOOL sms_purge_store(void)
{
    char   store[12] = {0};
    UINT32 used = 0;
    UINT32 total = 0;

    SdkResult ret = sdk_sms_get_storage_status(store, sizeof(store), &used, &total);
    if (ret != SDK_RESULT_SUCCESS) {
        sdk_log_warning("SMS purge: storage query failed: %d", (int)ret);
        return FALSE;
    }

    sdk_debug_print("SMS purge: store %s %u/%u used\r\n",
                    store, (unsigned)used, (unsigned)total);

    if (used == 0U) {
        return TRUE;
    }

    if (total == 0U || total > SMS_PURGE_MAX_INDEX) {
        total = SMS_PURGE_MAX_INDEX;
    }

    UINT32 deleted = 0;
    for (UINT32 index = 1U; index <= total && deleted < used; index++) {
        /* Each delete posts a result to the shared queue; drain as we go so it
         * cannot fill (and so any inbound SMS is parked, not lost). */
        sms_flush_temp_queue();
        if (sdk_sms_delete(index, g_temp_msg_queue) == SDK_RESULT_SUCCESS) {
            deleted++;
            g_sms.stats.total_deleted++;
        }
    }

    sdk_debug_print("SMS purge: deleted %u of %u\r\n",
                    (unsigned)deleted, (unsigned)used);
    return (deleted >= used) ? TRUE : FALSE;
}

static void sms_forward_inbound(const sms_message_t *sms)
{
    ModuleMessage cmd_request = {0};
    cmd_request.source_module = MODULE_ID_SMS;
    cmd_request.destination_module = MODULE_ID_CMD;
    utils_strncpy_safe(cmd_request.address, sms->sender_number, sizeof(cmd_request.address));
    {
        int n = utils_strncpy_safe(cmd_request.message, sms->message_content, sizeof(cmd_request.message));
        cmd_request.data_len = (n >= 0) ? (UINT32)n : 0U;
    }

    if (command_manager_accept_request(&cmd_request) != RESULT_SUCCESS)
        sdk_log_error("ERR_SMS_MISSED: Failed to queue inbound SMS command"); /* ERRC */
}

static BOOL sms_is_valid_sender(const char *num)
{
    if (!num || !num[0] || strcmp(num, "UNKNOWN") == 0)
    {
        sdk_log_error("ERR_SMS_UNKNOWN_NUMBER"); /* ERRC */
        return FALSE;
    }

    for (const char *p = num; *p; ++p)
    {
        char c = *p;
        if (!(isdigit((unsigned char)c) || strchr("+- ()", c)))
        {
            sdk_log_error("ERR_SMS_UNKNOWN_NUMBER"); /* ERRC */
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL sms_is_valid_content(const char *msg)
{
    size_t len = msg ? strlen(msg) : 0;
    return (len > 0 && len <= SMS_MANAGER_MAX_MESSAGE_LENGTH)
               ? TRUE
               : (sdk_log_error("ERR_SMS_INVALID_CONTENT") /* ERRC */, FALSE);
}

/*===============================================================
 * Public API
 *==============================================================*/
Result sms_manager_send(const char *recipient, const char *message)
{
    if (!g_sms.module || !g_sms.module->status.initialized ||
        !recipient || !message || !g_sms.module->config.msg_q)
    {
        sdk_log_error("ERR_SMS_SEND_FAILED"); /* ERRC */
        return RESULT_ERROR;
    }

    size_t recipient_len = strlen(recipient);
    size_t message_len = strlen(message);

    if (recipient_len >= sizeof(g_sms_send_buffer.address) ||
        message_len >= sizeof(g_sms_send_buffer.message))
    {
        sdk_log_error("ERR_SMS_SEND_FAILED"); /* ERRC */
        return RESULT_ERROR;
    }

    memset(&g_sms_send_buffer, 0, sizeof(g_sms_send_buffer));
    g_sms_send_buffer.source_module = MODULE_ID_CMD;
    g_sms_send_buffer.destination_module = MODULE_ID_SMS;
    if (utils_strncpy_safe(g_sms_send_buffer.address, recipient, sizeof(g_sms_send_buffer.address)) < 0) {
        sdk_log_error("ERR_SMS_SEND_FAILED"); /* ERRC */
        return RESULT_ERROR;
    }
    {
        int n = utils_strncpy_safe(g_sms_send_buffer.message, message, sizeof(g_sms_send_buffer.message));
        if (n < 0) {
            sdk_log_error("ERR_SMS_SEND_FAILED"); /* ERRC */
            return RESULT_ERROR;
        }
        g_sms_send_buffer.data_len = (UINT32)n;
    }

    return (queue_push(g_sms.module->config.msg_q,
                       &g_sms.module->config.msg_q_config,
                       &g_sms_send_buffer) == RESULT_SUCCESS) ?
                       RESULT_SUCCESS : ( sdk_log_error("ERR_SMS_SEND_FAILED") /* ERRC */ , RESULT_ERROR);
}

Result sms_manager_delete(int index)
{
    if (!g_sms.module || !g_sms.module->status.initialized || index <= 0)
        return RESULT_ERROR;

    if (g_temp_msg_queue == NULL) {
        sdk_log_error("Temporary message queue not initialized");
        return RESULT_ERROR;
    }

    sms_flush_temp_queue();

    SdkResult result = sdk_sms_delete((UINT32)index, g_temp_msg_queue);
    if (result == SDK_RESULT_SUCCESS) {
        g_sms.stats.total_deleted++;
        return RESULT_SUCCESS;
    }
    g_sms.stats.delete_errors++;
    sdk_log_error("ERR_SMS_DELETE_FAILED: index %d, result %d", index, (int)result); /* ERRC */
    return RESULT_ERROR;
}

Result sms_manager_set_format_mode(int mode)
{
    if (!g_sms.config) {
        return RESULT_ERROR;
    }
    g_sms.config->format_mode = mode;
    SdkResult result = sdk_sms_set_format((UINT8)mode);
    return (result == SDK_RESULT_SUCCESS) ?
                RESULT_SUCCESS : (sdk_log_error("ERR_SMS_CONFIG_FAILED") /* ERRC */ , RESULT_ERROR);
}

/**
 * @note Declared by the reference header but never defined there; provided
 *       here so the declaration is satisfiable.
 */
const sms_manager_stats_t *sms_manager_get_stats(void)
{
    return &g_sms.stats;
}

/*===============================================================
 * SMS Send Task
 *==============================================================*/
static void sms_send_task(void *arg)
{
    (void)arg;

    while (1)
    {
        module_manager_update_uptime(MODULE_ID_SMS);
        /* Walnut addition (as in every ported module): stack-usage sampling. */
        (void)task_stats_update_periodic(g_sms.task_ref, "SMS", MODULE_ID_SMS,
                                         &g_sms.task_stats, 0);

        sms_configure();

        /* Walnut: drain the SDK queue so asynchronous SDK_SMS_EVT_INCOMING
         * events land in urc_q. The reference had urc_processor fill urc_q
         * with the +CMTI URC lines instead. */
        sms_flush_temp_queue();

        if (g_sms.module->config.urc_q) {
            sms_urc_queued_t urc_el;
            UINT32           urc_popped;
            while (queue_pop(g_sms.module->config.urc_q,
                             &g_sms.module->config.urc_q_config,
                             &urc_el,
                             1U,
                             &urc_popped) == RESULT_SUCCESS &&
                   urc_popped > 0U) {
                /* reference: urc_el.hdr.msg_id == SDK_MSG_URC &&
                 *            urc_el.hdr.arg1 == SDK_URC_SMS_MASK */
                if (urc_el.hdr.type == SDK_SMS_EVT_INCOMING) {
                    urc_el.hdr.text = urc_el.arg3_inline;   /* reference: hdr.arg3 */
                    sdk_debug_print("SMS URC (async): index=%ld\r\n", (long)urc_el.hdr.index);
                    sms_process_urc(&urc_el.hdr);
                }
            }
        }

        ModuleMessage msg;
        UINT32 popped = 0;

        if (queue_pop(g_sms.module->config.msg_q,
                      &g_sms.module->config.msg_q_config,
                      &msg,
                      1,
                      &popped) == RESULT_SUCCESS && popped)
        {
            const char *recipient = msg.address;
            const char *message_text = module_message_payload_ptr(&msg);
            size_t message_len = module_message_payload_len(&msg);

            if (recipient && recipient[0] != '\0' && message_text && message_len > 0)
            {
                if (sms_send_internal(recipient, message_text, (UINT32)message_len) == RESULT_SUCCESS)
                {
                    g_sms.stats.total_sent++;
                }
                else
                {
                    g_sms.stats.send_errors++;
                    sdk_log_error("ERR_SMS_SEND_FAILED"); /* ERRC */
                }
            }
            else
            {
                g_sms.stats.send_errors++;
                sdk_log_error("ERR_SMS_SEND_FAILED"); /* ERRC */
            }
            if (msg.use_dynamic_buffer && msg.dynamic_buffer)
            {
                free(msg.dynamic_buffer);
            }
        }

        utils_sleep_ms(g_sms.task_interval_ms);
    }
}

/*===============================================================
 * Internal SMS Send
 *==============================================================*/
static Result sms_send_internal(const char *recipient, const char *message, UINT32 message_len)
{
    if (g_temp_msg_queue == NULL || !g_sms.config || message_len == 0) {
        return RESULT_ERROR;
    }

    sms_flush_temp_queue();

    /* WALNUT API DIFFERENCE:
     *   reference: sdk_sms_send(format_mode, message, message_len, recipient,
     *              msgq) - asynchronous, format/length explicit.
     *   walnut:    sdk_sms_send(number, text) - blocking, text mode only, body
     *              length taken from the NUL terminator, no msgq.
     * The payload out of ModuleMessage is length-delimited (data_len) and not
     * guaranteed NUL-terminated, so it is copied into a bounded buffer first.
     * g_sms.config->format_mode is still honoured - it is applied to the modem
     * by sms_configure()/sms_manager_set_format_mode() via AT+CMGF; walnut's
     * send takes no per-message format argument. */
    if (message_len > SMS_MANAGER_MAX_MESSAGE_LENGTH) {
        sdk_log_error("ERR_SMS_SEND_FAILED: body %u > %d chars",
                  (unsigned)message_len, SMS_MANAGER_MAX_MESSAGE_LENGTH); /* ERRC */
        return RESULT_ERROR;
    }

    char text[SMS_MANAGER_MAX_MESSAGE_LENGTH + 1];
    if (utils_memcpy_safe(text, sizeof(text), message, (size_t)message_len) < 0) {
        sdk_log_error("ERR_SMS_SEND_FAILED: body copy failed"); /* ERRC */
        return RESULT_ERROR;
    }
    text[message_len] = '\0';

    SdkResult result = sdk_sms_send(recipient, text);
    return (result == SDK_RESULT_SUCCESS) ?
            RESULT_SUCCESS : (sdk_log_error("ERR_SMS_SEND_FAILED: result %d", (int)result) /* ERRC */ , RESULT_ERROR);
}

/*===============================================================
 * URC Handling
 *==============================================================*/
static void sms_process_urc(const SdkSmsMessage *msg)
{
    if (!msg) {
        sdk_debug_print("SMS URC: null message\r\n");
        return;
    }

    /* reference: if (msg->arg2 != SDK_URC_NEW_MSG_IND) */
    if (msg->type != SDK_SMS_EVT_INCOMING) {
        sdk_debug_print("Ignoring non-new-message SMS URC\r\n");
        return;
    }

    /* WALNUT API DIFFERENCE: the message index arrives as a field. The
     * reference had to parse it out of the raw "+CMTI: \"SM\",12" line with
     * strrchr(',')/atoi (with a fallback that atoi'd the whole string), because
     * its URC carried only that text. */
    int index = (int)msg->index;
    if (index <= 0) {
        sdk_log_warning("SMS URC: invalid index parsed: %d", index);
        return;
    }

    sdk_debug_print("SMS URC: parsed index: %d\r\n", index);

    sms_message_t sms = {0};
    BOOL have_message = FALSE;

    /* WALNUT API DIFFERENCE: the kernel already read the message before
     * notifying us, so the incoming event carries the raw +CMGR response - no
     * second sdk_sms_read() round-trip is needed. When the text is absent or
     * not a +CMGR response, fall back to the reference's read-by-index path. */
    if (msg->text && strstr(msg->text, "+CMGR:") != NULL) {
        sms_extract_fields(msg->text, sms.sender_number, sms.message_content);
        sms.message_index = index;
        sms.timestamp = SDK_GET_TICKS();
        sms.is_read = TRUE;
        g_sms.stats.total_received++;
        have_message = TRUE;
        sdk_debug_print("SMS URC: inline text parsed - sender=%s, content_len=%u\r\n",
                  sms.sender_number, (unsigned)strlen(sms.message_content));
    } else {
        have_message = sms_read_message(index, &sms);
    }

    if (!have_message)
    {
        sms_manager_delete(index);
        return;
    }

    if (sms_is_valid_sender(sms.sender_number) && sms_is_valid_content(sms.message_content))
        sms_forward_inbound(&sms);

    sms_manager_delete(index);
}

/*===============================================================
 * SMS Read
 *==============================================================*/
static BOOL sms_read_message(INT32 index, sms_message_t *out)
{
    SdkSmsMessage rsp = {0};   /* reference: sdk_msg_t rsp */
    BOOL success = FALSE;

    if (!out || g_temp_msg_queue == NULL || !g_sms.config) {
        sdk_log_error("SMS read: invalid parameters (out=%p, queue=%p, config=%p)",
                  out, g_temp_msg_queue, g_sms.config);
        if (out) {
            g_sms.stats.receive_errors++;
            sdk_log_error("ERR_SMS_READ_FAILED"); /* ERRC */
        }
        return FALSE;
    }

    sdk_debug_print("Reading SMS at index %d\r\n", (int)index);
    sms_flush_temp_queue();

    /* Read SMS from the SIM store.
     * Reference passed the literal storage id 1 (its SDK's TEXT/ME selector).
     * WALNUT DIFFERENCE: 1 == SDK_SMS_STORAGE_ME here, while received messages
     * live in SM (the store +CMTI reports), so the named SM selector is used. */
    SdkResult read_result = sdk_sms_read(SDK_SMS_STORAGE_SM, (UINT32)index, g_temp_msg_queue);
    if (read_result != SDK_RESULT_SUCCESS) {
        sdk_log_error("SMS read: sdk_sms_read failed for index %d, result=%d", (int)index, (int)read_result);
        g_sms.stats.receive_errors++;
        sdk_log_error("ERR_SMS_READ_FAILED"); /* ERRC */
        return FALSE;
    }

    sdk_debug_print("SMS read: waiting for response (timeout=%u ms)\r\n", (unsigned)g_sms.config->urc_timeout_ms);

    /* Walnut: this queue also carries asynchronous incoming messages, so a
     * stray SDK_SMS_EVT_INCOMING can arrive before the read result. Park it and
     * keep waiting instead of failing the read (the reference queue only ever
     * held operation results, so it did a single recv). */
    SdkResult recv_result;
    for (;;) {
        memset(&rsp, 0, sizeof(rsp));
        recv_result = sdk_msgq_recv(g_temp_msg_queue, &rsp, g_sms.config->urc_timeout_ms);
        if (recv_result != SDK_RESULT_SUCCESS) {
            break;
        }
        if (rsp.type == SDK_SMS_EVT_READ_RESULT) {
            break;
        }
        sms_park_incoming(&rsp);
        sdk_sms_msg_free(&rsp);
    }

    if (recv_result != SDK_RESULT_SUCCESS) {
        sdk_log_error("SMS read: msgq_recv failed for index %d, result=%d (timeout=%u ms)",
                  (int)index, (int)recv_result, (unsigned)g_sms.config->urc_timeout_ms);
        g_sms.stats.receive_errors++;
        sdk_log_error("ERR_SMS_READ_FAILED"); /* ERRC */
        return FALSE;
    }

    /* reference: if (!rsp.arg3) */
    if (!rsp.text) {
        sdk_log_error("ERR_SMS_READ_FAILED: SMS read: response received but text is NULL (status=%d)",
                  (int)rsp.status);
        g_sms.stats.receive_errors++;
        return FALSE;
    }

    const char *response_text = (const char *)rsp.text;
    /* Don't use strlen() on SDK-provided data - it might not be null-terminated */
    /* The SDK should provide null-terminated strings, but be safe */
    sdk_debug_print("SMS read: response received\r\n");

    if (strstr(response_text, "+CMGR:") == NULL) {
        sdk_log_warning("SMS read: unexpected response type (expected +CMGR:), response: %s", response_text);
        sdk_sms_msg_free(&rsp);   /* reference: sdk_memory_free(rsp.arg3) */
        sms_flush_temp_queue();
        sdk_log_error("ERR_SMS_READ_FAILED"); /* ERRC */
        g_sms.stats.receive_errors++;
        return FALSE;
    }

    sms_extract_fields(response_text, out->sender_number, out->message_content);
    out->message_index = (int)index;
    out->timestamp = SDK_GET_TICKS();
    out->is_read = TRUE;
    g_sms.stats.total_received++;
    success = TRUE;

    sdk_debug_print("SMS read: success - sender=%s, content_len=%u\r\n",
              out->sender_number, (unsigned)strlen(out->message_content));

    sdk_sms_msg_free(&rsp);       /* reference: sdk_memory_free(rsp.arg3) */

    return success;
}

/*===============================================================
 * SMS Parsing
 *==============================================================*/
static void sms_extract_fields(const char *raw, char *sender, char *content)
{
    sender[0] = content[0] = '\0';

    if (!raw)
    {
        utils_strncpy_safe(sender, "UNKNOWN", SMS_MANAGER_MAX_ADDRESS_LENGTH);
        sdk_log_error("ERR_SMS_UNKNOWN_NUMBER"); /* ERRC */
        return;
    }

    const char *cmgr = strstr(raw, "+CMGR:");
    if (!cmgr)
    {
        utils_strncpy_safe(content, raw, SMS_MANAGER_MAX_MESSAGE_LENGTH);
        utils_strncpy_safe(sender, "UNKNOWN", SMS_MANAGER_MAX_ADDRESS_LENGTH);
        return;
    }

    /* Parse format: +CMGR: "status","sender_number","","timestamp"\r\ncontent */
    const char *p = strchr(cmgr, '"');
    if (p)
    {
        p = strchr(p + 1, '"');  /* Skip status field */
        if (p)
        {
            p = strchr(p + 1, '"');  /* Find sender start */
            if (p)
            {
                const char *quote_start = p + 1;
                p = strchr(p + 1, '"');  /* Find sender end */
                if (p)
                {
                    size_t len = p - quote_start;
                    if (len >= SMS_MANAGER_MAX_ADDRESS_LENGTH)
                        len = SMS_MANAGER_MAX_ADDRESS_LENGTH - 1;
                    /* Use safe copy with explicit length */
                    if (len > 0) {
                        utils_memcpy_safe(sender, SMS_MANAGER_MAX_ADDRESS_LENGTH, quote_start, len);
                        sender[len] = '\0';
                    } else {
                        sender[0] = '\0';
                    }
                }
            }
        }
    }

    const char *body = strstr(raw, "\r\n");
    if (body)
    {
        body += 2;
        /* Use safe length calculation - find end of body or max length */
        /* Don't use strlen() as SMS data might not be null-terminated */
        size_t len = 0;
        const char *body_end = strstr(body, "\r\n");
        if (body_end)
        {
            len = body_end - body;
        }
        else
        {
            /* No trailing \r\n found, calculate safe length */
            const char *p = body;
            while (len < SMS_MANAGER_MAX_MESSAGE_LENGTH && *p != '\0' && *p != '\r' && *p != '\n')
            {
                len++;
                p++;
            }
        }

        /* Trim trailing whitespace */
        while (len > 0 && (body[len - 1] == '\r' || body[len - 1] == '\n'))
            len--;

        if (len >= SMS_MANAGER_MAX_MESSAGE_LENGTH)
            len = SMS_MANAGER_MAX_MESSAGE_LENGTH - 1;

        /* Use safe copy with explicit length */
        if (len > 0) {
            utils_memcpy_safe(content, SMS_MANAGER_MAX_MESSAGE_LENGTH, body, len);
            content[len] = '\0';
        } else {
            content[0] = '\0';
        }
    }

    if (!sender[0])
    {
        utils_strncpy_safe(sender, "UNKNOWN", SMS_MANAGER_MAX_ADDRESS_LENGTH);
    }
}

/*===============================================================
 * SIM Events
 *==============================================================*/
static void sms_on_sim_status(const EventData *event, void *user_data)
{
    (void)user_data;

    if (!event) {
        return;
    }

    if (event->type == EVENT_SIM_AVAILABLE) {
        g_sms_sim_inserted = TRUE;
    } else if (event->type == EVENT_SIM_UNAVAILABLE) {
        g_sms_sim_inserted = FALSE;
    }
}

static void sms_configure(void)
{
    /* EVENT_SIM_AVAILABLE may have been broadcast before we registered the handler.
     * reference: sim_manager_get_sim_status() - walnut: weware_sim_get_sim_status() */
    if (!g_sms_sim_inserted && weware_sim_get_sim_status()) {
        g_sms_sim_inserted = TRUE;
    }

    if (!g_sms_sim_inserted) {
        s_sms_modem_config_applied = FALSE;
        s_sms_config_attempts = 0;
        return;
    }

    if (s_sms_modem_config_applied) {
        return;
    }

    if (g_temp_msg_queue == NULL || !g_sms.config) {
        sdk_log_warning("SMS configure: temp queue or config not available");
        return;
    }

    /* Also attaches g_temp_msg_queue as the incoming-SMS route (walnut:
     * sdk_sms_msgq_poll) - must happen before sdk_sms_init() turns +CMTI on so
     * no arrival is missed. */
    sms_flush_temp_queue();

    s_sms_config_attempts++;
    sdk_debug_print("Configuring SMS settings (attempt %u)\r\n", (unsigned)s_sms_config_attempts);

    SdkResult ret = -1;
    BOOL essential_ok = TRUE;   /* walnut addition: drives the retry latch below */

    /* WALNUT EXTRA API (no CG counterpart): bring up the vendor SMS task/queue
     * and register the incoming-SMS hook. Idempotent; also applies the SDK
     * defaults (AT+CMGF=1, AT+CSCS="GSM", AT+CNMI=2,1,0,0,0) which the explicit
     * calls below then re-assert with our configured values.
     * NOTE: the prebuilt sdk_sms_init() returns 0 unconditionally (it is
     * wm_sms_init() + wm_sms_set_incoming_cb() with a hardcoded `return 0`), so
     * this check can never fire - kept for API correctness. */
    ret = sdk_sms_init();
    if (ret != SDK_RESULT_SUCCESS) {
        sdk_log_error("ERR_SMS_CONFIG_FAILED: sdk_sms_init failed: %d", (int)ret); /* ERRC */
        return;   /* retry on the next task cycle */
    }

    { /* delete previous SMS in SIM */
        ret = sdk_sms_delete_all(g_temp_msg_queue);
        if (ret == SDK_RESULT_SUCCESS) {
            sdk_debug_print("SMS: deleted all messages\r\n");
        } else if (ret != SDK_RESULT_NOT_SUPPORTED) {
            /* WALNUT SDK DEFECT: sdk_sms_delete_all() issues "AT+CMGD=0,4" -
             * index 0 is out of range for the SIM store (indices start at 1), so
             * the modem answers ERROR and this always returns -1 on this kernel.
             * Fall back to a bounded per-index sweep using the (walnut-extra)
             * storage query, which uses only in-range indices. */
            sdk_debug_print("SMS: delete all returned %d, sweeping per-index\r\n", (int)ret);
            if (!sms_purge_store()) {
                essential_ok = FALSE;
            }
        } else sdk_log_error("ERR_SMS_DELETE_FAILED: delete all not supported"); /* ERRC */
    }

    { /* set SMS character format*/
        ret = sdk_sms_set_format((UINT8)g_sms.config->format_mode);
        if (ret == SDK_RESULT_SUCCESS) {
            sdk_debug_print("SMS format set to %u\r\n", (unsigned)g_sms.config->format_mode);
        } else if (ret != SDK_RESULT_NOT_SUPPORTED) {
            sdk_log_warning("SMS format set failed: %d", (int)ret);
            essential_ok = FALSE;
        } else sdk_log_error("ERR_SMS_CONFIG_FAILED: set format not supported"); /* ERRC */
    }

    { /* set SMS new message indication policy */
        /* WALNUT PARAMETER CHANGE - the reference passes (1, 2, 1, 0, 0):
         *   mt=2 routes SMS-DELIVER straight to the TE as +CMT, and bm=1 routes
         *   cell broadcasts to the TE. This modem rejects that combination
         *   ("AT+CNMI=1,2,1,0,0" -> ERROR), and had it succeeded it would have
         *   BROKEN inbound SMS: the walnut kernel's inbound hook is a +CMTI
         *   callback (wm_sms_cmti_cb / smsSetCmtiCallback, "sms cmti:
         *   storage=%s, index=%d"), which only fires when the message is
         *   stored and indicated - i.e. mt=1.
         * So we assert the kernel's own default instead: mode=2 (buffer URCs
         * while the link is reserved, flush after), mt=1 (store + indicate with
         * +CMTI), bm/ds/bfr = 0. */
        ret = sdk_sms_set_new_msg_ind(2, 1, 0, 0, 0);
        if (ret == SDK_RESULT_SUCCESS) {
            sdk_debug_print("SMS new message indication configured (+CMTI)\r\n");
        } else if (ret != SDK_RESULT_NOT_SUPPORTED) {
            sdk_log_warning("SMS new message indication setup failed: %d", (int)ret);
            essential_ok = FALSE;   /* without +CMTI no SMS is ever received */
        } else sdk_log_error("ERR_SMS_CONFIG_FAILED: set new msg ind not supported"); /* ERRC */
    }

    { /* set SMS character set protocol: standard 7-bit GSM */
        ret = sdk_sms_set_charset(0);
        if (ret == SDK_RESULT_SUCCESS) {
            sdk_debug_print("SMS charset set to GSM 7-bit\r\n");
        } else if (ret != SDK_RESULT_NOT_SUPPORTED) {
            sdk_log_warning("SMS charset set failed: %d", (int)ret);
            essential_ok = FALSE;
        } else sdk_log_error("ERR_SMS_CONFIG_FAILED: set charset not supported"); /* ERRC */
    }

    /* Reference latched unconditionally here. Walnut retries a bounded number
     * of times so the storage-dependent commands get another chance once the
     * SIM's SMS store has finished loading. */
    if (!essential_ok && s_sms_config_attempts < SMS_CONFIG_MAX_ATTEMPTS) {
        sdk_log_warning("SMS modem config incomplete, retrying (%u/%u)",
                        (unsigned)s_sms_config_attempts, (unsigned)SMS_CONFIG_MAX_ATTEMPTS);
        return;
    }

    if (essential_ok) {
        sdk_log_info("SMS modem configured");
    } else {
        sdk_log_error("ERR_SMS_CONFIG_FAILED: giving up after %u attempts - "
                      "inbound SMS may not work", (unsigned)s_sms_config_attempts); /* ERRC */
    }

    if (g_sms_sim_inserted)
        s_sms_modem_config_applied = TRUE;
}

/*===============================================================
 * Init (last — bottom-up entry)
 *==============================================================*/

Result sms_manager_deinit(void)
{
    if (!g_sms.module || !g_sms.module->status.initialized)
        return RESULT_SUCCESS;

    g_sms_sim_inserted = FALSE;
    s_sms_modem_config_applied = FALSE;
    event_manager_unregister_module("SMS Manager");

    if (g_sms.task_ref) {
        sdk_task_delete(g_sms.task_ref);
        g_sms.task_ref = NULL;
    }

    if (g_sms.module->config.urc_q != NULL) {
        queue_manager_destroy(g_sms.module->config.urc_q, &g_sms.module->config.urc_q_config);
        g_sms.module->config.urc_q = NULL;
    }

    if (g_sms.module->config.msg_q) {
        queue_manager_destroy(g_sms.module->config.msg_q, &g_sms.module->config.msg_q_config);
        g_sms.module->config.msg_q = NULL;
    }

    if (g_temp_msg_queue != NULL) {
        /* Walnut: the SDK has no sdk_sms_deinit(), so the vendor SMS task keeps
         * running and holds this queue as its incoming route. Flush (frees any
         * heap text) before deleting it. */
        sms_flush_temp_queue();
        sdk_msgq_delete(g_temp_msg_queue);
        g_temp_msg_queue = NULL;
    }

    event_manager_broadcast(EVENT_SMS_DISCONNECTED, "SMS Manager", NULL, 0);
    g_sms.module = NULL;
    g_sms.config = NULL;
    memset(&g_sms.stats, 0, sizeof(g_sms.stats));
    sdk_log_info("SMS manager stopped");
    return RESULT_SUCCESS;
}

Result sms_manager_init(void)
{
    g_sms.module = g_modules[MODULE_ID_SMS];
    if (!g_sms.module || g_sms.module->status.initialized)
        return RESULT_SUCCESS;

    g_sms.config = &g_sms_config;

    if (queue_manager_create(&g_sms.module->config.msg_q_config,
                             &g_sms.module->config.msg_q) != RESULT_SUCCESS)
        return RESULT_ERROR;

    if (g_temp_msg_queue == NULL) {
        /* reference element size: sizeof(sdk_msg_t) - walnut queues carry
         * SdkSmsMessage (the SMS API requires msg_size == sizeof(SdkSmsMessage)). */
        g_temp_msg_queue = sdk_msgq_create("sms_tempq", sizeof(SdkSmsMessage),
                                           SMS_MANAGER_QUEUE_SIZE, 0);
        if (g_temp_msg_queue == NULL) {
            sdk_log_error("SMS temp queue create failed");
            queue_manager_destroy(g_sms.module->config.msg_q, &g_sms.module->config.msg_q_config);
            g_sms.module->config.msg_q = NULL;
            return RESULT_ERROR;
        }
    }

    if (g_sms.module->config.urc_q_config.capacity > 0U &&
        g_sms.module->config.urc_q_config.element_size > 0U) {
        if (queue_manager_create(&g_sms.module->config.urc_q_config,
                                 &g_sms.module->config.urc_q) != RESULT_SUCCESS) {
            sdk_log_error("SMS URC queue create failed");
            queue_manager_destroy(g_sms.module->config.msg_q, &g_sms.module->config.msg_q_config);
            g_sms.module->config.msg_q = NULL;
            return RESULT_ERROR;
        }
    }

    event_manager_register(EVENT_SIM_AVAILABLE, sms_on_sim_status, NULL, "SMS Manager");
    event_manager_register(EVENT_SIM_UNAVAILABLE, sms_on_sim_status, NULL, "SMS Manager");

    /* reference passed g_sms.task_stack / sizeof(task_stack); walnut lets the
     * kernel allocate the stack (stack_ptr = NULL). */
    g_sms.task_ref = sdk_task_create(sms_send_task,
                                     NULL,
                                     "smsTask",
                                     NULL,
                                     SMS_MANAGER_TASK_STACK_SIZE,
                                     g_sms.task_priority);
    if (!g_sms.task_ref) {
        if (g_sms.module->config.urc_q != NULL) {
            queue_manager_destroy(g_sms.module->config.urc_q, &g_sms.module->config.urc_q_config);
            g_sms.module->config.urc_q = NULL;
        }
        queue_manager_destroy(g_sms.module->config.msg_q, &g_sms.module->config.msg_q_config);
        g_sms.module->config.msg_q = NULL;
        return RESULT_ERROR;
    }

    event_manager_broadcast(EVENT_SMS_CONNECTED, "SMS Manager", NULL, 0);
    sdk_log_info("SMS manager ready");
    return RESULT_SUCCESS;
}
