/**
  ******************************************************************************
  * @file    tcp.c
  * @author  WheelsEye
  * @brief   TCP client - lwIP based with robust timeout handling and
  *          self-recovery. State machine:
  *          INIT -> WAIT_NETWORK -> SOCKET_CREATING -> SOCKET_CREATED ->
  *          CONNECTING -> CONNECTED -> SENDING_LOGIN -> WAIT_ACK_LOGIN ->
  *          ACK_RECEIVED -> SENDING_DATA -> WAIT_ACK_DATA -> ACK_RECEIVED ...
  *          Any failure -> ERROR -> CLOSED -> RECONNECT_DELAY -> INIT.
  *
  *          Socket access goes through the multi-modem TCP functionality
  *          abstraction (sdk_tcp_* / sdk_functionality_tcp.h). Socket events
  *          arrive through tcp_socket_event_callback() with netconn
  *          semantics (CONNECTED, RCVPLUS, SENDACKED, CLOSE_*, ERROR_*);
  *          on walnut the backend (sdk_walnut_tcp.c) synthesizes them in
  *          its TCPMON task, since the kernel exports only the lwIP BSD
  *          socket layer, not the netconn callback layer.
  *
  *          WAIT_ACK_* states complete on SENDACKED - the peer's TCP-level
  *          acknowledgment (SO_SNDBUF drain), or send-success on kernels
  *          without SO_SNDBUF. The server sends no application-level reply.
  ******************************************************************************
  */

#include <string.h>
#include <stdio.h>

/* errno constants (EAGAIN etc.) come from lwIP arch.h via the wm_global.h
 * include chain - the same values the kernel's lwip_getsockerrno returns.
 * newlib <errno.h> must NOT be included here: its values differ. */

// sdk
#include "wm_global.h"
#include "wm_sdk_os.h"
#include "wm_sdk_log.h"

// app
#include "tcp/tcp.h"
#include "tcp/tcp_ops.h"
#include "tcp/sdk_functionality_tcp.h"
#include "network/network.h"
#include "module/module_manager.h"
#include "common/queue_manager.h"
#include "common/task_stats.h"
#include "common/utils.h"   /* SET_STATE / HANDLE_STATE (reference macros) */
#include "module/command/command_manager.h"

#define LOG_TAG "TCP"
#define LOG_MODULE_LEVEL LOG_LEVEL_DEBUG
#include "module/log/log.h"

/*===============================================================
 * Constants
 *==============================================================*/
#define TCP_TASK_INTERVAL_MS        200U
#define TCP_TASK_STACK              8192U
#define TCP_OVERALL_TIMEOUT_MS      (5U * 60U * 1000U)  /* 5 minutes */
#define TCP_RECV_BUF_SIZE           256U

/*===============================================================
 * Globals - runtime state shared with tcp_ops.c
 *==============================================================*/
tcp_client_runtime_t g_tcp_client = {
    .initialized          = false,
    .state                = TCP_STATE_INIT,
    .tcp_fd               = -1,
    .last_connected_state = false,
    .session_up           = false,
    .config               = NULL,
    .tcp_bytes_sent       = -1,
    .tcp_bytes_acked      = 0,
    .tcp_send_queue_batch_count = 0,
    .recv_pending         = 0,
    .total_iterations     = 0,
    .reconnect_delay_cycles = 0,
};

/* Packet buffers off-stack (shared with tcp_ops.c) */
char g_login_packet_buffer[TCP_LOGIN_BUFFER_SIZE];
char g_data_packet_buffer[TCP_DATA_BUFFER_SIZE];

/* Static buffers for TCP callback to avoid stack overflow (callbacks may have limited stack) */
static char g_tcp_recv_buf[TCP_RECV_BUF_SIZE] = {0};  /* Receive buffer for TCP callback */
static ModuleMessage g_tcp_cmd_msg = {0};  /* ModuleMessage buffer for TCP callback */
static char g_tcp_addr_buf[32] = {0};  /* Address buffer for TCP callback */

static void              *g_tcp_task  = NULL;
/* Stack usage sampling for the TCP task (reference: g_tcp_client.task_stats) */
static TaskStats          g_tcp_task_stats;

/*===============================================================
 * State Timeout Configuration (0 = no timeout for that state)
 * Timeout escalates to CLOSED; CLOSED/RECONNECT_DELAY fall back to INIT.
 * CONNECTING uses config->connection_timeout_ms instead of this table.
 * reset_overall marks desired states that re-anchor the overall timeout.
 *==============================================================*/
static const struct {
    TcpState state;
    UINT32   timeout_ms;
    bool     reset_overall;
} g_tcp_state_timeouts[] = {
    {TCP_STATE_WAIT_NETWORK,    60000U, false},  /* 60s - wait for network   */
    {TCP_STATE_SOCKET_CREATING, 20000U, false},  /* 20s - socket creation    */
    {TCP_STATE_SOCKET_CREATED,  5000U,  false},  /* 5s - DNS + connect start */
    {TCP_STATE_CONNECTING,      30000U, false},  /* config override applies  */
    {TCP_STATE_CONNECTED,       0U,     true },  /* desired state            */
    {TCP_STATE_SENDING_LOGIN,   10000U, false},  /* 10s - send login         */
    {TCP_STATE_WAIT_ACK_LOGIN,  20000U, false},  /* 20s - wait login ACK     */
    {TCP_STATE_ACK_RECEIVED,    0U,     true },  /* desired state            */
    /* Reference 100s keepalive: an idle send queue cycles the connection
     * (SENDING_DATA -> CLOSED -> reconnect) so liveness is re-proven. */
    {TCP_STATE_SENDING_DATA,    100000U, false},
    {TCP_STATE_WAIT_ACK_DATA,   30000U, false},  /* 30s - wait data ACK      */
    {TCP_STATE_RECEIVING,       0U,     false},
    {TCP_STATE_RECONNECT_DELAY, 20000U, false},  /* safety net > retry cfg   */
};

/*===============================================================
 * Helper Functions
 *==============================================================*/
const char *weware_tcp_state_to_string(TcpState state)
{
    switch (state) {
        case TCP_STATE_INIT:            return "INIT";
        case TCP_STATE_WAIT_NETWORK:    return "WAIT_NETWORK";
        case TCP_STATE_SOCKET_CREATING: return "SOCKET_CREATING";
        case TCP_STATE_SOCKET_CREATED:  return "SOCKET_CREATED";
        case TCP_STATE_CONNECTING:      return "CONNECTING";
        case TCP_STATE_CONNECTED:       return "CONNECTED";
        case TCP_STATE_SENDING_LOGIN:   return "SENDING_LOGIN";
        case TCP_STATE_WAIT_ACK_LOGIN:  return "WAIT_ACK_LOGIN";
        case TCP_STATE_ACK_RECEIVED:    return "ACK_RECEIVED";
        case TCP_STATE_SENDING_DATA:    return "SENDING_DATA";
        case TCP_STATE_WAIT_ACK_DATA:   return "WAIT_ACK_DATA";
        case TCP_STATE_RECEIVING:       return "RECEIVING";
        case TCP_STATE_CLOSED:          return "CLOSED";
        case TCP_STATE_RECONNECT_DELAY: return "RECONNECT_DELAY";
        case TCP_STATE_ERROR:           return "ERROR";
        default:                        return "UNKNOWN";
    }
}

/* State transitions use the reference macros SET_STATE / HANDLE_STATE
 * (common/utils.h): RESULT_SUCCESS -> on_success, RESULT_ERROR -> on_error,
 * anything else -> on_busy (written back, reference semantics). The monitor
 * is level-triggered, so a CONNECTED overwritten by a concurrent on_busy
 * write is re-delivered on the next TCPMON pass. */

static void tcp_log_state_transition(TcpState old_state, TcpState new_state)
{
    if (old_state != new_state)
        LOG_INFO("TCP [STATE] %s -> %s",
                     weware_tcp_state_to_string(old_state),
                     weware_tcp_state_to_string(new_state));
}

/*===============================================================
 * One recv per TCP cycle (no drain loop). EAGAIN = clear flag;
 * the next RCVPLUS sets it again.
 *==============================================================*/
/** @return true if more data may be pending (keep flag), false to clear. */
static bool tcp_try_recv_once_and_dispatch(void)
{
    int fd = g_tcp_client.tcp_fd;
    if (fd < 0)
        return false;
    char *buf = g_tcp_recv_buf;
    ModuleMessage *cmd_msg = &g_tcp_cmd_msg;
    size_t buf_size = sizeof(g_tcp_recv_buf) - 1;
    memset(buf, 0, sizeof(g_tcp_recv_buf));
    int rlen = sdk_tcp_recv(fd, buf, (unsigned int)buf_size, 0);
    if (rlen > 0)
    {
        if (rlen < 5)
            return true;   /* skip short, more may be pending */
        if (rlen < (int)sizeof(g_tcp_recv_buf))
            buf[rlen] = '\0';
        else
        {
            rlen = (int)buf_size;
            buf[rlen] = '\0';
        }
        LOG_INFO("[TCP] Received data (%d bytes): %.*s", rlen, (int)((rlen < 200) ? rlen : 200), buf);
        memset(cmd_msg, 0, sizeof(g_tcp_cmd_msg));
        cmd_msg->source_module = MODULE_ID_TCP;
        cmd_msg->destination_module = MODULE_ID_CMD;
        if (g_tcp_client.config)
        {
            memset(g_tcp_addr_buf, 0, sizeof(g_tcp_addr_buf));
            int addr_len = snprintf(g_tcp_addr_buf, sizeof(g_tcp_addr_buf), "%s:%d",
                                   g_tcp_client.config->server_ip, g_tcp_client.config->server_port);
            if (addr_len > 0 && addr_len < (int)sizeof(cmd_msg->address))
                utils_strncpy_safe(cmd_msg->address, g_tcp_addr_buf, sizeof(cmd_msg->address));
            else
                cmd_msg->address[0] = '\0';
        }
        else
            cmd_msg->address[0] = '\0';
        {
            int copied = utils_strncpy_safe(cmd_msg->message, buf, sizeof(cmd_msg->message));
            if (copied >= 0) {
                cmd_msg->data_len = (UINT32)copied;
            }
        }
        if (command_manager_accept_request(cmd_msg) != RESULT_SUCCESS)
            LOG_WARN("[TCP] Failed to send to command manager");
        return true;   /* more data may be pending, keep flag for next cycle */
    }
    if (rlen == 0)
        return false;  /* closed */
    if (sdk_tcp_get_sock_errno(fd) == EAGAIN)
        return false;  /* not ready this cycle; clear flag, next RCVPLUS will set it again */
    LOG_WARN("[TCP] recv failed, errno: %d", sdk_tcp_get_sock_errno(fd));
    return false;
}

/*===============================================================
 * Socket Event Callback (netconn semantics; runs on the TCPMON task)
 * RCVPLUS: set flag only; the task loop does recv and dispatch.
 *==============================================================*/
void tcp_socket_event_callback(int s, int evt, unsigned short int len)
{
    /* Read state once at start (race-condition protection) */
    TcpState current_state = g_tcp_client.state;

    /* Validate state before proceeding */
    if (current_state < TCP_STATE_INIT || current_state > TCP_STATE_ERROR) {
        LOG_ERROR("TCP [CALLBACK] invalid state %d, ignoring event %d",
                      current_state, evt);
        return;
    }

    /* Ignore callbacks for the wrong / already-closed socket */
    if (s != g_tcp_client.tcp_fd || g_tcp_client.tcp_fd < 0) {
        LOG_DEBUG("TCP [CALLBACK] ignoring event %d for stale fd %d (current %d)",
                        evt, s, g_tcp_client.tcp_fd);
        return;
    }

    /* Ignore callbacks if already in CLOSED state (prevents re-entry) */
    if (current_state == TCP_STATE_CLOSED)
        return;

    switch (evt) {
    case SDK_NETCONN_EVT_CONNECTED:
        if (current_state == TCP_STATE_CONNECTING ||
            current_state == TCP_STATE_SOCKET_CREATED) {
            LOG_INFO("TCP [CALLBACK] connection established -> CONNECTED");
            SET_STATE(g_tcp_client.state, TCP_STATE_CONNECTED);
        } else if (current_state == TCP_STATE_ERROR) {
            /* Connection succeeded despite ERROR state - recover */
            LOG_INFO("TCP [CALLBACK] CONNECTED in ERROR state - recovering");
            SET_STATE(g_tcp_client.state, TCP_STATE_CONNECTED);
        } else if (current_state == TCP_STATE_CONNECTED) {
            /* Duplicate (the monitor is level-triggered) - ignore */
        } else {
            /* Unexpected state - but connection succeeded, so accept it
             * (reference behavior) */
            LOG_WARN("TCP [CALLBACK] CONNECTED in unexpected state %s, accepting anyway",
                            weware_tcp_state_to_string(current_state));
            SET_STATE(g_tcp_client.state, TCP_STATE_CONNECTED);
        }
        break;

    case SDK_NETCONN_EVT_SENDPLUS:
        LOG_DEBUG("TCP [CALLBACK] SENDPLUS: send buffer available");
        break;

    case SDK_NETCONN_EVT_SENDMINUS:
        LOG_DEBUG("TCP [CALLBACK] SENDMINUS: send buffer not available");
        break;

    case SDK_NETCONN_EVT_SENDACKED:
    {
        /* Track ACK for any pending TCP send operation */
        if (g_tcp_client.tcp_bytes_sent > 0) {
            g_tcp_client.tcp_bytes_acked += (int)len;

            if (g_tcp_client.tcp_bytes_acked >= g_tcp_client.tcp_bytes_sent) {
                /* Re-read state to ensure we have the latest value */
                TcpState ack_state = g_tcp_client.state;
                LOG_INFO("TCP [CALLBACK] send fully acknowledged (%d bytes), state=%s",
                             g_tcp_client.tcp_bytes_sent,
                             weware_tcp_state_to_string(ack_state));

                if (ack_state == TCP_STATE_WAIT_ACK_LOGIN ||
                    ack_state == TCP_STATE_WAIT_ACK_DATA) {
                    SET_STATE(g_tcp_client.state, TCP_STATE_ACK_RECEIVED);
                }
            } else {
                LOG_DEBUG("TCP [CALLBACK] partial ACK: %d/%d bytes",
                                g_tcp_client.tcp_bytes_acked,
                                g_tcp_client.tcp_bytes_sent);
            }
        } else {
            LOG_DEBUG("TCP [CALLBACK] SENDACKED with no pending send");
        }
        break;
    }

    case SDK_NETCONN_EVT_RCVPLUS:
        /* Set flag only; task loop does recv and dispatch */
        LOG_DEBUG("TCP [CALLBACK] RCVPLUS: len=%u (recv in task)", len);
        g_tcp_client.recv_pending = 1;
        break;

    case SDK_NETCONN_EVT_RCVMINUS:
        LOG_DEBUG("TCP [CALLBACK] RCVMINUS");
        break;

    case SDK_NETCONN_EVT_ACCEPTPLUS:
        LOG_DEBUG("TCP [CALLBACK] ACCEPTPLUS (client socket - unexpected)");
        break;

    case SDK_NETCONN_EVT_CLOSE_WAIT:
    case SDK_NETCONN_EVT_CLOSE_NORMAL:
    case SDK_NETCONN_EVT_ERROR_CLSD:
    case SDK_NETCONN_EVT_ERROR_RST:
    case SDK_NETCONN_EVT_ERROR_ABRT:
    case SDK_NETCONN_EVT_ERROR:
    {
        LOG_WARN("TCP [CALLBACK] %s in state %s -> CLOSED",
                        (evt == SDK_NETCONN_EVT_CLOSE_WAIT)   ? "CLOSE_WAIT" :
                        (evt == SDK_NETCONN_EVT_CLOSE_NORMAL) ? "CLOSE_NORMAL" :
                        (evt == SDK_NETCONN_EVT_ERROR_CLSD)   ? "ERROR_CLSD" :
                        (evt == SDK_NETCONN_EVT_ERROR_RST)    ? "ERROR_RST" :
                        (evt == SDK_NETCONN_EVT_ERROR_ABRT)   ? "ERROR_ABRT" : "ERROR",
                        weware_tcp_state_to_string(current_state));

        /* Reset send tracking if we were in a send state */
        if (current_state == TCP_STATE_SENDING_DATA ||
            current_state == TCP_STATE_WAIT_ACK_DATA ||
            current_state == TCP_STATE_SENDING_LOGIN ||
            current_state == TCP_STATE_WAIT_ACK_LOGIN) {
            tcp_reset_send_tracking();
        }

        SET_STATE(g_tcp_client.state, TCP_STATE_CLOSED);
        break;
    }

    default:
        LOG_WARN("TCP [CALLBACK] unknown event %d for socket %d", evt, s);
        break;
    }
}

/*===============================================================
 * Timeout supervision
 *==============================================================*/
static void tcp_check_state_timeout(void)
{
    UINT32 elapsed = wm_sdk_get_ticks() - g_tcp_client.state_entry_tick;
    UINT32 timeout = 0;
    UINT32 i;

    if (g_tcp_client.state == TCP_STATE_CONNECTING && g_tcp_client.config) {
        timeout = g_tcp_client.config->connection_timeout_ms;
    } else {
        for (i = 0; i < sizeof(g_tcp_state_timeouts) / sizeof(g_tcp_state_timeouts[0]); i++) {
            if (g_tcp_state_timeouts[i].state == g_tcp_client.state) {
                timeout = g_tcp_state_timeouts[i].timeout_ms;
                break;
            }
        }
    }
    if (timeout == 0U || elapsed < timeout)
        return;

    LOG_WARN("TCP [TIMEOUT] state timeout in %s after %lu ms",
                    weware_tcp_state_to_string(g_tcp_client.state),
                    (unsigned long)elapsed);

    /* Reset send tracking on timeout, then recover via CLOSED */
    tcp_reset_send_tracking();

    if (g_tcp_client.state == TCP_STATE_CLOSED ||
        g_tcp_client.state == TCP_STATE_RECONNECT_DELAY)
        SET_STATE(g_tcp_client.state, TCP_STATE_INIT);
    else
        SET_STATE(g_tcp_client.state, TCP_STATE_CLOSED);

    g_tcp_client.state_entry_tick = wm_sdk_get_ticks();
}

static void tcp_check_overall_timeout(void)
{
    UINT32 i;

    /* Desired states re-anchor the overall timeout */
    for (i = 0; i < sizeof(g_tcp_state_timeouts) / sizeof(g_tcp_state_timeouts[0]); i++) {
        if (g_tcp_state_timeouts[i].state == g_tcp_client.state &&
            g_tcp_state_timeouts[i].reset_overall) {
            g_tcp_client.session_down_since = wm_sdk_get_ticks();
            return;
        }
    }

    if ((wm_sdk_get_ticks() - g_tcp_client.session_down_since) < TCP_OVERALL_TIMEOUT_MS)
        return;

    LOG_ERROR("TCP [TIMEOUT] overall timeout expired in %s, closing connection for recovery",
                  weware_tcp_state_to_string(g_tcp_client.state));
    tcp_reset_send_tracking();
    SET_STATE(g_tcp_client.state, TCP_STATE_CLOSED);
    g_tcp_client.session_down_since = wm_sdk_get_ticks();
}

/*===============================================================
 * Main Task Loop
 *==============================================================*/
static void tcp_client_main_loop(void *param)
{
    TcpState last_state = g_tcp_client.state;

    (void)param;
    LOG_INFO("TCP client main loop started");

    g_tcp_client.state_entry_tick   = wm_sdk_get_ticks();
    g_tcp_client.session_down_since = wm_sdk_get_ticks();

    while (1) {
        g_tcp_client.total_iterations++;

        /* Task-stall watchdog feed (module_manager_monitor_tasks) */
        module_manager_update_uptime(MODULE_ID_TCP);
        (void)task_stats_update_periodic(g_tcp_task, "TCP", MODULE_ID_TCP,
                                         &g_tcp_task_stats, 0);

        /* Validate state before processing (defensive check) */
        if (g_tcp_client.state < TCP_STATE_INIT ||
            g_tcp_client.state > TCP_STATE_ERROR) {
            LOG_ERROR("TCP invalid state %d (possible corruption), resetting to INIT",
                          g_tcp_client.state);
            SET_STATE(g_tcp_client.state, TCP_STATE_INIT);
        }

        /* Log state transition + restart the per-state timeout clock */
        if (last_state != g_tcp_client.state) {
            tcp_log_state_transition(last_state, g_tcp_client.state);
            last_state = g_tcp_client.state;
            g_tcp_client.state_entry_tick = wm_sdk_get_ticks();
        }

        tcp_check_state_timeout();
        tcp_check_overall_timeout();

        /* One recv per cycle when RCVPLUS set the flag; clear when no data
         * (EAGAIN = next RCVPLUS re-arms it) */
        if (g_tcp_client.recv_pending) {
            if (!tcp_try_recv_once_and_dispatch())
                g_tcp_client.recv_pending = 0;
        }

        switch (g_tcp_client.state) {
        case TCP_STATE_INIT:
            SET_STATE(g_tcp_client.state, TCP_STATE_WAIT_NETWORK);
            break;

        case TCP_STATE_WAIT_NETWORK:
            HANDLE_STATE(g_tcp_client.state, tcp_state_handle_wait_network(),
                              TCP_STATE_SOCKET_CREATING,
                              TCP_STATE_ERROR,
                              TCP_STATE_WAIT_NETWORK);
            break;

        case TCP_STATE_SOCKET_CREATING:
            HANDLE_STATE(g_tcp_client.state, tcp_state_handle_socket_creating(),
                              TCP_STATE_SOCKET_CREATED,
                              TCP_STATE_ERROR,
                              TCP_STATE_SOCKET_CREATING);
            break;

        case TCP_STATE_SOCKET_CREATED:
            HANDLE_STATE(g_tcp_client.state, tcp_state_handle_socket_created(),
                              TCP_STATE_CONNECTING,
                              TCP_STATE_ERROR,
                              TCP_STATE_SOCKET_CREATED);
            break;

        case TCP_STATE_CONNECTING:
            HANDLE_STATE(g_tcp_client.state, tcp_state_handle_connecting(),
                              TCP_STATE_CONNECTED,
                              TCP_STATE_ERROR,
                              TCP_STATE_CONNECTING);
            break;

        case TCP_STATE_CONNECTED:
            HANDLE_STATE(g_tcp_client.state, tcp_state_handle_connected(),
                              TCP_STATE_SENDING_LOGIN,
                              TCP_STATE_ERROR,
                              TCP_STATE_CONNECTED);
            break;

        case TCP_STATE_SENDING_LOGIN:
            HANDLE_STATE(g_tcp_client.state, tcp_state_handle_sending_login(),
                              TCP_STATE_WAIT_ACK_LOGIN,
                              TCP_STATE_ERROR,
                              TCP_STATE_SENDING_LOGIN);
            break;

        case TCP_STATE_WAIT_ACK_LOGIN:
            HANDLE_STATE(g_tcp_client.state, tcp_state_handle_wait_ack_login(),
                              TCP_STATE_ACK_RECEIVED,
                              TCP_STATE_ERROR,
                              TCP_STATE_WAIT_ACK_LOGIN);
            break;

        case TCP_STATE_ACK_RECEIVED:
            HANDLE_STATE(g_tcp_client.state, tcp_state_handle_ack_received(),
                              TCP_STATE_SENDING_DATA,
                              TCP_STATE_ERROR,
                              TCP_STATE_ACK_RECEIVED);
            break;

        case TCP_STATE_SENDING_DATA:
            HANDLE_STATE(g_tcp_client.state, tcp_state_handle_sending_data(),
                              TCP_STATE_WAIT_ACK_DATA,
                              TCP_STATE_ERROR,
                              TCP_STATE_SENDING_DATA);
            break;

        case TCP_STATE_WAIT_ACK_DATA:
            HANDLE_STATE(g_tcp_client.state, tcp_state_handle_wait_ack_data(),
                              TCP_STATE_ACK_RECEIVED,
                              TCP_STATE_ERROR,
                              TCP_STATE_WAIT_ACK_DATA);
            break;

        case TCP_STATE_RECEIVING:
            HANDLE_STATE(g_tcp_client.state, tcp_state_handle_receiving(),
                              TCP_STATE_SENDING_DATA,
                              TCP_STATE_ERROR,
                              TCP_STATE_RECEIVING);
            break;

        case TCP_STATE_CLOSED:
            HANDLE_STATE(g_tcp_client.state, tcp_state_handle_closed(),
                              TCP_STATE_RECONNECT_DELAY,
                              TCP_STATE_ERROR,
                              TCP_STATE_CLOSED);
            break;

        case TCP_STATE_RECONNECT_DELAY:
            HANDLE_STATE(g_tcp_client.state, tcp_state_handle_reconnect_delay(),
                              TCP_STATE_INIT,
                              TCP_STATE_ERROR,
                              TCP_STATE_RECONNECT_DELAY);
            break;

        case TCP_STATE_ERROR:
            HANDLE_STATE(g_tcp_client.state, tcp_state_handle_error(),
                              TCP_STATE_CLOSED,
                              TCP_STATE_ERROR,
                              TCP_STATE_ERROR);
            break;
        }

        wm_sdk_task_sleep(TCP_TASK_INTERVAL_MS);
    }
}

/*===============================================================
 * Public API
 *==============================================================*/
TcpState weware_tcp_get_state(void)
{
    return g_tcp_client.state;
}

bool weware_tcp_is_ready(void)
{
    return g_tcp_client.session_up;
}

wm_SdkResult weware_tcp_send(const void *data, UINT16 len)
{
    /* Reference-parity store-and-forward: push a ModuleMessage row into the
     * persistent TCP send queue (TCP_SEND_Q). The queue absorbs payloads
     * while the session is down; rows are popped only after the server ACK
     * and persist across a soft reboot. */
    if (!data || len == 0U || len > MODULE_MESSAGE_INLINE_SIZE)
        return WM_SDK_RESULT_INVALID_PARAM;

    const ModuleConfig *tcp_config = module_manager_get_config(MODULE_ID_TCP);
    if (!tcp_config || !tcp_config->msg_q || tcp_config->msg_q_config.element_size == 0U)
        return WM_SDK_RESULT_ERROR;

    ModuleMessage module_msg = {0};
    module_msg.source_module      = MODULE_ID_TCP;
    module_msg.destination_module = MODULE_ID_TCP;
    module_msg.use_dynamic_buffer = FALSE;
    memcpy(module_msg.message, data, len);
    module_msg.data_len = len;

    Result qr = queue_push(tcp_config->msg_q, &tcp_config->msg_q_config, &module_msg);
    if (qr != RESULT_SUCCESS)
        return (qr == RESULT_BUSY) ? WM_SDK_RESULT_BUSY : WM_SDK_RESULT_ERROR;
    return WM_SDK_RESULT_SUCCESS;
}

wm_SdkResult weware_tcp_reset_connection(void)
{
    LOG_INFO("TCP connection reset requested");
    SET_STATE(g_tcp_client.state, TCP_STATE_CLOSED);
    return WM_SDK_RESULT_SUCCESS;
}

wm_SdkResult weware_tcp_init(void)
{
    LOG_INFO("Initializing TCP module");

    if (g_tcp_client.initialized) {
        LOG_WARN("TCP module already initialized");
        return WM_SDK_RESULT_SUCCESS;
    }

    /* Select the modem backend for the TCP functionality abstraction */
    sdk_platform_register_tcp_ops(sdk_walnut_get_tcp_functionality_ops());

    /* Config (defaults applied on first use) */
    g_tcp_client.config = weware_tcp_config_get();
    LOG_INFO("TCP config loaded: %s:%u",
                 g_tcp_client.config->server_ip,
                 (unsigned)g_tcp_client.config->server_port);

    /* Send queue (reference tcp_create_queue): TCP_SEND_Q from the registry
     * (capacity 7, ModuleMessage rows, file overflow, persists across a soft
     * reboot - queue_manager_create reloads the persisted snapshot). */
    Module *tcp_module = module_manager_get_module(MODULE_ID_TCP);
    if (tcp_module && tcp_module->config.msg_q == NULL &&
        tcp_module->config.msg_q_config.element_size > 0U &&
        tcp_module->config.msg_q_config.capacity > 0U) {
        if (queue_manager_create(&tcp_module->config.msg_q_config,
                                 &tcp_module->config.msg_q) != RESULT_SUCCESS) {
            LOG_ERROR("Failed to create TCP send queue '%s'",
                          tcp_module->config.msg_q_config.name);
            return WM_SDK_RESULT_ERROR;
        }
        LOG_INFO("TCP send queue created: '%s' (cap=%u, elem=%u)",
                     tcp_module->config.msg_q_config.name,
                     (unsigned)tcp_module->config.msg_q_config.capacity,
                     (unsigned)tcp_module->config.msg_q_config.element_size);
    }

    tcp_reset_send_tracking();

    /* The event-monitor (TCPMON) task is owned by the walnut TCP backend
     * and starts with the first event-driven socket. */
    if (g_tcp_task == NULL) {
        g_tcp_task = wm_sdk_task_create(tcp_client_main_loop, NULL, "TCPCLI",
                                     NULL, TCP_TASK_STACK,
                                     TP_TIMED_ACTIVITY);
        if (g_tcp_task == NULL) {
            LOG_ERROR("Failed to create TCP task");
            return WM_SDK_RESULT_ERROR;
        }
    }

    g_tcp_client.initialized = true;
    LOG_INFO("TCP client ready");
    return WM_SDK_RESULT_SUCCESS;
}

wm_SdkResult weware_tcp_deinit(void)
{
    if (!g_tcp_client.initialized)
        return WM_SDK_RESULT_SUCCESS;

    LOG_INFO("Deinitializing TCP client...");

    if (g_tcp_task != NULL) {
        wm_sdk_task_delete(g_tcp_task);
        g_tcp_task = NULL;
    }

    if (g_tcp_client.tcp_fd >= 0) {
        sdk_tcp_close(g_tcp_client.tcp_fd);
        g_tcp_client.tcp_fd = -1;
    }

    /* Destroy the send queue (reference tcp_client_deinit pattern) */
    Module *tcp_module = module_manager_get_module(MODULE_ID_TCP);
    if (tcp_module && tcp_module->config.msg_q != NULL) {
        queue_manager_destroy(tcp_module->config.msg_q,
                              &tcp_module->config.msg_q_config);
        tcp_module->config.msg_q = NULL;
    }

    memset(&g_tcp_client, 0, sizeof(g_tcp_client));
    g_tcp_client.state  = TCP_STATE_INIT;
    g_tcp_client.tcp_fd = -1;
    tcp_reset_send_tracking();

    LOG_INFO("TCP client stopped");
    return WM_SDK_RESULT_SUCCESS;
}
