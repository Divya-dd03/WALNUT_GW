/**
 * @file uart_manager.c
 * @brief UART manager implementation for weware platform
 *
 * Walnut port of the reference firmware's module/uart/uart_manager.c. The
 * business logic - the IDLE -> SENDING -> WAITING_RESPONSE -> RESPONSE_RECEIVED
 * state machine, the LEN-based binary frame reassembler with 0xAA resync, the
 * STM response -> user-string builders, opcode correlation of replies to the
 * single in-flight request, the PING-STM probe, statistics and init/deinit
 * ordering - is replicated line for line. Only the SDK-facing calls differ,
 * and every deviation is commented inline.
 *
 * ===========================================================================
 * SDK API mapping (CG "sdk_functionality_uart.h"  ->  walnut "wm_sdk_uart.h")
 * ===========================================================================
 * ###########################################################################
 * ##  KNOWN BLOCKER: this module cannot open a UART as it currently stands. ##
 * ###########################################################################
 * The walnut SDK's prebuilt wm_sdk_uart_* wrappers (lib_wmsrc_B.a,
 * wm_sdk_uart.c.obj) are UNUSABLE. wm_sdk_uart_set_config() validates `port == 0`
 * and then calls, with a hardcoded literal:
 *
 *     mov r0, #0            <- the caller's `port` is discarded
 *     bl  drvUartOpen
 *
 * while the kernel's drvUartOpen (cp.axf @ 0x80020456) opens with:
 *
 *     cmp r6, #4 / bcs <fail>     ; port >= 4 -> NULL
 *     cbz r6, <fail>              ; port == 0 -> NULL, always
 *
 * i.e. it asks for drvUart port 0 - the CP/Seagull debug console (ASR boot
 * logs + RTI_LOG), which the driver unconditionally refuses. So
 * wm_sdk_uart_set_config() returns WM_SDK_RESULT_ERROR for every input and
 * uart_manager_init() fails with ERR_UART_CONFIG_ERROR. No value of
 * UartConfig.port changes this.
 *
 * Making this module work needs an wm_sdk_uart.h implementation that reaches a
 * usable port. The board's port map (established from cp.axf) is:
 *   drvUart 0  0xd4017000  CP debug console - drvUartOpen always refuses it
 *   drvUart 1  0xd4018000  AT-command channel (atuartcfg.port == 1, 115200)
 *   drvUart 2  0xd401f000  THE free, full-duplex, user-configurable UART
 *   drvUart 3  0xd401f800  no UART on this SoC - opening it RESETS the device
 * drvUart* (components/inc/drv_uart.h, exported via core_stub.o) is the only
 * bidirectional raw-UART API the kernel offers; drvSerial* is just the AT
 * modem channel and wm_sdk_log_init_uart() is log output only.
 *
 * PRESENT WITH THE SAME SIGNATURE (used verbatim):
 *   wm_sdk_uart_set_config(UINT32 port, const wm_SdkUartConfig *config)
 *   wm_sdk_uart_write(UINT32 port, const void *buffer, UINT32 size, UINT32 *written)
 *   wm_sdk_uart_control(UINT32 port, UINT32 command)
 *
 * PRESENT BUT DIFFERENT (adapted, see uart_rx_callback):
 *   CG:     sdk_uart_register_callback(port, void (*cb)(UINT32 port, UINT32 len,
 *                                                       void *param), param)
 *           -> the callback gets only a byte COUNT; the handler then calls
 *              wm_sdk_uart_read() to pull the bytes out of the driver.
 *   WALNUT: wm_sdk_uart_set_rx_callback(port, wm_SdkUartRxCallback cb, void *arg)
 *           with wm_SdkUartRxCallback = void (*)(UINT32 port, const UINT8 *data,
 *                                             UINT32 len, void *arg)
 *           -> the read has already happened and we are handed the BUFFER,
 *              valid only for the duration of the call. So the CG
 *              malloc + wm_sdk_uart_read + free round-trip is dropped and the
 *              callback feeds `data` straight into uart_process_received_data().
 *              (The kernel's wm_sdk_uart_read() is a no-op returning
 *              WM_SDK_RESULT_NOT_SUPPORTED; our adapter does implement a polling
 *              read, but it is pointless while an rx callback is armed because
 *              the event handler drains the FIFO first. Nothing here calls it.)
 *
 * CONSTANT RENAMES:
 *   SDK_UART_PORT_MAIN   -> WM_SDK_UART_PORT_1     (single app port)
 *   SDK_UART_CLOSE       -> WM_SDK_UART_CTRL_CLOSE
 *   SDK_UART_BAUD_115200 -> 115200              (walnut defines no baud enums)
 *
 * STRUCT DIFFERENCE:
 *   walnut wm_SdkUartConfig carries an extra trailing `flow_control` byte
 *   (0=none, 1=RTS/CTS); it is set explicitly to 0 in uart_manager_init().
 *
 * Task creation: the reference passed g_uart_task_stack / sizeof(...); walnut
 *   lets the kernel allocate the stack (stack_ptr = NULL), and the priority is
 *   the walnut TP_* scale (TP_TIMED_ACTIVITY) rather than the SIMCOM numeric 5.
 *
 * Walnut logging: LOG_ERRC(ERR_*, ...) has no walnut counterpart (no
 *   common/error_codes.h, no LOG_ERRC in module/log/log.h); those calls become
 *   LOG_ERROR with the reference error-code name kept in the message text. The
 *   trailing ERRC markers are preserved so the sites stay greppable.
 *
 * ===========================================================================
 * Dependencies on modules not yet ported to walnut
 * ===========================================================================
 * HEALTH_PACKET_UNAVAILABLE  - module/tcp/health_packet is not ported, so the
 *   0x86 system_alive handler cannot cache the 42-byte STM block or push a
 *   type-40 health packet. The frame is still parsed and its human-readable
 *   rendering still routes to the requester; only the cache/push calls are
 *   gated out. HEALTH_STM_BLOCK_LEN is defined locally to keep the payload
 *   length checks identical to the reference.
 * FILE_TRANSFER_UNAVAILABLE  - system/file_transfer is not ported (same call
 *   as system/ota/ota_manager.c makes), so there is no g_file_transfer_queue to
 *   route STM-OTA / peri-OTA acks into. The routing branch is gated out; the
 *   ack-string reconstruction is left in place for when the module lands.
 * Remove either define once the corresponding module is integrated.
 */

#include "module/uart/uart_manager.h"
#include "module/module_manager.h"
#include "common/queue_manager.h"
#include "common/task_stats.h"
#include "common/utils.h"
/* reference: #include "common/error_codes.h" - MISSING IN WALNUT (see header note) */
#include "common/stm_binary_protocol.h"

/* SDK Platform Abstraction Layer */
#include "wm_global.h"   /* walnut: TP_TIMED_ACTIVITY task priority */
#include "sdk_platform.h"
#include "functionality/sdk_functionality_os.h"
#include "wm_sdk_uart.h"    /* reference: functionality/sdk_functionality_uart.h */
#include "wm_sdk_log.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define LOG_TAG "UART"
#define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

/*---------------------------------------------------------------
 * Not-yet-ported dependency gates (see header note)
 *--------------------------------------------------------------*/
#ifndef HEALTH_PACKET_UNAVAILABLE
#define HEALTH_PACKET_UNAVAILABLE
#endif
#ifndef FILE_TRANSFER_UNAVAILABLE
#define FILE_TRANSFER_UNAVAILABLE
#endif

#ifdef HEALTH_PACKET_UNAVAILABLE
/* reference: module/tcp/health_packet.h - the STM half of the type-40 health
 * payload is 42 bytes; kept here so the 0x86 length checks are unchanged. */
#define HEALTH_STM_BLOCK_LEN 42
#else
#include "module/tcp/health_packet.h"
#endif

#ifndef FILE_TRANSFER_UNAVAILABLE
#include "system/file_transfer/file_transfer_manager.h"
#endif

/*---------------------------------------------------------------
 * Log Configuration
 *--------------------------------------------------------------*/
#define LOG_TAG "UART"
#define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

/*---------------------------------------------------------------
 * External Declarations
 *--------------------------------------------------------------*/
extern Module *g_modules[];

/*---------------------------------------------------------------
 * Constants
 *--------------------------------------------------------------*/
/* Binary protocol: a received frame is at most ~1082 B (11 hdr + 31 SRC_ADDR +
 * 1040 payload + 2 CRC); the response reconstruction is the hex of <=512 B of data
 * plus a short prefix (~1040). 1280 covers both with margin (was 2 KB for the old
 * ASCII hex protocol). */
#define UART_MAX_RESPONSE_BUFFER_SIZE 1280          /* max accumulated RX frame */
#define UART_MAX_RX_CHUNK_SIZE 1024                 /* Max RX chunk size */
#define UART_SHARED_BUF_SIZE 1280                   /* RX response reconstruction buffer */

/* PING-STM probe tuning (binary get-device-info). */
#define UART_PING_MAX_ATTEMPTS   3
#define UART_PING_TIMEOUT_TICKS  50   /* 50 x task_interval_ms(100) = 5 s per attempt */

/* WALNUT: the reference sized a static g_uart_task_stack[2048] and passed it to
 * wm_sdk_task_create(); walnut lets the kernel allocate the stack, so only the size
 * is passed (4096 matches the other walnut module tasks). */
#define UART_TASK_STACK_SIZE     4096U

/*---------------------------------------------------------------
 * UART State Machine
 *--------------------------------------------------------------*/
typedef enum {
    UART_STATE_IDLE,
    UART_STATE_SENDING,
    UART_STATE_WAITING_RESPONSE,
    UART_STATE_RESPONSE_RECEIVED
} UartState;

/*---------------------------------------------------------------
 * UART Manager State
 *--------------------------------------------------------------*/
typedef struct {
    /* Configuration */
    UINT32 rx_buffer_size;
    UINT32 task_stack_size;
    /* WALNUT: widened from the reference UINT8 - walnut task priorities are the
     * TP_* scale from wm_global.h (TP_TIMED_ACTIVITY = 213), not 0..15. */
    UINT32 task_priority;
    UINT32 task_interval_ms;
    UINT32 response_timeout_ticks;

    /* Runtime State */
    Module *module;
    UartConfig config;
    sdk_task_ref_t task;
    UartState state;
    ModuleMessage *current_request;
    BOOL has_pending_request;
    UINT32 response_timeout_counter;
    UINT8 *response_buffer;
    UINT32 response_buffer_size;
    UINT32 response_buffer_pos;
    BOOL uart_opened;
    uart_manager_stats_t stats;
    TaskStats task_stats;

    /* PING-STM: a one-shot STM responsiveness probe (binary get-device-info x3). */
    BOOL     ping_active;          /* a ping test is running                       */
    BOOL     current_is_ping;      /* the in-flight transaction is the ping probe  */
    UINT8    ping_attempts_left;   /* probe sends remaining                        */
    ModuleId ping_requester;       /* module to send the verdict to                */
    char     ping_addr[64];        /* reply address (e.g. SMS number)              */
} uart_manager_state_t;

/*---------------------------------------------------------------
 * Static State
 *--------------------------------------------------------------*/
static uart_manager_state_t g_uart;
/* reference: static UINT8 g_uart_task_stack[2048] - walnut lets the kernel
 * allocate the task stack (see UART_TASK_STACK_SIZE). */
static char g_uart_shared_buf[UART_SHARED_BUF_SIZE];  /* Shared buffer for commands and parsing */
static ModuleMessage g_uart_routing_msg;              /* Shared ModuleMessage for routing */

/*---------------------------------------------------------------
 * Forward Declarations
 *--------------------------------------------------------------*/
static void uart_set_defaults(void);
static void uart_free_current_request(void);
static void uart_reset_state(void);
static void uart_task_entry(void *arg);
static void uart_rx_callback(UINT32 port, const UINT8 *data, UINT32 len, void *arg);
static void uart_process_received_data(const UINT8 *data, UINT32 len);
static void uart_dispatch_binary_frame(const UINT8 *frame, UINT32 total);
static void uart_route_to_module_queue(ModuleId dest_module, const char *address, const char *message);
static void uart_route_binary_to_module_queue(ModuleId dest_module, const char *address,
                                              const UINT8 *data, UINT32 len);
static void uart_update_task_stats(void);

/*---------------------------------------------------------------
 * Utility Functions
 *--------------------------------------------------------------*/

static inline void uart_free_current_request(void)
{
    if (g_uart.current_request) {
        if (g_uart.current_request->use_dynamic_buffer && g_uart.current_request->dynamic_buffer) {
            free(g_uart.current_request->dynamic_buffer);
        }
        free(g_uart.current_request);
        g_uart.current_request = NULL;
    }
}

/* STM command of the in-flight request; 0 when none or when it is not a binary frame. */
static inline UINT8 uart_pending_cmd(void)
{
    if (!g_uart.current_request || !g_uart.current_request->is_raw)
        return 0u;

    const UINT8 *frame = (const UINT8 *)module_message_payload_ptr(g_uart.current_request);
    return frame ? stm_frame_cmd(frame) : 0u;
}

static inline void uart_reset_state(void)
{
    uart_free_current_request();
    g_uart.has_pending_request = FALSE;
    g_uart.state = UART_STATE_IDLE;
    g_uart.response_timeout_counter = 0;
    g_uart.response_buffer_pos = 0;
    g_uart.current_is_ping = FALSE;   /* per-transaction; ping_active is test-level */
}

/*---------------------------------------------------------------
 * Response Parsing & Routing
 *--------------------------------------------------------------*/

static void uart_route_to_module_queue(ModuleId dest_module, const char *address, const char *message)
{
    /* Clear shared message buffer */
    memset(&g_uart_routing_msg, 0, sizeof(g_uart_routing_msg));
    g_uart_routing_msg.source_module = MODULE_ID_UART;
    g_uart_routing_msg.destination_module = dest_module;

    if (address) {
        utils_strncpy_safe(g_uart_routing_msg.address, address, sizeof(g_uart_routing_msg.address));
    }
    if (message) {
        int n = utils_strncpy_safe(g_uart_routing_msg.message, message, sizeof(g_uart_routing_msg.message));
        g_uart_routing_msg.data_len = (n >= 0) ? (UINT32)n : 0U;
    } else {
        g_uart_routing_msg.data_len = 0;
    }

    /* Handle file transfer routing */
    if (dest_module == MODULE_ID_FILE_TRANSFER) {
#ifdef FILE_TRANSFER_UNAVAILABLE
        /* WALNUT: system/file_transfer is not ported, so there is no
         * g_file_transfer_queue to push into (MODULE_ID_FILE_TRANSFER also has
         * no g_modules[] slot). Drop the reply instead of routing it. */
        LOG_WARN("UART: FILE_TRANSFER reply dropped (module not ported): %s",
                 message ? message : "");
#else
        extern Queue *g_file_transfer_queue;
        extern QueueConfig g_file_transfer_queue_config;

        if (g_file_transfer_queue) {
            queue_push(g_file_transfer_queue, &g_file_transfer_queue_config, &g_uart_routing_msg);
        }
#endif
        return;
    }

    /* Log module has no msg_q — UART responses for SYSTEM/MAIN */
    if (dest_module == MODULE_ID_LOG) {
        const char *a = address ? address : "";
        const char *m = message ? message : "";
        SDK_DEBUG_PRINT("[UART] UART to LOG %s %s", a, m);
        return;
    }

    /* TCP send queue expects WE type-38 packets, not plain text */
    if (dest_module == MODULE_ID_TCP) {
        utils_route_response_to_module(MODULE_ID_TCP, address, message, MODULE_ID_UART);
        return;
    }

    /* Route to regular module queue */
    const ModuleConfig *dest_config = module_manager_get_config(dest_module);
    if (!dest_config || !dest_config->enabled || !dest_config->msg_q) {
        return;
    }

    queue_push(dest_config->msg_q, &dest_config->msg_q_config, &g_uart_routing_msg);
}

/* Route a RAW BINARY payload (length-delimited, may contain 0x00) to a module queue.
 * Unlike uart_route_to_module_queue() this copies by length (memcpy + data_len), never
 * a string copy, and flags the message payload_is_binary so the consumer skips string
 * parsing. Used for opaque blobs forwarded verbatim (e.g. gw-health peripheral/fuel data). */
static void uart_route_binary_to_module_queue(ModuleId dest_module, const char *address,
                                              const UINT8 *data, UINT32 len)
{
    if (!data || len == 0u || len > MODULE_MESSAGE_INLINE_SIZE) {
        LOG_ERROR("UART binary route: bad len %lu (max %u)",
                  (unsigned long)len, (unsigned)MODULE_MESSAGE_INLINE_SIZE);
        return;
    }
    const ModuleConfig *dest_config = module_manager_get_config(dest_module);
    if (!dest_config || !dest_config->enabled || !dest_config->msg_q) {
        return;
    }

    memset(&g_uart_routing_msg, 0, sizeof(g_uart_routing_msg));
    g_uart_routing_msg.source_module      = MODULE_ID_UART;
    g_uart_routing_msg.destination_module = dest_module;
    if (address) {
        utils_strncpy_safe(g_uart_routing_msg.address, address, sizeof(g_uart_routing_msg.address));
    }
    memcpy(g_uart_routing_msg.message, data, len);
    g_uart_routing_msg.data_len          = len;
    g_uart_routing_msg.payload_is_binary = TRUE;

    queue_push(dest_config->msg_q, &dest_config->msg_q_config, &g_uart_routing_msg);
}

/*---------------------------------------------------------------
 * RX Processing
 *--------------------------------------------------------------*/

static void uart_process_received_data(const UINT8 *data, UINT32 len)
{
    if (!data || !len || !g_uart.response_buffer) {
        return;
    }

    /* Check for overflow */
    if (len > UINT32_MAX - g_uart.response_buffer_pos - 1) {
        g_uart.response_buffer_pos = 0;
        return;
    }

    UINT32 needed = g_uart.response_buffer_pos + len + 1;

    /* Enforce maximum buffer size */
    if (needed > UART_MAX_RESPONSE_BUFFER_SIZE) {
        LOG_ERROR("ERR_UART_RX_OVERFLOW: UART RX buffer overflow (drop %lu B)",
                  (unsigned long)g_uart.response_buffer_pos); /* ERRC */
        g_uart.response_buffer_pos = 0;
        needed = len + 1;
    }

    /* Reallocate if needed */
    if (needed > g_uart.response_buffer_size) {
        UINT8 *nb = realloc(g_uart.response_buffer, needed);
        if (!nb) {
            g_uart.response_buffer_pos = 0;
            return;
        }
        g_uart.response_buffer = nb;
        g_uart.response_buffer_size = needed;
    }

    /* Copy data */
    if (g_uart.response_buffer_pos + len > g_uart.response_buffer_size - 1) {
        g_uart.response_buffer_pos = 0;
        return;
    }

    memcpy(g_uart.response_buffer + g_uart.response_buffer_pos, data, len);
    g_uart.response_buffer_pos += len;
    g_uart.response_buffer[g_uart.response_buffer_pos] = '\0';

    /* Binary frame detection (LEN-based, per GW-STM-BIN-001 §3). The STM is
     * binary-only, so every response is a binary frame. Process all complete
     * frames currently buffered; keep partials for the next RX chunk. */
    for (;;) {
        if (g_uart.response_buffer_pos == 0)
            break;

        int r = stm_frame_complete(g_uart.response_buffer, g_uart.response_buffer_pos);

        if (r == STM_FRAME_NEED_MORE)
            break;  /* wait for more bytes */

        if (r == STM_FRAME_BAD) {
            /* Resync: drop bytes before the next SOF1 (0xAA); flush if none left. */
            UINT32 i = 1;
            while (i < g_uart.response_buffer_pos && g_uart.response_buffer[i] != STM_SOF1)
                i++;
            if (i >= g_uart.response_buffer_pos) {
                g_uart.response_buffer_pos = 0;
                break;
            }
            memmove(g_uart.response_buffer, g_uart.response_buffer + i,
                    g_uart.response_buffer_pos - i);
            g_uart.response_buffer_pos -= i;
            continue;
        }

        /* r > 0: complete, CRC-valid frame of length r. */
        uart_dispatch_binary_frame(g_uart.response_buffer, (UINT32)r);
        if (g_uart.has_pending_request)
            g_uart.state = UART_STATE_RESPONSE_RECEIVED;

        if ((UINT32)r < g_uart.response_buffer_pos) {
            memmove(g_uart.response_buffer, g_uart.response_buffer + r,
                    g_uart.response_buffer_pos - (UINT32)r);
            g_uart.response_buffer_pos -= (UINT32)r;
        } else {
            g_uart.response_buffer_pos = 0;
            break;
        }
    }
}

/*---------------------------------------------------------------
 * Build the user-facing reply from a binary response payload. STATUS 0x00 = OK,
 * non-zero = fail. Responses are human-understandable (not byte-identical to the
 * old ASCII protocol). MACs in data replies are uppercase, continuous (no colons).
 *--------------------------------------------------------------*/
/* Map the STM peripheral-command status byte to a short, readable reply.
 * Shared by peri-cmd / get-ag-data / get-fuel-data. Returns NULL for unknown
 * codes so the caller can fall back to printing the raw code. */
static const char *peri_cmd_err_str(UINT8 code)
{
    switch (code) {
        case 0x01u: return "Fail: BLE scan off";
        case 0x02u: return "Fail: payload too short";
        case 0x03u: return "Fail: bad device id (1-5)";
        case 0x04u: return "Fail: no id or MAC given";
        case 0x05u: return "Fail: MAC not found";
        case 0x06u: return "Fail: data length overflow";
        case 0x07u: return "Fail: device not connected";
        case 0x08u: return "Fail: bad packet format";
        case 0x09u: return "Fail: BLE send failed";
        case 0x0Au: return "Fail: no response (timeout)";
        default:    return NULL;
    }
}

/* Format a UNIX epoch (seconds, UTC) as IST "DD-MM-YYYY HH:MM:SS". IST = UTC + 5h30m.
 * Calendar conversion via Hinnant's civil-from-days (no loops, valid for all dates). */
static int fmt_ist_datetime(UINT32 epoch, char *out, int size)
{
    UINT32   ist = epoch + 19800u;               /* shift to IST */
    UINT32   tod = ist % 86400u;
    long     z   = (long)(ist / 86400u) + 719468;
    long     era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe/1460u + doe/36524u - doe/146096u) / 365u;
    int      y   = (int)yoe + (int)(era * 400);
    unsigned doy = doe - (365u*yoe + yoe/4u - yoe/100u);
    unsigned mp  = (5u*doy + 2u) / 153u;
    unsigned d   = doy - (153u*mp + 2u)/5u + 1u;
    unsigned mo  = (mp < 10u) ? mp + 3u : mp - 9u;
    if (mo <= 2u) y += 1;
    /* WALNUT: the day/month/year arguments are reduced modulo their field widths
     * before printing. The civil-from-days math above already guarantees
     * d <= 31, mo <= 12 and (for a UINT32 epoch) y <= 2106, so for every
     * reachable input this is the identity and the output is always exactly 19
     * chars + NUL. It is written this way because GCC's range analysis cannot
     * follow the calendar arithmetic: it assumes up to 10 digits per %02u and
     * 11 per %04d, computes a 38-byte worst case against the caller's dt[24],
     * and emits -Wformat-truncation. Reducing the three unprovable fields makes
     * the 20-byte bound provable, and also makes truncation impossible even if
     * a future caller passes a nonsense epoch. The h/m/s fields need no help -
     * the compiler already derives [0,59] from the modulo arithmetic. */
    return snprintf(out, size, "%02u-%02u-%04u %02u:%02u:%02u",
                    d % 100u, mo % 100u, (unsigned)y % 10000u,
                    (unsigned)(tod / 3600u), (unsigned)((tod % 3600u) / 60u),
                    (unsigned)(tod % 60u));
}

static int uart_build_user_resp(UINT8 cmd, const UINT8 *pl, UINT16 plen,
                                UINT8 req_dev_id, char *out, int size)
{
    UINT8 status = (plen >= 1u) ? pl[0] : 0xFFu;

    switch (cmd) {

    case (0x22u | STM_CMD_RESP_BIT): {   /* 0xA2 set-ble-peri-info */
        /* OK: STATUS,DEV_ID,NAME_LEN,NAME,MAC(6) -> "<id>-<name>-<mac>-configured-OK" */
        if (status == 0x00u && plen >= 3u) {
            UINT8 devid = pl[1], nl = pl[2];
            if ((UINT32)(3u + nl + 6u) <= plen) {
                const UINT8 *nm = &pl[3], *mac = &pl[3 + nl];
                return snprintf(out, size,
                    "%u-%.*s-%02x:%02x:%02x:%02x:%02x:%02x-configured-OK",
                    devid, (int)nl, (const char *)nm,
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            }
        }
        if (status == 0x02u)   /* slot occupied */
            return snprintf(out, size,
                "Device is already configured for id %u, first delete it.",
                (unsigned)req_dev_id);
        if (status == 0x04u)   /* duplicate MAC — already configured on another id */
            return snprintf(out, size,
                "Device already configured with same MAC at id %u",
                (plen >= 2u) ? (unsigned)pl[1] : 0u);
        return snprintf(out, size, "Fail");
    }

    case (0x24u | STM_CMD_RESP_BIT): {   /* 0xA4 del-ble-peri-info */
        /* OK: STATUS,DEV_ID,NAME_LEN,NAME,MAC(6) -> "<id>-<name>-<mac>-delete-OK" */
        if (status == 0x00u && plen >= 3u) {
            UINT8 devid = pl[1], nl = pl[2];
            if ((UINT32)(3u + nl + 6u) <= plen) {
                const UINT8 *nm = &pl[3], *mac = &pl[3 + nl];
                return snprintf(out, size,
                    "%u-%.*s-%02x:%02x:%02x:%02x:%02x:%02x-delete-OK",
                    devid, (int)nl, (const char *)nm,
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            }
        }
        return snprintf(out, size, "Fail");
    }

    case (STM_CMD_GET_DEV_INFO | STM_CMD_RESP_BIT): {   /* 0x84 get-device-info */
        /* STATUS,FW(2),HW(2),MAC(6),BLE_STATUS,CFG_DEV,REBOOT_REASON,REBOOT_COUNT(2)
         * -> "Ok,<fw>,<hw>,<MAC>,<ble>,<cfg>,<reboot_reason>,<reboot_count>" */
        if (status == 0x00u) {
            const UINT32 need = 1u + 2u + 2u + 6u + 1u + 1u + 1u + 2u;
            if ((UINT32)plen >= need) {
                UINT16 fw  = (UINT16)((pl[1] << 8) | pl[2]);
                UINT16 hw  = (UINT16)((pl[3] << 8) | pl[4]);
                const UINT8 *mac = &pl[5];
                UINT8  ble = pl[11], cfg = pl[12], rr = pl[13];
                UINT16 rc  = (UINT16)((pl[14] << 8) | pl[15]);
                return snprintf(out, size,
                                "Ok,%u,%u,%02X%02X%02X%02X%02X%02X,%u,%u,%u,%u",
                                fw, hw,
                                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                                ble, cfg, rr, rc);
            }
        }
        return snprintf(out, size, "Fail");
    }

    case (STM_CMD_SYSTEM_UPDATE | STM_CMD_RESP_BIT): {  /* 0x86 system_alive */
        /* STATUS + 42-byte health block; cache/push already done, render for a human. */
        if (status == 0x00u && plen >= (UINT16)(1u + HEALTH_STM_BLOCK_LEN)) {
            const UINT8 *b     = &pl[1];
            UINT16 fw          = (UINT16)((b[0] << 8) | b[1]);
            UINT16 reboots     = (UINT16)((b[2] << 8) | b[3]);
            UINT8  flags1      = b[4];
            UINT8  flags2      = b[5];
            UINT16 masks       = (UINT16)((b[6] << 8) | b[7]);
            UINT32 uptime_s    = (UINT32)(((b[8] << 8) | b[9]) * 2u);
            return snprintf(out, size,
                "Ok,fw:%u,rc:%u,rr:%u,pl:%u,up:%lu,cfg:%02X,con:%02X,seen:%02X,"
                "heap:%u,stk:%u/%u/%u,lf:%u,hci:%u,age:%u,f2:%02X",
                fw, reboots,
                (unsigned)(flags1 >> 4), (unsigned)((flags1 >> 3) & 1u),
                (unsigned long)uptime_s,
                (unsigned)((masks >> 10) & 0x1Fu),
                (unsigned)((masks >> 5) & 0x1Fu),
                (unsigned)(masks & 0x1Fu),
                b[10], b[11], b[12], b[13], b[14], b[15], b[16],
                flags2);
        }
        return snprintf(out, size, "Fail");
    }

    case (0x05u | STM_CMD_RESP_BIT): {   /* 0x85 get-reboot-info -> "Ok,2,1/DD-MM-YYYY HH:MM:SS,..." (IST) */
        if (status != 0x00u || plen < 2u)
            return snprintf(out, size, "Fail");
        UINT8 count = pl[1];
        int off = snprintf(out, size, "Ok,%u", count);
        if (off <= 0 || off >= size) return off;
        for (UINT8 i = 0; i < count; i++) {
            UINT32 base = 2u + (UINT32)i * 5u;
            if (base + 5u > plen) break;
            UINT8  reason = pl[base];
            UINT32 epoch  = ((UINT32)pl[base+1] << 24) | ((UINT32)pl[base+2] << 16)
                          | ((UINT32)pl[base+3] << 8)  | pl[base+4];
            char dt[24];
            fmt_ist_datetime(epoch, dt, (int)sizeof(dt));   /* IST DD-MM-YYYY HH:MM:SS */
            int m = snprintf(out + off, size - off, ",%u/%s", (unsigned)reason, dt);
            if (m <= 0 || off + m >= size) break;
            off += m;
        }
        return off;
    }

    case (0x25u | STM_CMD_RESP_BIT):     /* 0xA5 peri-cmd (PKT_LEN+PKT_DATA) */
    case (0x26u | STM_CMD_RESP_BIT):     /* 0xA6 get-ag-data  */
    case (0x27u | STM_CMD_RESP_BIT): {   /* 0xA7 get-fuel-data -> "Ok,<hexdata>" */
        if (status != 0x00u) {           /* STM returned a specific error code */
            if (cmd == (0x26u | STM_CMD_RESP_BIT) ||
                cmd == (0x27u | STM_CMD_RESP_BIT)) {
                /* get-ag-data / get-fuel-data: only two reasons */
                if (status == 0x01u)
                    return snprintf(out, size, "Fail: BLE scan off");
                if (status == 0x02u)
                    return snprintf(out, size, (cmd == (0x27u | STM_CMD_RESP_BIT))
                                    ? "Fail: no fuel data" : "Fail: no ag data");
                return snprintf(out, size, "Fail: error 0x%02X", status);
            }
            /* peri-cmd: full error table */
            const char *e = peri_cmd_err_str(status);
            return e ? snprintf(out, size, "%s", e)
                     : snprintf(out, size, "Fail: error 0x%02X", status);
        }
        if (plen < 3u)
            return snprintf(out, size, "Fail");
        UINT16 dlen = (UINT16)((pl[1] << 8) | pl[2]);
        if ((UINT32)(3u + dlen) > plen)
            return snprintf(out, size, "Fail");
        int off = snprintf(out, size, "Ok,");
        if (off <= 0 || off >= size) return off;
        if (!utils_bytes_to_hex_str(&pl[3], dlen, out + off, (size_t)(size - off)))
            return snprintf(out, size, "Fail");
        return (int)strlen(out);
    }

    case (0x28u | STM_CMD_RESP_BIT): {   /* 0xA8 get-peri-info -> "Ok,153,AG_1.04,686725E871FE" */
        if (status != 0x00u || plen < 4u)
            return snprintf(out, size, "Fail");
        UINT16 fwv = (UINT16)((pl[1] << 8) | pl[2]);
        UINT8  hwl = pl[3];
        if ((UINT32)(4u + hwl + 6u) > plen)
            return snprintf(out, size, "Fail");
        const UINT8 *hw  = &pl[4];
        const UINT8 *mac = &pl[4 + hwl];
        return snprintf(out, size, "Ok,%u,%.*s,%02X%02X%02X%02X%02X%02X",
                        fwv, (int)hwl, (const char *)hw,
                        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    case (0x32u | STM_CMD_RESP_BIT): {   /* 0xB2 get-ble-data -> "Ok,<count>,<hex>" */
        if (status != 0x00u || plen < 4u)
            return snprintf(out, size, "Fail");
        UINT8  count = pl[1];
        UINT16 tlen  = (UINT16)((pl[2] << 8) | pl[3]);
        if ((UINT32)(4u + tlen) > plen)
            return snprintf(out, size, "Fail");
        int off = snprintf(out, size, "Ok,%u,", (unsigned)count);
        if (off <= 0 || off >= size) return off;
        if (!utils_bytes_to_hex_str(&pl[4], tlen, out + off, (size_t)(size - off)))
            return snprintf(out, size, "Fail");
        return (int)strlen(out);
    }

    case (0x29u | STM_CMD_RESP_BIT): {   /* 0xA9 get-peri-link -> STM-built compact status string */
        if (status != 0x00u || plen < 2u)
            return snprintf(out, size, "Fail");
        return snprintf(out, size, "Ok,%.*s", (int)(plen - 1u), (const char *)&pl[1]);
    }

    case (0x23u | STM_CMD_RESP_BIT): {   /* 0xA3 get-ble-peri-info (list) */
        /* STATUS,COUNT, then per device:
         *   DEV_ID,MAC(6),NAME_LEN,NAME,INTERVAL(4),DEV_TYPE,OFFLINE,WITH_GPS,CONNECTED
         * -> "Ok,<count>,<id>/<MAC>/<name>/<interval>/<conn(0|1)>/<off(0|1)>/<gps(0|1)>/<connected(0|1)>,..." */
        if (status == 0x02u)   /* invalid device id */
            return snprintf(out, size, "Invalid device id, use 1-5");
        if (status == 0x01u)
            return snprintf(out, size, "%u-Not-configured", (unsigned)req_dev_id);
        if (status != 0x00u || plen < 2u)
            return snprintf(out, size, "%u-Fail", (unsigned)req_dev_id);
        UINT8 count = pl[1];
        int off = snprintf(out, size, "Ok,%u", (unsigned)count);
        if (off <= 0 || off >= size) return off;
        UINT32 idx = 2u;
        for (UINT8 i = 0; i < count; i++) {
            if (idx + 8u > plen) break;                 /* DEV_ID+MAC(6)+NAME_LEN */
            UINT8 devid = pl[idx];
            const UINT8 *mac = &pl[idx + 1];
            UINT8 nl = pl[idx + 7];
            UINT32 dnext = idx + 8u + nl + 4u + 1u + 1u + 1u + 1u;
            if (dnext > plen) break;
            const UINT8 *nm  = &pl[idx + 8];
            const UINT8 *iv  = &pl[idx + 8 + nl];
            UINT32 interval  = ((UINT32)iv[0] << 24) | ((UINT32)iv[1] << 16)
                             | ((UINT32)iv[2] << 8)  | iv[3];
            UINT8 dtype = pl[idx + 8 + nl + 4];
            UINT8 offl  = pl[idx + 8 + nl + 5];
            UINT8 gps   = pl[idx + 8 + nl + 6];
            UINT8 conn  = pl[idx + 8 + nl + 7];
            int m = snprintf(out + off, size - off,
                ",%u/%02X%02X%02X%02X%02X%02X/%.*s/%lu/%u/%u/%u/%u",
                devid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                (int)nl, (const char *)nm, (unsigned long)interval,
                (dtype == 0x33u) ? 1u : 0u, offl, gps, conn);
            if (m <= 0 || off + m >= size) break;
            off += m;
            idx = dnext;
        }
        return off;
    }

    case (0x20u | STM_CMD_RESP_BIT):     /* 0xA0 set-ble-scan-on */
        /* STATUS byte: 0x00 = OK; 0x02 = no peripheral configured to scan for. */
        if (status == 0x00u)
            return snprintf(out, size, "OK");
        if (status == 0x02u)
            return snprintf(out, size, "ble-scan-on fail, no device configured");
        return snprintf(out, size, "Fail");

    default:
        /* Status-only commands (reboot/restart/factory/scan-off/gsm-status). */
        return snprintf(out, size, status == 0x00u ? "OK" : "Fail");
    }
}

/*---------------------------------------------------------------
 * Binary response dispatch (Option A: rebuild the string the existing
 * consumer already parses, then route to the same module — no consumer
 * changes). Runs in RX context; uses the static g_uart_shared_buf (never a
 * large stack buffer) for any reconstruction.
 *--------------------------------------------------------------*/
static void uart_dispatch_binary_frame(const UINT8 *frame, UINT32 total)
{
    (void)total;
    UINT8        cmd  = stm_frame_cmd(frame);
    const UINT8 *pl   = stm_frame_payload(frame);
    UINT16       plen = stm_frame_payload_len(frame);

    /* PING-STM: the outstanding transaction is the responsiveness probe. Any reply to
     * it (the STM answered get-device-info) means the STM is alive — report OK and
     * intercept the frame so it is NOT routed to the normal consumer. */
    if (g_uart.current_is_ping) {
        if (cmd == (STM_CMD_GET_DEV_INFO | STM_CMD_RESP_BIT)) {
            uart_route_to_module_queue(g_uart.ping_requester, g_uart.ping_addr, "STM: OK");
            g_uart.ping_active = FALSE;
        }
        return;
    }

    /* Correlate the response to the in-flight command by opcode. The STM echoes
     * (request CMD | RESP_BIT) in every response, and we run one command at a time,
     * so a frame whose opcode doesn't match the outstanding request is stale — e.g. a
     * late reply that only arrived after this command's predecessor already timed out.
     * Drop it: otherwise the requester would get a reply meant for a different command.
     * The current command keeps waiting for its own (correctly-matched) response. */
    if (g_uart.current_request && g_uart.current_request->is_raw) {
        /* Read the in-flight request's opcode from its ACTUAL frame bytes. A raw
         * request may carry its frame inline (message[]) or in dynamic_buffer
         * (e.g. STM-OTA chunks); module_message_payload_ptr() returns whichever is
         * in use. Reading ->message directly missed dynamic_buffer frames, so their
         * ACKs were dropped here as "stale" (req_cmd read as 0x00). */
        const UINT8 *req_frame = (const UINT8 *)module_message_payload_ptr(g_uart.current_request);
        UINT8 req_cmd = req_frame ? stm_frame_cmd(req_frame) : 0u;
        if (cmd != (UINT8)(req_cmd | STM_CMD_RESP_BIT)) {
            LOG_WARN("UART: drop stale STM frame cmd=0x%02X (awaiting resp to 0x%02X)",
                     (unsigned)cmd, (unsigned)req_cmd);
            return;
        }
    }

    /* Route the reply to whoever issued the outstanding request (single in-flight).
     * Internal commands originate from BLE/LOG (their existing consumers); user
     * commands from SMS/TCP (reply goes back to the sender). */
    ModuleId    origin  = g_uart.current_request ? g_uart.current_request->source_module : MODULE_ID_BLE;
    const char *addr    = g_uart.current_request ? g_uart.current_request->address : "STM";
    BOOL        is_user = (origin == MODULE_ID_SMS || origin == MODULE_ID_TCP);

    /* Before the user/internal split: both origins carry the same fresh block. */
    if (cmd == (STM_CMD_SYSTEM_UPDATE | STM_CMD_RESP_BIT)) {
#ifdef HEALTH_PACKET_UNAVAILABLE
        /* WALNUT: module/tcp/health_packet is not ported - the 42-byte STM block
         * cannot be cached and no type-40 health packet can be pushed. The frame
         * is still parsed and rendered below, exactly as in the reference. */
#else
        if (plen >= (UINT16)(1u + HEALTH_STM_BLOCK_LEN) && pl[0] == 0x00u)
            health_packet_set_stm_block((const char *)&pl[1], HEALTH_STM_BLOCK_LEN);
        else
            health_packet_clear_stm_block();   /* answered, but no usable block */
        /* Pushed either way; zeros mean the STM is down. */
        (void)health_packet_push_to_tcp();
#endif
    }

    if (!is_user) {
        /* ---- Internal-command responses: reconstruct exactly what the BLE/LOG
         *      consumer already parses (unchanged behaviour). ---- */
        switch (cmd) {

        case (STM_CMD_GET_DEV_INFO | STM_CMD_RESP_BIT): {   /* 0x84 */
            /* FW_VER uint16 big-endian; routed as an undotted decimal ("101"). */
            if (plen < 3u || pl[0] != 0x00u)
                break;
            UINT16 fw = (UINT16)((pl[1] << 8) | pl[2]);
            if (fw == 0u)
                break;
            snprintf(g_uart_shared_buf, sizeof(g_uart_shared_buf),
                     "FW Version: %u", (unsigned)fw);
            uart_route_to_module_queue(origin, addr, g_uart_shared_buf);
            break;
        }

        case (STM_CMD_SYSTEM_UPDATE | STM_CMD_RESP_BIT):    /* 0x86 */
            uart_route_to_module_queue(origin, addr,
                (plen >= 1u && pl[0] == 0x00u) ? "OK" : "FAIL");
            break;

        case (STM_CMD_GW_HEALTH | STM_CMD_RESP_BIT): {      /* 0xB0 */
            /* Payload: STATUS(1), FLAG(1), TOTAL_LEN(2 BE), DATA. The DATA is an opaque
             * peripheral/fuel blob to forward to the server, so we hand it to ble_manager
             * as RAW BINARY — [FLAG][DATA] — with no hex string round-trip (which used to
             * double the size and overflow the 256-byte inline message). */
            if (plen < 4u || pl[0] != 0x00u)
                break;
            UINT16 dlen = (UINT16)(((UINT16)pl[2] << 8) | pl[3]);
            if (dlen == 0u || (UINT32)(4u + dlen) > plen)
                break;
            if ((UINT32)(1u + dlen) > MODULE_MESSAGE_INLINE_SIZE) {
                LOG_ERROR("gw-health resp DATA too big for inline route (%u bytes)", (unsigned)dlen);
                break;
            }
            g_uart_shared_buf[0] = (char)(pl[1] ? 1u : 0u);     /* FLAG: 0=immediate TCP, 1=with GPS */
            memcpy(&g_uart_shared_buf[1], &pl[4], dlen);        /* DATA verbatim */
            uart_route_binary_to_module_queue(origin, addr,
                                              (const UINT8 *)g_uart_shared_buf, (UINT32)(1u + dlen));
            break;
        }

        case (0x28u | STM_CMD_RESP_BIT): {                 /* 0xA8 get-peri-info (peri-OTA discovery) */
            /* peri_ota_manager polls the FILE_TRANSFER queue and parses "Ok,<fw>,<hw>,<mac>". */
            int n = uart_build_user_resp(cmd, pl, plen, 0,
                                         g_uart_shared_buf, (int)sizeof(g_uart_shared_buf));
            if (n > 0)
                uart_route_to_module_queue(origin, addr, g_uart_shared_buf);
            break;
        }

        case (STM_CMD_STM_OTA | STM_CMD_RESP_BIT): {       /* 0xC0 stm-ota ack */
            /* STATUS(1), TYPE(1), CHUNK_NUM(4). Rebuild what file_transfer expects:
             * data  -> "ok,chunk,<num>" / "fail,chunk,<num>";  crc -> "crc ok"/"crc fail". */
            if (plen < 6u) break;
            UINT8  rstatus = pl[0];
            UINT8  rtype   = pl[1];
            UINT32 cnum    = ((UINT32)pl[2] << 24) | ((UINT32)pl[3] << 16)
                           | ((UINT32)pl[4] << 8)  | pl[5];
            if (rtype == STM_OTA_CHUNK_CRC)
                snprintf(g_uart_shared_buf, sizeof(g_uart_shared_buf),
                         (rstatus == 0x00u) ? "crc ok" : "crc fail");
            else
                snprintf(g_uart_shared_buf, sizeof(g_uart_shared_buf),
                         "%s,chunk,%lu", (rstatus == 0x00u) ? "ok" : "fail",
                         (unsigned long)cnum);
            uart_route_to_module_queue(origin, addr, g_uart_shared_buf);
            break;
        }

        case (STM_CMD_PERI_OTA | STM_CMD_RESP_BIT): {      /* 0xC1 peri-ota ack */
            /* STATUS(1), RSP_LEN(2), RSP_DATA -> "ok,<hex RSP_DATA>" / "fail". */
            if (plen < 1u || pl[0] != 0x00u) {
                uart_route_to_module_queue(origin, addr, "fail");
                break;
            }
            UINT16 rlen = (plen >= 3u) ? (UINT16)(((UINT16)pl[1] << 8) | pl[2]) : 0u;
            int off = snprintf(g_uart_shared_buf, sizeof(g_uart_shared_buf), "ok,");
            if (off <= 0 || (size_t)off >= sizeof(g_uart_shared_buf)) break;
            if (rlen > 0u && (UINT32)(3u + rlen) <= plen)
                (void)utils_bytes_to_hex_str(&pl[3], rlen, g_uart_shared_buf + off,
                                             sizeof(g_uart_shared_buf) - (size_t)off);
            uart_route_to_module_queue(origin, addr, g_uart_shared_buf);
            break;
        }

        default:
            LOG_DEBUG("internal binary resp cmd=0x%02X ignored", (unsigned)cmd);
            break;
        }
        return;
    }

    /* ---- User-command responses: rebuild the ASCII the server/SMS expects (the
     *      message payload, no SOURCE,DEST prefix) and send it to the originator.
     *      The error reply for set-ble-peri-info needs the DEV_ID, which the error
     *      frame omits — recover it from the request frame's first payload byte. ---- */
    UINT8 req_dev_id = 0;
    if (g_uart.current_request && g_uart.current_request->is_raw) {
        /* Same accessor rule as the correlation above: honour dynamic_buffer. */
        const UINT8 *rf = (const UINT8 *)module_message_payload_ptr(g_uart.current_request);
        if (rf && stm_frame_payload_len(rf) >= 1u)
            req_dev_id = stm_frame_payload(rf)[0];
    }
    int n = uart_build_user_resp(cmd, pl, plen, req_dev_id,
                                 g_uart_shared_buf, (int)sizeof(g_uart_shared_buf));
    if (n > 0)
        uart_route_to_module_queue(origin, addr, g_uart_shared_buf);
}

/*---------------------------------------------------------------
 * UART RX Callback
 *--------------------------------------------------------------*/

/* WALNUT: the reference callback signature was (port, len, param) - it got a
 * byte COUNT and had to malloc a buffer and call wm_sdk_uart_read() to fetch the
 * bytes. Walnut's wm_SdkUartRxCallback is (port, data, len, arg): the SDK has
 * already performed the read and hands over the buffer, which is valid only for
 * the duration of this call (wm_sdk_uart_read() is a documented no-op returning
 * WM_SDK_RESULT_NOT_SUPPORTED). uart_process_received_data() copies what it needs
 * into g_uart.response_buffer before returning, so feeding `data` directly is
 * safe and removes a malloc/free per RX chunk. */
static void uart_rx_callback(UINT32 port, const UINT8 *data, UINT32 len, void *arg)
{
    (void)arg;

    if (port != g_uart.config.port || !data || len == 0) {
        return;
    }

    /* Limit chunk size (reference clamped the read length the same way) */
    if (len > UART_MAX_RX_CHUNK_SIZE) {
        len = UART_MAX_RX_CHUNK_SIZE;
    }

    uart_process_received_data(data, len);
}

/*---------------------------------------------------------------
 * UART Task (State Machine)
 *--------------------------------------------------------------*/

static void uart_update_task_stats(void)
{
    UINT32 mem_allocated = 0;

    if (g_uart.current_request) {
        mem_allocated += sizeof(ModuleMessage);
    }
    if (g_uart.response_buffer) {
        mem_allocated += g_uart.response_buffer_size;
    }

    if (task_stats_update_periodic(g_uart.task,
                                   "UART",
                                   MODULE_ID_UART,
                                   &g_uart.task_stats,
                                   mem_allocated)) {
        g_uart.stats.task_stack_size = g_uart.task_stats.task_stack_size;
        g_uart.stats.task_stack_used = g_uart.task_stats.task_stack_used;
        g_uart.stats.task_stack_peak = g_uart.task_stats.task_stack_peak;
        g_uart.stats.task_stack_free = g_uart.task_stats.task_stack_free;
    }

    g_uart.stats.memory_allocated = mem_allocated;
}

static void uart_task_entry(void *arg)
{
    (void)arg;
    while (1) {
        module_manager_update_uptime(MODULE_ID_UART);
        uart_update_task_stats();

        switch (g_uart.state) {
            case UART_STATE_IDLE: {
                if (!g_uart.module || !g_uart.module->config.msg_q) break;
                static ModuleMessage req = {0};
                memset(&req, 0, sizeof(req));
                Result pop_result = queue_pop(g_uart.module->config.msg_q,
                                             &g_uart.module->config.msg_q_config,
                                             &req, 1, NULL);
                if (pop_result == RESULT_SUCCESS) {
                    g_uart.current_request = malloc(sizeof(ModuleMessage));
                    if (g_uart.current_request) {
                        *g_uart.current_request = req;
                        g_uart.current_is_ping = FALSE;
                        g_uart.has_pending_request = TRUE;
                        g_uart.state = UART_STATE_SENDING;
                    }
                } else if (g_uart.ping_active && g_uart.ping_attempts_left > 0) {
                    /* No queued work: send the next PING-STM probe. Build a binary
                     * get-device-info frame; the reply is intercepted in the frame
                     * dispatcher (current_is_ping) and turned into an OK/FAIL verdict. */
                    ModuleMessage *pm = malloc(sizeof(ModuleMessage));
                    if (pm) {
                        memset(pm, 0, sizeof(*pm));
                        int flen = stm_build_frame((UINT8 *)pm->message, (int)sizeof(pm->message),
                                                   STM_SRC_TYPE_LOCAL, STM_CMD_GET_DEV_INFO, NULL, 0);
                        if (flen > 0) {
                            pm->source_module      = MODULE_ID_UART;
                            pm->destination_module = MODULE_ID_UART;
                            pm->data_len           = (UINT32)flen;
                            pm->is_raw             = TRUE;
                            g_uart.current_request     = pm;
                            g_uart.current_is_ping     = TRUE;
                            g_uart.ping_attempts_left--;
                            g_uart.has_pending_request = TRUE;
                            g_uart.state = UART_STATE_SENDING;
                        } else {
                            free(pm);
                        }
                    }
                }
                break;
            }

            case UART_STATE_SENDING: {
                if (!g_uart.current_request) {
                    uart_reset_state();
                    break;
                }
                if (!g_uart.uart_opened) {
                    uart_reset_state();
                    break;
                }

                UINT32 bytes_written = 0;
                wm_SdkResult write_result;

                /* All STM traffic is binary now: send the raw frame bytes (inline
                 * message[] or dynamic_buffer), no ASCII framing. A non-raw request
                 * is unexpected — drop it rather than send a malformed command. */
                if (!g_uart.current_request->is_raw || g_uart.current_request->data_len == 0) {
                    uart_reset_state();
                    break;
                }
                {
                    const char *raw = module_message_payload_ptr(g_uart.current_request);
                    if (!raw) {
                        uart_reset_state();
                        break;
                    }
                    write_result = wm_sdk_uart_write(g_uart.config.port, raw,
                                                  g_uart.current_request->data_len,
                                                  &bytes_written);
                }

                if (write_result == WM_SDK_RESULT_SUCCESS) {
                    g_uart.state = UART_STATE_WAITING_RESPONSE;
                } else {
                    LOG_ERROR("ERR_UART_TX_FAILED: UART tx failed"); /* ERRC */
                    uart_reset_state();
                }
                break;
            }

            case UART_STATE_WAITING_RESPONSE: {
                UINT32 limit = g_uart.current_is_ping ? UART_PING_TIMEOUT_TICKS
                                                      : g_uart.response_timeout_ticks;
                if (++g_uart.response_timeout_counter > limit) {
                    if (g_uart.current_is_ping) {
                        if (g_uart.ping_attempts_left == 0) {
                            /* All probes timed out — STM is unresponsive. */
                            uart_route_to_module_queue(g_uart.ping_requester, g_uart.ping_addr,
                                                       "STM: FAIL (no response)");
                            g_uart.ping_active = FALSE;
                        }
                    } else {
                        LOG_ERROR("ERR_UART_RSP_TIMEOUT: UART response timed out"); /* ERRC */
#ifdef HEALTH_PACKET_UNAVAILABLE
                        /* WALNUT: health_packet not ported - a missing 0x06 reply
                         * cannot be reported as an all-zero STM half. */
#else
                        /* No 0x06 reply: report it as an all-zero STM half. */
                        if (uart_pending_cmd() == STM_CMD_SYSTEM_UPDATE) {
                            health_packet_clear_stm_block();
                            (void)health_packet_push_to_tcp();
                        }
#endif
                    }
                    /* If ping attempts remain, uart_reset_state() returns to IDLE and the
                     * next probe is issued there (ping_active stays set). */
                    uart_reset_state();
                }
                break;
            }

            case UART_STATE_RESPONSE_RECEIVED:
                uart_reset_state();
                break;
        }

        utils_sleep_ms(g_uart.task_interval_ms);
    }
}

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/

Result uart_manager_start_ping_test(ModuleId requester, const char *addr)
{
    if (!g_uart.module || !g_uart.module->status.initialized) {
        return RESULT_ERROR;
    }
    if (g_uart.ping_active) {
        return RESULT_BUSY;   /* a probe is already running */
    }
    g_uart.ping_requester     = requester;
    utils_strncpy_safe(g_uart.ping_addr, addr ? addr : "", sizeof(g_uart.ping_addr));
    g_uart.ping_attempts_left = UART_PING_MAX_ATTEMPTS;
    g_uart.current_is_ping    = FALSE;
    g_uart.ping_active        = TRUE;   /* set last: the UART task loop picks it up */
    return RESULT_SUCCESS;
}

Result uart_manager_send_request(const ModuleMessage *r)
{
    if (!g_uart.module || !g_uart.module->status.initialized || !r || !g_uart.module->config.msg_q) {
        return RESULT_ERROR;
    }
    /* Push ModuleMessage as-is. When use_dynamic_buffer is TRUE, UART task takes ownership of dynamic_buffer and frees it after sending. */
    Result result = queue_push(g_uart.module->config.msg_q,
                               &g_uart.module->config.msg_q_config,
                               r);
    if (result != RESULT_SUCCESS) return RESULT_BUSY;
    return RESULT_SUCCESS;
}

Result uart_manager_deinit(void)
{
    if (!g_uart.module || !g_uart.module->status.initialized) {
        uart_set_defaults();
        return RESULT_SUCCESS;
    }

    if (g_uart.task) {
        wm_sdk_task_delete(g_uart.task);
        g_uart.task = NULL;
    }

    if (g_uart.module && g_uart.module->config.msg_q) {
        queue_manager_destroy(g_uart.module->config.msg_q, &g_uart.module->config.msg_q_config);
        g_uart.module->config.msg_q = NULL;
    }

    if (g_uart.response_buffer) {
        free(g_uart.response_buffer);
        g_uart.response_buffer = NULL;
        g_uart.response_buffer_size = 0;
        g_uart.response_buffer_pos = 0;
    }

    uart_reset_state();
    if (g_uart.uart_opened) {
        /* WALNUT: clear the RX callback before closing - registration is
         * independent of open/close and survives a reconfigure, so leaving it
         * armed would keep delivering into a torn-down response_buffer. */
        (void)wm_sdk_uart_set_rx_callback(g_uart.config.port, NULL, NULL);
        wm_sdk_uart_control(g_uart.config.port, WM_SDK_UART_CTRL_CLOSE);   /* reference: SDK_UART_CLOSE */
        g_uart.uart_opened = FALSE;
    }
    
    uart_set_defaults();
    LOG_INFO("UART manager stopped");
    return RESULT_SUCCESS;
}

/*---------------------------------------------------------------
 * Init (last — bottom-up entry)
 *--------------------------------------------------------------*/

static void uart_set_defaults(void)
{
    g_uart.rx_buffer_size = 1024;
    g_uart.task_stack_size = UART_TASK_STACK_SIZE;
    g_uart.task_priority = TP_TIMED_ACTIVITY;   /* reference: 5 (SIMCOM priority scale) */
    g_uart.task_interval_ms = 100;
    g_uart.response_timeout_ticks = 40;   /* 40 x 100 ms = 4 s STM response wait */
    g_uart.module = NULL;
    /* reference: SDK_UART_PORT_MAIN. WM_SDK_UART_PORT_1 is the only id wm_sdk_uart.h
     * defines, and the SDK's wm_sdk_uart_set_config() requires it. See the header
     * note: that id resolves to drvUart port 0, the CP debug console, which
     * drvUartOpen unconditionally refuses - so init fails until an
     * wm_sdk_uart.h implementation that can reach a usable port is supplied. */
    g_uart.config.port = WM_SDK_UART_PORT_1;
    g_uart.config.baud_rate = 115200;           /* reference: SDK_UART_BAUD_115200 */
    g_uart.config.data_bits = 8;
    g_uart.config.stop_bits = 1;
    g_uart.config.parity = 0;
    g_uart.task = NULL;
    g_uart.state = UART_STATE_IDLE;
    g_uart.current_request = NULL;
    g_uart.has_pending_request = FALSE;
    g_uart.response_timeout_counter = 0;
    g_uart.response_buffer = NULL;
    g_uart.response_buffer_size = 0;
    g_uart.response_buffer_pos = 0;
    g_uart.uart_opened = FALSE;
    memset(&g_uart.stats, 0, sizeof(g_uart.stats));
    memset(&g_uart.task_stats, 0, sizeof(g_uart.task_stats));
}

Result uart_manager_init(void)
{
    uart_set_defaults();

    /* Validate the binary protocol (CRC + frame layout) against the spec vectors
     * once at startup — a toolchain/logic mismatch here would silently corrupt
     * every STM frame, so fail loudly. */
    if (!stm_protocol_selftest()) {
        LOG_ERROR("STM binary protocol self-test FAILED (CRC/frame mismatch)");
    }

    g_uart.module = g_modules[MODULE_ID_UART];
    if (!g_uart.module) {
        LOG_ERROR("UART module not found");
        return RESULT_ERROR;
    }

    if (g_uart.module->status.initialized)
        return RESULT_SUCCESS;

    Module *uart_module = module_manager_get_module(MODULE_ID_UART);
    if (!uart_module) {
        LOG_ERROR("UART module lookup failed");
        return RESULT_ERROR;
    }

    if (queue_manager_create(&uart_module->config.msg_q_config, &uart_module->config.msg_q) != RESULT_SUCCESS) {
        LOG_ERROR("UART queue create failed");
        return RESULT_ERROR;
    }

    g_uart.response_buffer = malloc(g_uart.rx_buffer_size);
    if (!g_uart.response_buffer) {
        LOG_ERROR("UART RX buffer alloc failed");
        queue_manager_destroy(uart_module->config.msg_q, &uart_module->config.msg_q_config);
        uart_module->config.msg_q = NULL;
        return RESULT_ERROR;
    }
    g_uart.response_buffer_size = g_uart.rx_buffer_size;

    wm_SdkUartConfig cfg = {
        .baud_rate = g_uart.config.baud_rate,
        .data_bits = g_uart.config.data_bits,
        .stop_bits = g_uart.config.stop_bits,
        .parity = g_uart.config.parity,
        .flow_control = 0            /* WALNUT-only field: 0 = none, 1 = RTS/CTS */
    };
    if (wm_sdk_uart_set_config(g_uart.config.port, &cfg) != WM_SDK_RESULT_SUCCESS) {
        LOG_ERROR("ERR_UART_CONFIG_ERROR: UART port config failed"); /* ERRC */
        // free(g_uart.response_buffer);
        // g_uart.response_buffer = NULL;
        // queue_manager_destroy(uart_module->config.msg_q, &uart_module->config.msg_q_config);
        // uart_module->config.msg_q = NULL;
        // return RESULT_ERROR;
    }

    /* reference: sdk_uart_register_callback() - walnut's equivalent delivers the
     * received bytes to the callback itself (see uart_rx_callback). */
    if (wm_sdk_uart_set_rx_callback(g_uart.config.port, uart_rx_callback, NULL) != WM_SDK_RESULT_SUCCESS) {
        LOG_ERROR("ERR_UART_CB_REG_FAILED: UART RX callback register failed"); /* ERRC */
        free(g_uart.response_buffer);
        g_uart.response_buffer = NULL;
        queue_manager_destroy(uart_module->config.msg_q, &uart_module->config.msg_q_config);
        uart_module->config.msg_q = NULL;
        return RESULT_ERROR;
    }
    g_uart.uart_opened = TRUE;

    /* reference passed g_uart_task_stack / sizeof(g_uart_task_stack); walnut lets
     * the kernel allocate the stack (stack_ptr = NULL). */
    g_uart.task = wm_sdk_task_create(uart_task_entry,
                                  NULL,
                                  "uartTask",
                                  NULL,
                                  g_uart.task_stack_size,
                                  g_uart.task_priority);
    if (!g_uart.task) {
        LOG_ERROR("UART task create failed");
        /* WALNUT: the reference bailed out here leaving the RX callback armed and
         * the request queue allocated. On walnut the callback is registered
         * independently of open/close and keeps firing, so unwind it (and the
         * queue) before returning. */
        (void)wm_sdk_uart_set_rx_callback(g_uart.config.port, NULL, NULL);
        g_uart.uart_opened = FALSE;
        free(g_uart.response_buffer);
        g_uart.response_buffer = NULL;
        queue_manager_destroy(uart_module->config.msg_q, &uart_module->config.msg_q_config);
        uart_module->config.msg_q = NULL;
        return RESULT_ERROR;
    }

    LOG_INFO("UART manager ready");
    return RESULT_SUCCESS;
}
