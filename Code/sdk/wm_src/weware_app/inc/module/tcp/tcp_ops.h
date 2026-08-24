/**
  ******************************************************************************
  * @file    tcp_ops.h
  * @author  WheelsEye
  * @brief   TCP operations and state handlers - lwIP based (internal API
  *          between tcp.c and tcp_ops.c).
  *
  *          The socket layer is reached through the multi-modem TCP
  *          functionality abstraction (sdk_functionality_tcp.h); on walnut
  *          the backend (sdk_walnut_tcp.c) emulates event-driven sockets and
  *          delivers SDK_NETCONN_EVT_* events (see
  *          sdk_functionality_network_compat.h) into
  *          tcp_socket_event_callback().
  ******************************************************************************
  */

#ifndef WEWARE_TCP_OPS_H
#define WEWARE_TCP_OPS_H

#include <stdbool.h>

#include "sdk_types.h"
#include "common/types.h"   /* Result - state handlers return it (reference) */
#include "tcp/tcp.h"
#include "tcp/sdk_functionality_network_compat.h"

/*---------------------------------------------------------------
 * Buffer sizes
 *--------------------------------------------------------------*/
/* Login + optional last-valid GPS append (reference:
 * LOGIN_TCP_COMBINED_MAX_SIZE = LOGIN_PACKET_TOTAL_SIZE 24 +
 * GPS_PACKET_TOTAL_SIZE 55). */
#define TCP_LOGIN_BUFFER_SIZE   79
#define TCP_DATA_BUFFER_SIZE    512   /* combined data packets (max 512)    */

/*---------------------------------------------------------------
 * Types
 *--------------------------------------------------------------*/
/* Handlers return Result (reference tcp_ops_lwip.h): RESULT_SUCCESS =
 * transition, RESULT_ERROR = error path, anything else (RESULT_BUSY) = stay.
 * Dispatched by the HANDLE_STATE() macro (common/utils.h). */

/* TCP client runtime state (single instance g_tcp_client, owned by tcp.c) */
typedef struct {
    bool             initialized;
    TcpState         state;
    int              tcp_fd;
    bool             last_connected_state; /* connect-edge for events/log   */
    bool             session_up;           /* login exchange complete       */
    WewareTcpConfig *config;

    /* Send/ACK tracking driven by SENDACKED events (reference-compatible) */
    volatile int     tcp_bytes_sent;       /* -1 = no send in flight        */
    volatile int     tcp_bytes_acked;
    UINT32           tcp_send_queue_batch_count; /* send-queue rows peeked for
                                                    the in-flight send; popped
                                                    on ACK */

    /* Set by RCVPLUS; task loop drains recv and clears (non-blocking) */
    volatile UINT8   recv_pending;

    /* timing (sdk_get_ticks() ms, wrap-safe via unsigned subtraction) */
    UINT32           state_entry_tick;
    UINT32           session_down_since;   /* overall-timeout anchor        */
    UINT32           total_iterations;
    UINT32           reconnect_delay_cycles;
} tcp_client_runtime_t;

/*---------------------------------------------------------------
 * Shared runtime + buffers (defined in tcp.c)
 *--------------------------------------------------------------*/
extern tcp_client_runtime_t g_tcp_client;
extern char g_login_packet_buffer[TCP_LOGIN_BUFFER_SIZE];
extern char g_data_packet_buffer[TCP_DATA_BUFFER_SIZE];

/* Socket event sink (defined in tcp.c, reference architecture) */
void tcp_socket_event_callback(int s, int evt, unsigned short int len);

/*---------------------------------------------------------------
 * State handlers (tcp_ops.c)
 *--------------------------------------------------------------*/
Result tcp_state_handle_wait_network(void);
Result tcp_state_handle_socket_creating(void);
Result tcp_state_handle_socket_created(void);
Result tcp_state_handle_connecting(void);
Result tcp_state_handle_connected(void);
Result tcp_state_handle_sending_login(void);
Result tcp_state_handle_wait_ack_login(void);
Result tcp_state_handle_ack_received(void);
Result tcp_state_handle_sending_data(void);
Result tcp_state_handle_wait_ack_data(void);
Result tcp_state_handle_receiving(void);
Result tcp_state_handle_closed(void);
Result tcp_state_handle_reconnect_delay(void);
Result tcp_state_handle_error(void);

/*---------------------------------------------------------------
 * Command/response packet builders (reference: tcp_ops_lwip.h)
 *--------------------------------------------------------------*/
/** TCP packet type 0x26 (38) - ASCII command/response payload */
#define TCP_CMD_RESP_PACKET_TYPE  38
#define TCP_CMD_RESP_HEADER_SIZE  7

/**
 * @brief Build variable-length command response packet (type 38) for TCP queue.
 * Layout: WE(2) + type(1) + payload_len(2 LE) + payload(N) + we(2) = 7 + N bytes.
 * @param payload_len Payload length (pass < 0 to use strlen(payload)); max 249
 * @return Total packet length (7 + payload_len) on success, 0 on failure
 */
int tcp_command_response_packet_create(char *out, int out_size, const char *payload, int payload_len);

/**
 * @brief Build a BLE command-response packet (type 39) for the TCP queue.
 * Decodes a hex string into binary and frames it; used for STM replies of the
 * form "OK,cmd-rsp,<hex>" (caller passes only the <hex> part).
 * Layout: WE(2) + type(1)=39 + payload_len(2 LE) + binary(N) + we(2) = 7 + N.
 * @return Total packet length on success, 0 on failure (incl. malformed/empty hex).
 */
int tcp_ble_cmd_response_packet_create(char *out, int out_size, const char *hex_str, int hex_len);

/** Reset tcp_bytes_sent/acked/batch (call on timeout/close/new connection). */
void tcp_reset_send_tracking(void);

/** true when a send is in flight and fully acknowledged. */
bool tcp_check_ack_received(void);

#endif /* WEWARE_TCP_OPS_H */
