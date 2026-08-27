/**
  ******************************************************************************
  * @file    tcp.h
  * @author  WheelsEye
  * @brief   TCP client module public interface for the weware application.
  *          State-machine driven client with per-state timeouts and
  *          self-recovery: INIT -> WAIT_NETWORK -> SOCKET_CREATING ->
  *          SOCKET_CREATED -> CONNECTING -> CONNECTED -> SENDING_LOGIN ->
  *          WAIT_ACK_LOGIN -> ACK_RECEIVED -> SENDING_DATA -> WAIT_ACK_DATA
  *          -> ACK_RECEIVED ... any failure -> CLOSED -> RECONNECT_DELAY ->
  *          INIT.
  ******************************************************************************
  */

#ifndef WEWARE_TCP_H
#define WEWARE_TCP_H

#include <stdbool.h>
#include <stddef.h>

#include "sdk_types.h"

/*---------------------------------------------------------------
 * Types
 *--------------------------------------------------------------*/
/* TCP client state machine states */
typedef enum {
    TCP_STATE_INIT = 0,
    TCP_STATE_WAIT_NETWORK,
    TCP_STATE_SOCKET_CREATING,
    TCP_STATE_SOCKET_CREATED,
    TCP_STATE_CONNECTING,
    TCP_STATE_CONNECTED,
    TCP_STATE_SENDING_LOGIN,
    TCP_STATE_WAIT_ACK_LOGIN,
    TCP_STATE_ACK_RECEIVED,
    TCP_STATE_SENDING_DATA,
    TCP_STATE_WAIT_ACK_DATA,
    TCP_STATE_RECEIVING,
    TCP_STATE_CLOSED,
    TCP_STATE_RECONNECT_DELAY,
    TCP_STATE_ERROR,
} TcpState;

/* Runtime-configurable TCP client settings (see tcp_config.c) */
typedef struct {
    char   server_ip[64];           /* server IP or hostname                */
    UINT16 server_port;             /* server TCP port                      */
    UINT32 connection_timeout_ms;   /* CONNECTING state timeout             */
    UINT32 send_timeout_ms;         /* budget for one send                  */
    UINT32 retry_interval_ms;       /* RECONNECT_DELAY dwell                */
    bool   login_with_gps;          /* include GPS fix in the login packet  */
    bool   live_first;              /* send live data before backlog        */
} WewareTcpConfig;

/* Live config storage (tcp_config.c); exported for the module registry so
 * config_load_from_file can fill it before weware_tcp_init runs. */
extern WewareTcpConfig g_tcp_config;

/*---------------------------------------------------------------
 * Public API (state machine - tcp.c)
 *--------------------------------------------------------------*/
/**
 * @brief  Start the TCP client state-machine task.
 * @return SDK_RESULT_SUCCESS or negative error.
 */
SdkResult weware_tcp_init(void);

/**
 * @brief  Stop the task, close the socket and reset module state.
 * @return SDK_RESULT_SUCCESS or negative error.
 */
SdkResult weware_tcp_deinit(void);

/**
 * @brief  Current TCP state-machine value (diagnostic).
 */
TcpState weware_tcp_get_state(void);

/**
 * @brief  Human-readable name for a TCP state (static string).
 */
const char *weware_tcp_state_to_string(TcpState state);

/**
 * @brief  Session ready: connected AND login exchange complete.
 * @return true when data can be sent.
 */
bool weware_tcp_is_ready(void);

/**
 * @brief  Enqueue one payload on the persistent TCP send queue (TCP_SEND_Q).
 *         The queue absorbs payloads while the session is down; the state
 *         machine batches up to 5 rows per send and pops them only after
 *         the server ACK. Max payload: MODULE_MESSAGE_INLINE_SIZE bytes.
 * @return SDK_RESULT_SUCCESS if queued;
 *         SDK_RESULT_BUSY if the queue (and its file overflow) is full;
 *         SDK_RESULT_ERROR if the queue does not exist;
 *         SDK_RESULT_INVALID_PARAM on bad arguments.
 */
SdkResult weware_tcp_send(const void *data, UINT16 len);

/**
 * @brief  Force-close the connection and re-run the connect sequence
 *         (used after a config change).
 * @return SDK_RESULT_SUCCESS.
 */
SdkResult weware_tcp_reset_connection(void);

/*---------------------------------------------------------------
 * Public API (configuration - tcp_config.c)
 *--------------------------------------------------------------*/
/**
 * @brief  Get the live config storage (defaults applied on first call).
 */
WewareTcpConfig *weware_tcp_config_get(void);

/**
 * @brief  Fill @p config with the compile-time defaults.
 */
void weware_tcp_config_get_defaults(WewareTcpConfig *config);

/**
 * @brief  Validate every field of @p config.
 * @return SDK_RESULT_SUCCESS or SDK_RESULT_INVALID_PARAM.
 */
SdkResult weware_tcp_config_validate(const WewareTcpConfig *config);

/**
 * @brief  Set server endpoint and reset the connection to apply it.
 * @return SDK_RESULT_SUCCESS or SDK_RESULT_INVALID_PARAM.
 */
SdkResult weware_tcp_config_set_server(const char *server_ip, UINT16 server_port);

/**
 * @brief  Apply a comma-separated "key:value" config string, e.g.
 *         "ip:65.1.190.236,port:9616,conn-to:30000,send-to:5000,
 *          retry:10000,loginwithgps:false,live-first:true".
 *         All values validate before any is applied; on success the
 *         connection is reset to pick up the new settings.
 * @return SDK_RESULT_SUCCESS or SDK_RESULT_INVALID_PARAM.
 */
SdkResult weware_tcp_config_set(const char *config_string);

/**
 * @brief  Render the current config as a "key:value" string (same
 *         format accepted by weware_tcp_config_set()).
 * @return SDK_RESULT_SUCCESS, SDK_RESULT_INVALID_PARAM or
 *         SDK_RESULT_ERROR if @p buffer is too small.
 */
SdkResult weware_tcp_config_get_string(char *buffer, size_t buffer_size);

#endif /* WEWARE_TCP_H */
