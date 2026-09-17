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
 * SDK API mapping (CG "sdk_functionality_sms.h"  ->  walnut "wm_sdk_sms.h")
 * ===========================================================================
 * PRESENT WITH THE SAME SIGNATURE (used verbatim):
 *   wm_sdk_sms_read(UINT8 storage, UINT32 index, void *msgq)
 *   wm_sdk_sms_delete(UINT32 index, void *msgq)
 *   wm_sdk_sms_delete_all(void *msgq)
 *   wm_sdk_sms_set_format(UINT8 format)
 *   wm_sdk_sms_set_charset(UINT8 charset)
 *   wm_sdk_sms_set_new_msg_ind(UINT8 mode, mt, bm, ds, bfr)
 *   wm_sdk_sms_msgq_poll(void *msgq, UINT32 *msg_count)
 *
 * PRESENT BUT DIFFERENT (adapted, see sms_send_internal):
 *   CG:     wm_sdk_sms_send(format_mode, message, message_len, recipient, msgq)
 *           -> asynchronous, result posted to msgq.
 *   WALNUT: wm_sdk_sms_send(number, text)
 *           -> BLOCKING until the network accepts/rejects; text mode only,
 *              NUL-terminated body, no msgq and no format/length arguments.
 *
 * MISSING IN THE WALNUT SDK (worked around here):
 *   sdk_platform_register_sms_ops() / the whole SdkSmsFunctionalityOps
 *           dispatcher - walnut has no functionality layer for SMS; app code
 *           calls the kernel wm_sdk_sms_* symbols directly.
 *   SDK_SMS_MAX_ADDRESS_LENGTH - defined in sms_manager.h instead.
 *   sdk_msg_t / SDK_MSG_URC / SDK_URC_SMS_MASK / SDK_URC_NEW_MSG_IND - walnut
 *           has no generic modem-message type and urc_processor never sees an
 *           SMS URC. The queue element is wm_SdkSmsMessage and the "new message"
 *           discriminator is wm_SdkSmsMessage.type == WM_SDK_SMS_EVT_INCOMING.
 *   wm_sdk_memory_free(msg.arg3) for received text -> wm_sdk_sms_msg_free(&msg).
 *
 * EXTRA WALNUT APIs USED (no CG counterpart):
 *   wm_sdk_sms_init()      - brings up the vendor SMS task/queue and registers the
 *                         incoming-SMS hook. Must be called once before any
 *                         other SMS call; invoked from sms_configure().
 *   wm_sdk_sms_msg_free()  - releases the heap-owned 'text' of a received
 *                         wm_SdkSmsMessage (replaces the CG wm_sdk_memory_free).
 *
 * EXTRA WALNUT APIs AVAILABLE BUT NOT USED:
 *   wm_sdk_sms_get_storage_status(name, name_size, used, total) - AT+CPMS?
 *                         occupancy query. The reference has no equivalent, so
 *                         it is deliberately left out of the port.
 *
 * ===========================================================================
 * Inbound path difference (the one structural adaptation)
 * ===========================================================================
 * Reference: urc_processor pushed the raw "+CMTI: \"SM\",12" line into the SMS
 * module urc_q; the SMS task popped it, parsed the index, then called
 * wm_sdk_sms_read() to fetch the body.
 *
 * Walnut: the kernel reads the message itself and the SDK posts an
 * wm_SdkSmsMessage{type=WM_SDK_SMS_EVT_INCOMING, index, text=<raw +CMGR response>}
 * to whichever queue was last attached via wm_sdk_sms_msgq_poll() - i.e. our
 * g_temp_msg_queue, shared with the read/delete results. sms_flush_temp_queue()
 * therefore parks incoming events into the module urc_q (instead of dropping
 * them like the reference could) and the task drains that urc_q into
 * sms_process_urc() exactly as before. sms_read_message() is retained and is
 * still used whenever an incoming event carries no usable +CMGR text.
 *
 * Walnut logging: LOG_ERRC(ERR_*, ...) has no walnut counterpart (no
 * common/error_codes.h, no LOG_ERRC in module/log/log.h); those calls become
 * LOG_ERROR with the reference error-code name kept in the message text. The
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
#include "wm_sdk_os.h"             /* reference: functionality/sdk_functionality_os.h */
#include "wm_sdk_sms.h"            /* reference: functionality/sdk_functionality_sms.h */
#include "wm_sdk_log.h"
/* Kernel SMS API. Needed for wm_sms_set_preferred_storage() (AT+CPMS=...),
 * which wm_sdk_sms.h deliberately does NOT expose - wm_sdk_sms_get_storage_status()
 * is documented read-only - yet wm_sms_init() never sets the preferred store
 * either, so without this the read/store memories are whatever the modem
 * defaults to. Same "reach past the SDK to the kernel" pattern the UART port
 * uses. No CG counterpart: the CG SDK selected the store inside sms_read. */
#include "wm_sms_secure.h"

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
#define LOG_MODULE_LEVEL LOG_LEVEL_DEBUG
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
 * messages - wm_sdk_sms_msgq_poll() attaches it (see sms_flush_temp_queue). */
static void* g_temp_msg_queue = NULL;

/* TRUE after EVENT_SIM_AVAILABLE until EVENT_SIM_UNAVAILABLE (SIM insert/remove). */
static volatile BOOL g_sms_sim_inserted = FALSE;
/* Latched in sms_configure() after a successful modem setup for current insert (SMS task only). */
static BOOL s_sms_modem_config_applied = FALSE;
/* Walnut addition: retry state for the current SIM insert. The reference
 * latched unconditionally, which on walnut leaves a half-configured modem: for
 * the first few seconds after the SIM reports READY its SMS store is still
 * loading and every storage-dependent AT command (AT+CMGD, AT+CPMS?) answers
 * ERROR. Observed on-target 2026-09-08: attempts 1-2 failed, attempt 3 passed.
 *
 * Spacing matters more than the count - at the 100 ms task interval five
 * back-to-back attempts cover only half a second, so a slower SIM would burn
 * the whole budget before the store is ready. Retries are therefore spaced
 * SMS_CONFIG_RETRY_INTERVAL_MS apart, giving a ~20 s window, and remain
 * bounded so a permanently failing modem cannot flood the modem with ATs. */
static UINT8  s_sms_config_attempts = 0;
static UINT32 s_sms_config_last_attempt_ms = 0;
#define SMS_CONFIG_MAX_ATTEMPTS       10U
#define SMS_CONFIG_RETRY_INTERVAL_MS  2000U

/*===============================================================
 * Forward Declarations
 *==============================================================*/
static void  sms_send_task(void *arg);
static void  sms_on_sim_status(const EventData *event, void *user_data);

static BOOL  sms_read_message(INT32 index, sms_message_t *out);
static void  sms_process_urc(const wm_SdkSmsMessage *msg);
static void  sms_extract_fields(const char *raw, char *sender, char *content);
static void  sms_configure(void);
static Result sms_send_internal(const char *recipient, const char *message, UINT32 message_len);
static void  sms_flush_temp_queue(void);
static void  sms_park_incoming(const wm_SdkSmsMessage *msg);
static BOOL  sms_purge_store(void);
static void  sms_log_store_status(const char *when);

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
 *       the original with wm_sdk_sms_msg_free().
 */
static void sms_park_incoming(const wm_SdkSmsMessage *msg)
{
    if (!msg || msg->type != WM_SDK_SMS_EVT_INCOMING) {
        return;
    }

    if (!g_sms.module || g_sms.module->config.urc_q == NULL) {
        LOG_ERROR("ERR_SMS_MISSED: inbound SMS dropped, urc_q unavailable"); /* ERRC */
        return;
    }

    sms_urc_queued_t urc_el;
    memset(&urc_el, 0, sizeof(urc_el));
    urc_el.hdr = *msg;
    urc_el.hdr.text = NULL;   /* heap pointer must not escape into the ring buffer */
    size_t raw_len = 0;
    if (msg->text) {
        raw_len = strlen(msg->text);
        utils_strncpy_safe(urc_el.arg3_inline, msg->text, sizeof(urc_el.arg3_inline));
    }

    /* The raw +CMGR response must fit SMS_URC_TEXT_MAX or the body is cut. */
    if (raw_len >= SMS_URC_TEXT_MAX) {
        LOG_WARN("SMS>rx3 TRUNCATED raw %u -> %u bytes (idx=%ld) - body may be cut",
                        (unsigned)raw_len, (unsigned)(SMS_URC_TEXT_MAX - 1U),
                        (long)msg->index);
    }

    if (queue_push(g_sms.module->config.urc_q,
                   &g_sms.module->config.urc_q_config,
                   &urc_el) != RESULT_SUCCESS) {
        LOG_ERROR("ERR_SMS_MISSED: inbound SMS dropped, urc_q push failed"); /* ERRC */
        return;
    }

    LOG_DEBUG("SMS>rx3 parked to urc_q idx=%ld raw_len=%u",
                    (long)msg->index, (unsigned)raw_len);
}

/**
 * @brief Flush any pending messages from the temporary message queue
 * @note Properly frees any allocated memory in messages to prevent leaks
 * @note Walnut: incoming messages share this queue, so they are parked in the
 *       module urc_q first instead of being discarded (reference dropped
 *       everything - its queue only ever held operation results).
 *       wm_sdk_sms_msgq_poll() doubles as the "attach this queue as the
 *       incoming-SMS route" call, so it must run before wm_sdk_sms_init().
 */
static void sms_flush_temp_queue(void)
{
    if (g_temp_msg_queue == NULL) {
        return;
    }

    UINT32 msgcount = 0;
    if (wm_sdk_sms_msgq_poll(g_temp_msg_queue, &msgcount) != WM_SDK_RESULT_SUCCESS || msgcount == 0) {
        return;   /* silent: this runs every task cycle, do not log the idle case */
    }

    LOG_DEBUG("SMS>rx1 sdk queue: %u event(s) pending", (unsigned)msgcount);

    while (msgcount > 0) {
        wm_SdkSmsMessage msg = {0};
        if (wm_sdk_msgq_recv(g_temp_msg_queue, &msg, 0) == WM_SDK_RESULT_SUCCESS) {
            /* type: 0=INCOMING 1=READ_RESULT 2=DELETE_RESULT */
            LOG_DEBUG("SMS>rx2 evt type=%d status=%d idx=%ld text=%s",
                            (int)msg.type, (int)msg.status, (long)msg.index,
                            msg.text ? msg.text : "(none)");
            sms_park_incoming(&msg);      /* walnut addition: keep inbound SMS */
            /* reference: if (msg.arg3) wm_sdk_memory_free((void*)msg.arg3); */
            wm_sdk_sms_msg_free(&msg);
            msgcount--;
        } else {
            break;
        }
    }
}

/** Upper bound on the per-index delete sweep (SIM SMS stores hold 20-50). */
#define SMS_PURGE_MAX_INDEX 60U

/**
 * @brief Log the active SMS store name and occupancy (AT+CPMS?)
 * @note WALNUT ADDITION, diagnostic. This is the only way to see which memory
 *       +CMTI indexes refer to and whether messages are actually landing: if
 *       `used` climbs after an inbound SMS but no WM_SDK_SMS_EVT_INCOMING arrives,
 *       delivery works and the notification/read path is at fault; if `used`
 *       stays 0, the message never reached the device at all.
 */
static void sms_log_store_status(const char *when)
{
    char   store[12] = {0};
    UINT32 used = 0;
    UINT32 total = 0;

    wm_SdkResult ret = wm_sdk_sms_get_storage_status(store, sizeof(store), &used, &total);
    if (ret == WM_SDK_RESULT_SUCCESS) {
        LOG_INFO("SMS store %s: %u/%u used (%s)",
                     store, (unsigned)used, (unsigned)total, when ? when : "");
    } else {
        LOG_WARN("SMS store status unavailable (%s): %d",
                        when ? when : "", (int)ret);
    }
}

/**
 * @brief Empty the preferred SMS store one index at a time
 * @note WALNUT ADDITION - a fallback for the reference's single
 *       wm_sdk_sms_delete_all() call, which maps to "AT+CMGD=0,4". That command
 *       answers ERROR for the first few seconds after the SIM reports READY
 *       (the SIM's SMS store is still loading) and then starts working, so this
 *       sweep only ever runs on the early attempts. It asks AT+CPMS? how many
 *       slots are used (wm_sdk_sms_get_storage_status - a walnut-extra API with no
 *       CG counterpart) and deletes in-range indices until that many are gone.
 * @note On-target 2026-09-08: while the store is not ready, AT+CPMS? fails too,
 *       so this returns FALSE and sms_configure() simply retries. Kept because
 *       it is the only way to clear the store should AT+CMGD=0,4 be rejected
 *       for good on some modem/SIM.
 * @return TRUE when the store is - or already was - empty; FALSE if it could
 *         not be queried or could not be fully emptied.
 */
static BOOL sms_purge_store(void)
{
    char   store[12] = {0};
    UINT32 used = 0;
    UINT32 total = 0;

    wm_SdkResult ret = wm_sdk_sms_get_storage_status(store, sizeof(store), &used, &total);
    if (ret != WM_SDK_RESULT_SUCCESS) {
        LOG_WARN("SMS purge: storage query failed: %d", (int)ret);
        return FALSE;
    }

    LOG_DEBUG("SMS purge: store %s %u/%u used",
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
        if (wm_sdk_sms_delete(index, g_temp_msg_queue) == WM_SDK_RESULT_SUCCESS) {
            deleted++;
            g_sms.stats.total_deleted++;
        }
    }

    LOG_DEBUG("SMS purge: deleted %u of %u",
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

    /* Walnut addition: log the command text, not just its length. The reference
     * logged neither; with only `content_len=NN` in the trace, correlating a
     * bad reply back to the command that caused it means counting characters. */
    Result rc = command_manager_accept_request(&cmd_request);

    LOG_DEBUG("SMS>rx8 -> CMD from='%s' cmd[%u]='%s' rc=%d",
                    cmd_request.address, (unsigned)cmd_request.data_len,
                    cmd_request.message, (int)rc);

    if (rc != RESULT_SUCCESS)
        LOG_ERROR("ERR_SMS_MISSED: Failed to queue inbound SMS command"); /* ERRC */
}

static BOOL sms_is_valid_sender(const char *num)
{
    if (!num || !num[0] || strcmp(num, "UNKNOWN") == 0)
    {
        LOG_ERROR("ERR_SMS_UNKNOWN_NUMBER"); /* ERRC */
        return FALSE;
    }

    for (const char *p = num; *p; ++p)
    {
        char c = *p;
        if (!(isdigit((unsigned char)c) || strchr("+- ()", c)))
        {
            LOG_ERROR("ERR_SMS_UNKNOWN_NUMBER"); /* ERRC */
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
               : (LOG_ERROR("ERR_SMS_INVALID_CONTENT") /* ERRC */, FALSE);
}

/*===============================================================
 * Public API
 *==============================================================*/
Result sms_manager_send(const char *recipient, const char *message)
{
    if (!g_sms.module || !g_sms.module->status.initialized ||
        !recipient || !message || !g_sms.module->config.msg_q)
    {
        LOG_ERROR("ERR_SMS_SEND_FAILED"); /* ERRC */
        return RESULT_ERROR;
    }

    size_t recipient_len = strlen(recipient);
    size_t message_len = strlen(message);

    if (recipient_len >= sizeof(g_sms_send_buffer.address) ||
        message_len >= sizeof(g_sms_send_buffer.message))
    {
        LOG_ERROR("ERR_SMS_SEND_FAILED"); /* ERRC */
        return RESULT_ERROR;
    }

    memset(&g_sms_send_buffer, 0, sizeof(g_sms_send_buffer));
    g_sms_send_buffer.source_module = MODULE_ID_CMD;
    g_sms_send_buffer.destination_module = MODULE_ID_SMS;
    if (utils_strncpy_safe(g_sms_send_buffer.address, recipient, sizeof(g_sms_send_buffer.address)) < 0) {
        LOG_ERROR("ERR_SMS_SEND_FAILED"); /* ERRC */
        return RESULT_ERROR;
    }
    {
        int n = utils_strncpy_safe(g_sms_send_buffer.message, message, sizeof(g_sms_send_buffer.message));
        if (n < 0) {
            LOG_ERROR("ERR_SMS_SEND_FAILED"); /* ERRC */
            return RESULT_ERROR;
        }
        g_sms_send_buffer.data_len = (UINT32)n;
    }

    return (queue_push(g_sms.module->config.msg_q,
                       &g_sms.module->config.msg_q_config,
                       &g_sms_send_buffer) == RESULT_SUCCESS) ?
                       RESULT_SUCCESS : ( LOG_ERROR("ERR_SMS_SEND_FAILED") /* ERRC */ , RESULT_ERROR);
}

Result sms_manager_delete(int index)
{
    if (!g_sms.module || !g_sms.module->status.initialized || index <= 0)
        return RESULT_ERROR;

    if (g_temp_msg_queue == NULL) {
        LOG_ERROR("Temporary message queue not initialized");
        return RESULT_ERROR;
    }

    sms_flush_temp_queue();

    wm_SdkResult result = wm_sdk_sms_delete((UINT32)index, g_temp_msg_queue);
    if (result == WM_SDK_RESULT_SUCCESS) {
        g_sms.stats.total_deleted++;
        return RESULT_SUCCESS;
    }
    g_sms.stats.delete_errors++;
    /* Expected for an inbound message: the kernel's wm_sms_task already deleted
     * it after dispatching the incoming callback (see sms_process_urc). */
    LOG_WARN("SMS delete index %d failed (%d) - may already be gone",
                    index, (int)result); /* ERRC ERR_SMS_DELETE_FAILED */
    return RESULT_ERROR;
}

Result sms_manager_set_format_mode(int mode)
{
    if (!g_sms.config) {
        return RESULT_ERROR;
    }
    g_sms.config->format_mode = mode;
    wm_SdkResult result = wm_sdk_sms_set_format((UINT8)mode);
    return (result == WM_SDK_RESULT_SUCCESS) ?
                RESULT_SUCCESS : (LOG_ERROR("ERR_SMS_CONFIG_FAILED") /* ERRC */ , RESULT_ERROR);
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

        /* Walnut: drain the SDK queue so asynchronous WM_SDK_SMS_EVT_INCOMING
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
                LOG_DEBUG("SMS>rx4 urc_q pop type=%d idx=%ld",
                                (int)urc_el.hdr.type, (long)urc_el.hdr.index);
                if (urc_el.hdr.type == WM_SDK_SMS_EVT_INCOMING) {
                    urc_el.hdr.text = urc_el.arg3_inline;   /* reference: hdr.arg3 */
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

            /* Everything the send queue handed us, before any interpretation:
             * who produced it, the destination, the payload length and whether
             * it is inline or a heap buffer. */
            LOG_DEBUG("SMS>tx1 msg_q pop src=%d dst=%d addr='%s' len=%u dyn=%d",
                            (int)msg.source_module, (int)msg.destination_module,
                            recipient ? recipient : "(null)",
                            (unsigned)message_len, (int)msg.use_dynamic_buffer);

            if (recipient && recipient[0] != '\0' && message_text && message_len > 0)
            {
                if (sms_send_internal(recipient, message_text, (UINT32)message_len) == RESULT_SUCCESS)
                {
                    g_sms.stats.total_sent++;
                }
                else
                {
                    g_sms.stats.send_errors++;
                    LOG_ERROR("ERR_SMS_SEND_FAILED"); /* ERRC */
                }
            }
            else
            {
                g_sms.stats.send_errors++;
                LOG_ERROR("ERR_SMS_SEND_FAILED"); /* ERRC */
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
     *   reference: wm_sdk_sms_send(format_mode, message, message_len, recipient,
     *              msgq) - asynchronous, format/length explicit.
     *   walnut:    wm_sdk_sms_send(number, text) - blocking, text mode only, body
     *              length taken from the NUL terminator, no msgq.
     * The payload out of ModuleMessage is length-delimited (data_len) and not
     * guaranteed NUL-terminated, so it is copied into a bounded buffer first.
     * g_sms.config->format_mode is still honoured - it is applied to the modem
     * by sms_configure()/sms_manager_set_format_mode() via AT+CMGF; walnut's
     * send takes no per-message format argument. */
    if (message_len > SMS_MANAGER_MAX_MESSAGE_LENGTH) {
        LOG_ERROR("ERR_SMS_SEND_FAILED: body %u > %d chars",
                  (unsigned)message_len, SMS_MANAGER_MAX_MESSAGE_LENGTH); /* ERRC */
        return RESULT_ERROR;
    }

    char text[SMS_MANAGER_MAX_MESSAGE_LENGTH + 1];
    if (utils_memcpy_safe(text, sizeof(text), message, (size_t)message_len) < 0) {
        LOG_ERROR("ERR_SMS_SEND_FAILED: body copy failed"); /* ERRC */
        return RESULT_ERROR;
    }
    text[message_len] = '\0';

    /* Walnut addition: the reference logged nothing on success, which left the
     * outbound half of an SMS command round-trip completely invisible (the
     * kernel's own AT trace goes to the CP console, not this one). */
    /* Log the exact body handed to the kernel. This is the discriminator for
     * the corrupted-reply symptom seen 2026-09-08 (stale AT-command text
     * prefixed to a reply): if the junk is already visible here the corruption
     * is upstream (command manager / SMS_SEND_Q), if not it is inside
     * wm_sms_send_text. */
    LOG_DEBUG("SMS>tx2 body to=%s [%u]: '%s'",
                    recipient, (unsigned)message_len, text);

    /* wm_sdk_sms_send() blocks for the whole AT+CMGS exchange. Bracketing it makes
     * the window visible in which other tasks' AT traffic can be swallowed by
     * the modem's '>' prompt (see the CMGS-prompt note in the progress doc),
     * and gives the elapsed time for the 60 s task-stall watchdog question. */
    UINT32 t0 = utils_monotonic_ms_now();
    LOG_DEBUG("SMS>tx3 wm_sdk_sms_send ENTER (AT+CMGS window opens)");
    wm_SdkResult result = wm_sdk_sms_send(recipient, text);
    LOG_DEBUG("SMS>tx4 wm_sdk_sms_send EXIT rc=%d after %u ms",
                    (int)result, (unsigned)utils_monotonic_ms_elapsed(t0));

    if (result == WM_SDK_RESULT_SUCCESS) {
        LOG_INFO("SMS sent to %s (%u chars, %u ms)",
                     recipient, (unsigned)message_len,
                     (unsigned)utils_monotonic_ms_elapsed(t0));
        return RESULT_SUCCESS;
    }
    LOG_ERROR("ERR_SMS_SEND_FAILED: to %s, result %d (%u ms)",
                  recipient, (int)result,
                  (unsigned)utils_monotonic_ms_elapsed(t0)); /* ERRC */
    return RESULT_ERROR;
}

/*===============================================================
 * URC Handling
 *==============================================================*/
static void sms_process_urc(const wm_SdkSmsMessage *msg)
{
    if (!msg) {
        LOG_DEBUG("SMS URC: null message");
        return;
    }

    /* reference: if (msg->arg2 != SDK_URC_NEW_MSG_IND) */
    if (msg->type != WM_SDK_SMS_EVT_INCOMING) {
        LOG_DEBUG("Ignoring non-new-message SMS URC");
        return;
    }

    /* WALNUT API DIFFERENCE: the message index arrives as a field. The
     * reference had to parse it out of the raw "+CMTI: \"SM\",12" line with
     * strrchr(',')/atoi (with a fallback that atoi'd the whole string), because
     * its URC carried only that text. */
    int index = (int)msg->index;
    if (index <= 0) {
        LOG_WARN("SMS URC: invalid index parsed: %d", index);
        return;
    }

    /* The complete raw +CMGR response as received, before parsing - this is
     * what sms_extract_fields() has to work with. */
    LOG_DEBUG("SMS>rx5 process idx=%d raw='%s'",
                    index, msg->text ? msg->text : "(null)");

    sms_message_t sms = {0};
    BOOL have_message = FALSE;

    /* WALNUT API DIFFERENCE: the kernel already read the message before
     * notifying us, so the incoming event carries the raw +CMGR response - no
     * second wm_sdk_sms_read() round-trip is needed. When the text is absent or
     * not a +CMGR response, fall back to the reference's read-by-index path. */
    if (msg->text && strstr(msg->text, "+CMGR:") != NULL) {
        sms_extract_fields(msg->text, sms.sender_number, sms.message_content);
        sms.message_index = index;
        sms.timestamp = SDK_GET_TICKS();
        sms.is_read = TRUE;
        g_sms.stats.total_received++;
        have_message = TRUE;
        LOG_DEBUG("SMS>rx6 inline parse OK sender='%s' body[%u]='%s'",
                        sms.sender_number,
                        (unsigned)strlen(sms.message_content),
                        sms.message_content);
    } else {
        LOG_DEBUG("SMS>rx6 no usable +CMGR text -> re-reading index %d", index);
        have_message = sms_read_message(index, &sms);
    }

    if (!have_message)
    {
        LOG_WARN("SMS>rx7 no message for index %d - discarding", index);
        sms_manager_delete(index);
        return;
    }

    {
        BOOL sender_ok  = sms_is_valid_sender(sms.sender_number);
        BOOL content_ok = sms_is_valid_content(sms.message_content);

        LOG_DEBUG("SMS>rx7 validate sender=%s content=%s",
                        sender_ok ? "OK" : "REJECT", content_ok ? "OK" : "REJECT");

        if (sender_ok && content_ok)
            sms_forward_inbound(&sms);
    }

    /* Reference deleted the message here, and so do we. WALNUT NOTE: for the
     * INCOMING path the kernel has ALREADY deleted it - wm_sms_task calls
     * wm_sms_delete_message(index, 0) right after invoking the incoming
     * callback - so this AT+CMGD usually answers ERROR and bumps
     * stats.delete_errors. Harmless, and still required for the
     * sms_read_message() fallback path above, which does not auto-delete. */
    {
        Result del = sms_manager_delete(index);
        LOG_DEBUG("SMS>rx9 delete idx=%d rc=%d (rx done)", index, (int)del);
        (void)del;
    }
}

/*===============================================================
 * SMS Read
 *==============================================================*/
static BOOL sms_read_message(INT32 index, sms_message_t *out)
{
    wm_SdkSmsMessage rsp = {0};   /* reference: sdk_msg_t rsp */
    BOOL success = FALSE;

    if (!out || g_temp_msg_queue == NULL || !g_sms.config) {
        LOG_ERROR("SMS read: invalid parameters (out=%p, queue=%p, config=%p)",
                  out, g_temp_msg_queue, g_sms.config);
        if (out) {
            g_sms.stats.receive_errors++;
            LOG_ERROR("ERR_SMS_READ_FAILED"); /* ERRC */
        }
        return FALSE;
    }

    LOG_DEBUG("Reading SMS at index %d", (int)index);
    sms_flush_temp_queue();

    /* Read SMS by index (reference passed the literal storage id 1).
     * NOTE: walnut's wm_sdk_sms_read() IGNORES its `storage` argument - the
     * disassembly shows it forwarding only the index to
     * wm_sms_read_message(index, resp, resp_len), i.e. a bare "AT+CMGR=<index>"
     * against whatever AT+CPMS has selected. The named selector below is
     * therefore documentation only; it changes nothing on this kernel. */
    wm_SdkResult read_result = wm_sdk_sms_read(WM_SDK_SMS_STORAGE_SM, (UINT32)index, g_temp_msg_queue);
    if (read_result != WM_SDK_RESULT_SUCCESS) {
        LOG_ERROR("SMS read: wm_sdk_sms_read failed for index %d, result=%d", (int)index, (int)read_result);
        g_sms.stats.receive_errors++;
        LOG_ERROR("ERR_SMS_READ_FAILED"); /* ERRC */
        return FALSE;
    }

    LOG_DEBUG("SMS read: waiting for response (timeout=%u ms)", (unsigned)g_sms.config->urc_timeout_ms);

    /* Walnut: this queue also carries asynchronous incoming messages, so a
     * stray WM_SDK_SMS_EVT_INCOMING can arrive before the read result. Park it and
     * keep waiting instead of failing the read (the reference queue only ever
     * held operation results, so it did a single recv). */
    wm_SdkResult recv_result;
    for (;;) {
        memset(&rsp, 0, sizeof(rsp));
        recv_result = wm_sdk_msgq_recv(g_temp_msg_queue, &rsp, g_sms.config->urc_timeout_ms);
        if (recv_result != WM_SDK_RESULT_SUCCESS) {
            break;
        }
        if (rsp.type == WM_SDK_SMS_EVT_READ_RESULT) {
            break;
        }
        sms_park_incoming(&rsp);
        wm_sdk_sms_msg_free(&rsp);
    }

    if (recv_result != WM_SDK_RESULT_SUCCESS) {
        LOG_ERROR("SMS read: msgq_recv failed for index %d, result=%d (timeout=%u ms)",
                  (int)index, (int)recv_result, (unsigned)g_sms.config->urc_timeout_ms);
        g_sms.stats.receive_errors++;
        LOG_ERROR("ERR_SMS_READ_FAILED"); /* ERRC */
        return FALSE;
    }

    /* reference: if (!rsp.arg3) */
    if (!rsp.text) {
        LOG_ERROR("ERR_SMS_READ_FAILED: SMS read: response received but text is NULL (status=%d)",
                  (int)rsp.status);
        g_sms.stats.receive_errors++;
        return FALSE;
    }

    const char *response_text = (const char *)rsp.text;
    /* Don't use strlen() on SDK-provided data - it might not be null-terminated */
    /* The SDK should provide null-terminated strings, but be safe */
    LOG_DEBUG("SMS read: response received");

    if (strstr(response_text, "+CMGR:") == NULL) {
        LOG_WARN("SMS read: unexpected response type (expected +CMGR:), response: %s", response_text);
        wm_sdk_sms_msg_free(&rsp);   /* reference: wm_sdk_memory_free(rsp.arg3) */
        sms_flush_temp_queue();
        LOG_ERROR("ERR_SMS_READ_FAILED"); /* ERRC */
        g_sms.stats.receive_errors++;
        return FALSE;
    }

    sms_extract_fields(response_text, out->sender_number, out->message_content);
    out->message_index = (int)index;
    out->timestamp = SDK_GET_TICKS();
    out->is_read = TRUE;
    g_sms.stats.total_received++;
    success = TRUE;

    LOG_DEBUG("SMS read: success - sender=%s, content_len=%u",
              out->sender_number, (unsigned)strlen(out->message_content));

    wm_sdk_sms_msg_free(&rsp);       /* reference: wm_sdk_memory_free(rsp.arg3) */

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
        LOG_ERROR("ERR_SMS_UNKNOWN_NUMBER"); /* ERRC */
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

    const char *body = strstr(raw, "");
    if (body)
    {
        body += 2;
        /* Use safe length calculation - find end of body or max length */
        /* Don't use strlen() as SMS data might not be null-terminated */
        size_t len = 0;
        const char *body_end = strstr(body, "");
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
        s_sms_config_last_attempt_ms = 0;
        return;
    }

    if (s_sms_modem_config_applied) {
        return;
    }

    if (g_temp_msg_queue == NULL || !g_sms.config) {
        LOG_WARN("SMS configure: temp queue or config not available");
        return;
    }

    /* Walnut addition: space out retries (see s_sms_config_attempts above) so
     * the SIM's SMS store gets real time to come up instead of the whole budget
     * being spent inside half a second of task cycles. */
    if (s_sms_config_attempts > 0 &&
        utils_monotonic_ms_elapsed(s_sms_config_last_attempt_ms) < SMS_CONFIG_RETRY_INTERVAL_MS) {
        return;
    }

    /* Also attaches g_temp_msg_queue as the incoming-SMS route (walnut:
     * wm_sdk_sms_msgq_poll) - must happen before wm_sdk_sms_init() turns +CMTI on so
     * no arrival is missed. */
    sms_flush_temp_queue();

    s_sms_config_attempts++;
    s_sms_config_last_attempt_ms = utils_monotonic_ms_now();
    LOG_DEBUG("Configuring SMS settings (attempt %u)", (unsigned)s_sms_config_attempts);

    wm_SdkResult ret = -1;
    BOOL essential_ok = TRUE;   /* walnut addition: drives the retry latch below */

    /* WALNUT EXTRA API (no CG counterpart): bring up the vendor SMS task/queue
     * and register the incoming-SMS hook. Idempotent; also applies the SDK
     * defaults (AT+CMGF=1, AT+CSCS="GSM", AT+CNMI=2,1,0,0,0) which the explicit
     * calls below then re-assert with our configured values.
     * NOTE: the prebuilt wm_sdk_sms_init() returns 0 unconditionally (it is
     * wm_sms_init() + wm_sms_set_incoming_cb() with a hardcoded `return 0`), so
     * this check can never fire - kept for API correctness. */
    ret = wm_sdk_sms_init();
    if (ret != WM_SDK_RESULT_SUCCESS) {
        LOG_ERROR("ERR_SMS_CONFIG_FAILED: wm_sdk_sms_init failed: %d", (int)ret); /* ERRC */
        return;   /* retry on the next task cycle */
    }

    /* What the modem defaulted to, before we touch anything (diagnostic). */
    sms_log_store_status("default");

    { /* select the SMS store explicitly
       * WALNUT GAP: neither wm_sdk_sms.h nor wm_sms_init() ever issues AT+CPMS, so
       * the read/write/receive memories stay at the modem default. That matters
       * because the kernel's inbound path is index-based: wm_sms_cmti_cb gets
       * "+CMTI: <mem>,<idx>" and wm_sms_task then does a bare AT+CMGR=<idx>
       * against mem1. If mem1 != the store +CMTI reported, the read FAILS and
       * wm_sms_task drops the message WITHOUT calling the incoming callback -
       * an inbound SMS vanishes with no trace on our side. Pinning all three to
       * "SM" makes +CMTI, AT+CMGR and AT+CMGD agree on SIM storage.
       * UPDATE 2026-09-08: the vendor demo reports `storage SM: 0/10 used` out
       * of the box, so mem1 was already SM and this mismatch is NOT what is
       * breaking inbound SMS here. Kept as a cheap guarantee (and it pins mem3,
       * which AT+CPMS? does not report), but demoted to non-essential. */
        int cpms = wm_sms_set_preferred_storage("SM", "SM", "SM");
        if (cpms == 0) {
            LOG_DEBUG("SMS preferred storage set to SM/SM/SM");
            sms_log_store_status("after CPMS");
        } else {
            /* NOT essential - deliberately does not clear essential_ok. The
             * vendor demo's "SMS: Storage status" shows this modem already
             * defaults to `storage SM: 0/10 used`, so the pin is belt-and-
             * braces; some modems also reject the three-argument CPMS form.
             * Failing it must not trigger the retry ladder. */
            LOG_WARN("SMS preferred storage set failed: %d (keeping modem default)",
                            cpms);
        }
    }

    { /* delete previous SMS in SIM */
        ret = wm_sdk_sms_delete_all(g_temp_msg_queue);
        if (ret == WM_SDK_RESULT_SUCCESS) {
            LOG_DEBUG("SMS: deleted all messages");
        } else if (ret != WM_SDK_RESULT_NOT_SUPPORTED) {
            /* wm_sdk_sms_delete_all() issues "AT+CMGD=0,4" (wm_sms_delete_message
             * with index 0, delflag 4). On-target 2026-09-08 this answers ERROR
             * on the first attempts after the SIM reports READY and then
             * succeeds - the SIM's SMS store is still loading, not an index
             * problem: AT+CPMS? (no index at all) fails in exactly the same
             * window. So the retry in sms_configure() is what actually fixes
             * this; the per-index sweep below is a fallback for a modem/SIM
             * that rejects AT+CMGD=0,4 permanently. */
            LOG_DEBUG("SMS: delete all returned %d, sweeping per-index", (int)ret);
            if (!sms_purge_store()) {
                essential_ok = FALSE;
            }
        } else LOG_ERROR("ERR_SMS_DELETE_FAILED: delete all not supported"); /* ERRC */
    }

    { /* set SMS character format*/
        ret = wm_sdk_sms_set_format((UINT8)g_sms.config->format_mode); // text mode
        if (ret == WM_SDK_RESULT_SUCCESS) {
            LOG_DEBUG("SMS format set to %u", (unsigned)g_sms.config->format_mode);
        } else if (ret != WM_SDK_RESULT_NOT_SUPPORTED) {
            LOG_WARN("SMS format set failed: %d", (int)ret);
            essential_ok = FALSE;
        } else LOG_ERROR("ERR_SMS_CONFIG_FAILED: set format not supported"); /* ERRC */
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
        ret = wm_sdk_sms_set_new_msg_ind(2, 1, 0, 0, 0);
        if (ret == WM_SDK_RESULT_SUCCESS) {
            LOG_DEBUG("SMS new message indication configured (+CMTI)");
        } else if (ret != WM_SDK_RESULT_NOT_SUPPORTED) {
            LOG_WARN("SMS new message indication setup failed: %d", (int)ret);
            essential_ok = FALSE;   /* without +CMTI no SMS is ever received */
        } else LOG_ERROR("ERR_SMS_CONFIG_FAILED: set new msg ind not supported"); /* ERRC */
    }

    { /* set SMS character set protocol: standard 7-bit GSM */
        ret = wm_sdk_sms_set_charset(0);
        if (ret == WM_SDK_RESULT_SUCCESS) {
            LOG_DEBUG("SMS charset set to GSM 7-bit");
        } else if (ret != WM_SDK_RESULT_NOT_SUPPORTED) {
            LOG_WARN("SMS charset set failed: %d", (int)ret);
            essential_ok = FALSE;
        } else LOG_ERROR("ERR_SMS_CONFIG_FAILED: set charset not supported"); /* ERRC */
    }

    /* Reference latched unconditionally here. Walnut retries a bounded number
     * of times so the storage-dependent commands get another chance once the
     * SIM's SMS store has finished loading. */
    if (!essential_ok && s_sms_config_attempts < SMS_CONFIG_MAX_ATTEMPTS) {
        LOG_WARN("SMS modem config incomplete, retrying (%u/%u)",
                        (unsigned)s_sms_config_attempts, (unsigned)SMS_CONFIG_MAX_ATTEMPTS);
        return;
    }

    if (essential_ok) {
        LOG_INFO("SMS modem configured");
        /* Baseline for the inbound test: `used` should be 0 here, and should
         * climb the moment an SMS is delivered to the device. */
        sms_log_store_status("configured");
    } else {
        LOG_ERROR("ERR_SMS_CONFIG_FAILED: giving up after %u attempts - "
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
        wm_sdk_task_delete(g_sms.task_ref);
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
        wm_sdk_msgq_delete(g_temp_msg_queue);
        g_temp_msg_queue = NULL;
    }

    event_manager_broadcast(EVENT_SMS_DISCONNECTED, "SMS Manager", NULL, 0);
    g_sms.module = NULL;
    g_sms.config = NULL;
    memset(&g_sms.stats, 0, sizeof(g_sms.stats));
    LOG_INFO("SMS manager stopped");
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
         * wm_SdkSmsMessage (the SMS API requires msg_size == sizeof(wm_SdkSmsMessage)). */
        g_temp_msg_queue = wm_sdk_msgq_create("sms_tempq", sizeof(wm_SdkSmsMessage),
                                           SMS_MANAGER_QUEUE_SIZE, 0);
        if (g_temp_msg_queue == NULL) {
            LOG_ERROR("SMS temp queue create failed");
            queue_manager_destroy(g_sms.module->config.msg_q, &g_sms.module->config.msg_q_config);
            g_sms.module->config.msg_q = NULL;
            return RESULT_ERROR;
        }
    }

    if (g_sms.module->config.urc_q_config.capacity > 0U &&
        g_sms.module->config.urc_q_config.element_size > 0U) {
        if (queue_manager_create(&g_sms.module->config.urc_q_config,
                                 &g_sms.module->config.urc_q) != RESULT_SUCCESS) {
            LOG_ERROR("SMS URC queue create failed");
            queue_manager_destroy(g_sms.module->config.msg_q, &g_sms.module->config.msg_q_config);
            g_sms.module->config.msg_q = NULL;
            return RESULT_ERROR;
        }
    }

    event_manager_register(EVENT_SIM_AVAILABLE, sms_on_sim_status, NULL, "SMS Manager");
    event_manager_register(EVENT_SIM_UNAVAILABLE, sms_on_sim_status, NULL, "SMS Manager");

    /* reference passed g_sms.task_stack / sizeof(task_stack); walnut lets the
     * kernel allocate the stack (stack_ptr = NULL). */
    g_sms.task_ref = wm_sdk_task_create(sms_send_task,
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
    LOG_INFO("SMS manager ready");
    return RESULT_SUCCESS;
}
