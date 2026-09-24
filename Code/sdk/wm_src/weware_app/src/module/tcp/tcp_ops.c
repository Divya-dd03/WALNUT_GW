/**
  ******************************************************************************
  * @file    tcp_ops.c
  * @author  WheelsEye
  * @brief   TCP operations and state handlers - lwIP based implementation
  *          with robust error handling (walnut port of tcp_ops_lwip.c).
  *
  *          All socket access goes through the multi-modem functionality
  *          abstraction (sdk_tcp_* -> sdk_walnut_tcp.c); this file holds the
  *          reference state-handler logic unchanged. Walnut divergences:
  *          - DNS uses lwip_getaddrinfo directly (result sockaddr is already
  *            in the kernel format the backend's connect expects);
  *          - the backend makes sockets non-blocking at creation (no
  *            SO_NONBLOCK setsockopt on this platform).
  *          The send path is the reference store-and-forward: peek a batch
  *          of up to 5 TCP_SEND_Q rows, combine into one 512-byte frame,
  *          optional live-first tail prepend, pop rows only after the ACK.
  ******************************************************************************
  */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* lwIP headers for AF_INET/addrinfo/lwip_shutdown and the errno values the
 * kernel's lwip_getsockerrno returns (do NOT add newlib <errno.h>). The
 * compat macros these define (send/recv/close/connect/...) are not used -
 * all socket calls go through sdk_tcp_* below. */
#include "lwip/sockets.h"
#include "lwip/netdb.h"

// sdk
#include "wm_global.h"
#include "wm_sdk_os.h"
#include "wm_sdk_log.h"

// app
#include "tcp/tcp_ops.h"
#include "tcp/login_packet.h"
#include "tcp/sdk_functionality_tcp.h"
#include "network/network.h"
#include "module/module_manager.h"   /* ModuleMessage / ModuleConfig / msg_q */
#include "module/gps/gps_packet.h"   /* WE / we frame markers, GPS_PACKET_TOTAL_SIZE */
#include "module/gps/gps_ops.h"      /* last-valid GPS append for login */
#include "common/queue_manager.h"
#include "common/event_manager.h"
#include "common/utils.h"            /* utils_hex_str_to_bytes */

#define LOG_TAG "TCP_OPS"
#define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

/* Exported by the kernel but not declared in lwIP headers */
extern void lwip_freeaddrinfo(struct addrinfo *ai);

/*===============================================================
 * Constants
 *==============================================================*/
#define MAX_COMBINED_SIZE       512
#define MAX_MESSAGES            5      /* send-queue rows combined per send */
#define TCP_TASK_INTERVAL_MS    200U   /* must match tcp.c loop interval    */
#define RECONNECT_DELAY_CYCLES  40U    /* fallback: ~8s at 200ms interval   */
#define TCP_DNS_PORTSTR_SIZE    6

/*===============================================================
 * Helper Functions
 *==============================================================*/

/**
 * @brief Reset send tracking variables
 */
void tcp_reset_send_tracking(void)
{
    g_tcp_client.tcp_bytes_sent  = -1;
    g_tcp_client.tcp_bytes_acked = 0;
    g_tcp_client.tcp_send_queue_batch_count = 0;
}

/**
 * @brief Check if ACK has already been received (race condition protection)
 */
bool tcp_check_ack_received(void)
{
    return (g_tcp_client.tcp_bytes_sent > 0 &&
            g_tcp_client.tcp_bytes_acked >= g_tcp_client.tcp_bytes_sent);
}

/**
 * @brief Validate socket state before send operations
 * @return true if socket is valid for sending, false otherwise
 */
static bool tcp_validate_socket_for_send(void)
{
    int err;

    if (g_tcp_client.tcp_fd < 0) {
        LOG_WARN("TCP socket invalid (fd=%d) for send operation",
                        g_tcp_client.tcp_fd);
        return false;
    }

    err = sdk_tcp_get_sock_errno(g_tcp_client.tcp_fd);
    if (err == ENOTCONN || err == ECONNRESET || err == ECONNABORTED || err == EPIPE) {
        LOG_ERROR("TCP socket in error state (errno=%d) for send operation", err);
        return false;
    }
    return true;
}

/**
 * @brief Send TCP data with comprehensive error checking
 * @return Bytes sent (>0), 0 if buffer full, -1 on error
 */
static int tcp_send_data(int sockfd, const void *buffer, int size)
{
    int send_result;

    if (sockfd < 0 || !buffer || size <= 0) {
        LOG_WARN("TCP [SEND] invalid parameters (fd=%d, size=%d)",
                        sockfd, size);
        return -1;
    }

    send_result = sdk_tcp_send(sockfd, buffer, (unsigned int)size, 0);

    if (send_result < 0) {
        LOG_ERROR("TCP [SEND] send failed (result=%d, errno=%d)",
                      send_result, sdk_tcp_get_sock_errno(sockfd));
        /* Reset send tracking on error - callback won't fire for failed sends */
        tcp_reset_send_tracking();
        return -1;
    }
    if (send_result == 0) {
        /* Buffer full - callback will notify when ready (SENDPLUS event) */
        LOG_DEBUG("TCP [SEND] send buffer full, waiting for SENDPLUS");
        return 0;
    }

    /* Send succeeded - update tracking, callback will notify when ACKed */
    LOG_DEBUG("TCP [SEND] sent %d/%d bytes", send_result, size);
    g_tcp_client.tcp_bytes_sent  = send_result;
    g_tcp_client.tcp_bytes_acked = 0;

    return send_result;
}

/**
 * @brief Close TCP socket
 */
static int tcp_close_socket(int fd)
{
    if (fd < 0)
        return 0;   /* already closed */
    return sdk_tcp_close(fd);
}

/*===============================================================
 * Send-queue helpers (reference tcp_ops_lwip.c)
 *==============================================================*/
static const ModuleConfig *tcp_get_module_config(void)
{
    return module_manager_get_config(MODULE_ID_TCP);
}

static void tcp_free_module_msg_dynamic(ModuleMessage *m)
{
    if (!m)
        return;
    if (m->use_dynamic_buffer && m->dynamic_buffer) {
        free(m->dynamic_buffer);
        m->dynamic_buffer = NULL;
        m->use_dynamic_buffer = FALSE;
        m->data_len = 0;
    }
}

/** Remove n elements from head (same order as peek_batch) and free dynamic payloads. */
static void tcp_pop_send_queue_batch_and_free(const ModuleConfig *tcp_config, UINT32 n)
{
    ModuleMessage sink[MAX_MESSAGES];
    UINT32 popped = 0;

    if (n == 0U || !tcp_config || !tcp_config->msg_q)
        return;
    if (n > MAX_MESSAGES)
        n = MAX_MESSAGES;

    if (queue_pop(tcp_config->msg_q, &tcp_config->msg_q_config,
                  sink, n, &popped) != RESULT_SUCCESS) {
        LOG_WARN("TCP [STATE] queue_pop after send failed (expected %u elements)",
                        (unsigned)n);
        return;
    }
    for (UINT32 i = 0; i < popped; i++)
        tcp_free_module_msg_dynamic(&sink[i]);
}

/**
 * @c live-first: when the send queue is full, pop the newest element (tail)
 * and prepend it to @a buf so live data goes out before backlog. On failure
 * the tail element is restored; dynamic payload is always released.
 */
static int tcp_prepend_live_first_queue_tail(const ModuleConfig *tcp_config,
                                             char *buf, int used_bytes, int buf_max)
{
    ModuleMessage tail_msg;
    const char   *payload;
    size_t        payload_len;
    int           space;

    if (!tcp_config || !tcp_config->msg_q || !buf || used_bytes <= 0 || buf_max <= 0)
        return used_bytes;
    if (!g_tcp_client.config || !g_tcp_client.config->live_first)
        return used_bytes;
    if (!queue_is_full(tcp_config->msg_q, &tcp_config->msg_q_config))
        return used_bytes;

    memset(&tail_msg, 0, sizeof(tail_msg));
    if (queue_pop_tail(tcp_config->msg_q, &tcp_config->msg_q_config,
                       &tail_msg) != RESULT_SUCCESS)
        return used_bytes;

    payload     = module_message_payload_ptr(&tail_msg);
    payload_len = module_message_payload_len(&tail_msg);
    space       = buf_max - used_bytes;
    if (!payload || payload_len == 0u || payload_len > (size_t)space) {
        (void)queue_push(tcp_config->msg_q, &tcp_config->msg_q_config, &tail_msg);
        tcp_free_module_msg_dynamic(&tail_msg);
        return used_bytes;
    }

    memmove(buf + payload_len, buf, (size_t)used_bytes);
    memcpy(buf, payload, payload_len);
    tcp_free_module_msg_dynamic(&tail_msg);
    LOG_INFO("TCP [STATE] SENDING_DATA: prepended tail (%u B) before batch (%d B)",
                 (unsigned)payload_len, used_bytes);
    return used_bytes + (int)payload_len;
}

/** Combine send-queue rows using each row's @c data_len (module_message_payload_len). */
static int tcp_combine_messages(ModuleMessage *msgs, UINT32 count, char *buffer, int max_size)
{
    int total_bytes        = 0;
    int messages_processed = 0;
    int invalid_count      = 0;

    for (UINT32 i = 0; i < count && messages_processed < MAX_MESSAGES; i++) {
        ModuleMessage *msg     = &msgs[i];
        const char    *payload = module_message_payload_ptr(msg);

        if (!payload) {
            invalid_count++;
            continue;
        }

        size_t plen = module_message_payload_len(msg);
        if (plen == 0u) {
            invalid_count++;
            continue;
        }
        if (plen > (size_t)max_size) {
            invalid_count++;
            LOG_DEBUG("TCP [COMBINE] skip source=%d (len=%u > max=%d)",
                            (int)msg->source_module, (unsigned)plen, max_size);
            continue;
        }
        int pkt_len = (int)plen;

        /* Check if we have space */
        int remaining_space = max_size - total_bytes;
        if (remaining_space <= 0)
            break;
        int bytes_to_copy = (pkt_len < remaining_space) ? pkt_len : remaining_space;
        memcpy(buffer + total_bytes, payload, (size_t)bytes_to_copy);
        total_bytes += bytes_to_copy;
        messages_processed++;

        if (total_bytes >= max_size)
            break;
    }

    if (invalid_count > 0)
        LOG_DEBUG("TCP [COMBINE] skipped %d invalid packet(s) of %u",
                        invalid_count, (unsigned)count);
    if (messages_processed > 0)
        LOG_DEBUG("TCP [COMBINE] combined %d message(s) into %d bytes",
                        messages_processed, total_bytes);

    return total_bytes;
}

/*===============================================================
 * TCP State Handlers - lwIP based
 *==============================================================*/

Result tcp_state_handle_wait_network(void)
{
    if (weware_network_is_connected()) {
        LOG_DEBUG("TCP network ready");
        return RESULT_SUCCESS;
    }
    return RESULT_BUSY;
}

Result tcp_state_handle_socket_creating(void)
{
    g_tcp_client.tcp_fd = sdk_tcp_socket_create_with_callback(
        AF_INET, SOCK_STREAM, IPPROTO_TCP, tcp_socket_event_callback);
    if (g_tcp_client.tcp_fd < 0) {
        LOG_ERROR("TCP socket create failed");
        return RESULT_ERROR;
    }

    /* Walnut: the backend already made the socket non-blocking (FIONBIO);
     * the reference's SO_NONBLOCK setsockopt does not exist here. */

    LOG_DEBUG("TCP socket created fd=%d", g_tcp_client.tcp_fd);
    return RESULT_SUCCESS;
}

Result tcp_state_handle_socket_created(void)
{
    struct addrinfo  hints;
    struct addrinfo *result = NULL;
    char             portstr[TCP_DNS_PORTSTR_SIZE];
    int              ret;

    if (!g_tcp_client.config) {
        LOG_ERROR("TCP [STATE] SOCKET_CREATED: config not available");
        return RESULT_ERROR;
    }

    LOG_INFO("TCP [STATE] SOCKET_CREATED: connecting to %s:%u",
                 g_tcp_client.config->server_ip,
                 (unsigned)g_tcp_client.config->server_port);

    /* Use getaddrinfo for DNS resolution (kernel-format result feeds the
     * backend's connect directly) */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    snprintf(portstr, sizeof(portstr), "%u",
             (unsigned)g_tcp_client.config->server_port);

    ret = lwip_getaddrinfo(g_tcp_client.config->server_ip, portstr,
                           &hints, &result);
    if (ret != 0 || result == NULL) {
        LOG_ERROR("TCP [STATE] SOCKET_CREATED: getaddrinfo failed for %s:%u (ret=%d)",
                      g_tcp_client.config->server_ip,
                      (unsigned)g_tcp_client.config->server_port, ret);
        return RESULT_ERROR;
    }

    /* Try to connect - non-blocking, so EINPROGRESS is the expected answer */
    ret = sdk_tcp_connect(g_tcp_client.tcp_fd, result->ai_addr,
                          (unsigned int)result->ai_addrlen);

    lwip_freeaddrinfo(result);

    if (ret < 0) {
        int err = sdk_tcp_get_errno();
        if (err == 0)
            err = sdk_tcp_get_sock_errno(g_tcp_client.tcp_fd);

        /* EINPROGRESS means connection is in progress - expected for
         * non-blocking sockets */
        if (err == EINPROGRESS || err == 115 || err == EAGAIN || err == 11) {
            LOG_DEBUG("TCP [STATE] SOCKET_CREATED: connection in progress (errno=%d)", err);
            return RESULT_SUCCESS;  /* Transition to CONNECTING */
        }
        LOG_ERROR("TCP [STATE] SOCKET_CREATED: connect failed immediately, errno=%d", err);
        return RESULT_ERROR;
    }

    /* Connected immediately */
    LOG_INFO("TCP [STATE] SOCKET_CREATED: connected immediately");
    return RESULT_SUCCESS;  /* Transition to CONNECTING (callback confirms) */
}

Result tcp_state_handle_connecting(void)
{
    /* Validate socket state */
    if (!tcp_validate_socket_for_send()) {
        LOG_WARN("TCP [STATE] CONNECTING: socket invalid, transitioning to ERROR");
        return RESULT_ERROR;
    }

    return RESULT_BUSY;  /* Wait for connection callback */
}

Result tcp_state_handle_connected(void)
{
    /* Broadcast CONNECTED only on the edge (reference parity; module_manager
     * tracks TCP connected state through this event) */
    if (!g_tcp_client.last_connected_state) {
        LOG_INFO("TCP [STATE] CONNECTED: connection established, broadcasting event");
        event_manager_broadcast(EVENT_TCP_CONNECTED, "TCP Client", NULL, 0);
        g_tcp_client.last_connected_state = true;
    }

    return RESULT_SUCCESS;  /* Transition to SENDING_LOGIN */
}

Result tcp_state_handle_sending_login(void)
{
    int packet_size;
    int bytes_sent;

    /* Validate socket before sending */
    if (!tcp_validate_socket_for_send()) {
        LOG_WARN("TCP [STATE] SENDING_LOGIN: socket invalid, transitioning to ERROR");
        return RESULT_ERROR;
    }

    packet_size = login_packet_create(g_login_packet_buffer,
                                      sizeof(g_login_packet_buffer));
    if (packet_size != LOGIN_PACKET_TOTAL_SIZE) {
        /* Walnut: the usual cause is the IMEI cache not being ready yet -
         * retry (the reference errors out; its IMEI is guaranteed by then).
         * The SENDING_LOGIN state timeout escalates if it never appears. */
        LOG_WARN("TCP [STATE] SENDING_LOGIN: failed to create login packet (size: %d, expected: %d)",
                        packet_size, LOGIN_PACKET_TOTAL_SIZE);
        return RESULT_BUSY;
    }

    /* loginwithgps: append the last-valid GPS packet after the login block
     * (reference parity; buffer sized LOGIN + GPS = 79 bytes) */
    if (g_tcp_client.config && g_tcp_client.config->login_with_gps) {
        int gps_len = gps_ops_build_last_valid_position_for_tcp(
            g_login_packet_buffer + LOGIN_PACKET_TOTAL_SIZE,
            (int)(sizeof(g_login_packet_buffer) - (size_t)LOGIN_PACKET_TOTAL_SIZE));
        if (gps_len != GPS_PACKET_TOTAL_SIZE) {
            LOG_WARN("TCP [STATE] SENDING_LOGIN: loginwithgps enabled but last-valid GPS build failed (%d), login only",
                            gps_len);
        } else {
            packet_size = LOGIN_PACKET_TOTAL_SIZE + GPS_PACKET_TOTAL_SIZE;
            LOG_DEBUG("TCP [STATE] SENDING_LOGIN: appended last-valid GPS (%d bytes), total %d",
                            gps_len, packet_size);
        }
    }

    bytes_sent = tcp_send_data(g_tcp_client.tcp_fd, g_login_packet_buffer,
                               packet_size);
    if (bytes_sent > 0) {
        LOG_INFO("TCP [STATE] SENDING_LOGIN: login packet sent (%d bytes), waiting for ACK",
                     bytes_sent);
        return RESULT_SUCCESS;  /* Transition to WAIT_ACK_LOGIN */
    }
    if (bytes_sent == 0) {
        LOG_DEBUG("TCP [STATE] SENDING_LOGIN: send buffer full, waiting for SENDPLUS");
        return RESULT_BUSY;     /* Buffer full, wait and retry */
    }

    LOG_ERROR("TCP [STATE] SENDING_LOGIN: failed to send login packet");
    return RESULT_ERROR;
}

Result tcp_state_handle_wait_ack_login(void)
{
    int err;

    /* CRITICAL: check if ACK has already been received (race protection) */
    if (tcp_check_ack_received()) {
        LOG_INFO("TCP [STATE] WAIT_ACK_LOGIN: ACK received (sent=%d, acked=%d), transitioning to ACK_RECEIVED",
                     g_tcp_client.tcp_bytes_sent, g_tcp_client.tcp_bytes_acked);
        return RESULT_SUCCESS;  /* reset happens in ack_received */
    }

    /* Validate socket state */
    if (!tcp_validate_socket_for_send()) {
        LOG_WARN("TCP [STATE] WAIT_ACK_LOGIN: socket invalid, transitioning to ERROR");
        tcp_reset_send_tracking();
        return RESULT_ERROR;
    }

    /* Check socket error state */
    err = sdk_tcp_get_sock_errno(g_tcp_client.tcp_fd);
    if (err == ENOTCONN || err == ECONNRESET || err == ECONNABORTED || err == EPIPE) {
        LOG_WARN("TCP [STATE] WAIT_ACK_LOGIN: socket closed/reset (errno=%d), transitioning to ERROR", err);
        tcp_reset_send_tracking();
        return RESULT_ERROR;
    }
    if (err != 0 && err != EAGAIN && err != 11) {
        LOG_WARN("TCP [STATE] WAIT_ACK_LOGIN: socket error (errno=%d), transitioning to ERROR", err);
        tcp_reset_send_tracking();
        return RESULT_ERROR;
    }

    /* Validate that we have a pending send operation */
    if (g_tcp_client.tcp_bytes_sent <= 0) {
        LOG_WARN("TCP [STATE] WAIT_ACK_LOGIN: no pending send (tcp_bytes_sent=%d), transitioning to ERROR",
                        g_tcp_client.tcp_bytes_sent);
        tcp_reset_send_tracking();
        return RESULT_ERROR;
    }

    return RESULT_BUSY;  /* Wait for ACK callback */
}

Result tcp_state_handle_ack_received(void)
{
    const ModuleConfig *tcp_config = tcp_get_module_config();

    /* One place for post-ACK cleanup: task path or callback may reach here
     * first. Pop + free the peeked send-queue batch (rows delivered), then
     * clear the reboot-persist snapshot so a reboot does not resend them. */
    if (g_tcp_client.tcp_send_queue_batch_count > 0U)
        tcp_pop_send_queue_batch_and_free(tcp_config,
                                          g_tcp_client.tcp_send_queue_batch_count);

    if (tcp_config && tcp_config->msg_q)
        (void)queue_persist_clear_snapshot_after_consume(tcp_config->msg_q,
                                                         &tcp_config->msg_q_config);

    tcp_reset_send_tracking();

    if (!g_tcp_client.session_up) {
        g_tcp_client.session_up = true;
        LOG_INFO("TCP session ready");
    }

    LOG_INFO("TCP [STATE] ACK_RECEIVED: ACK confirmed, transitioning to SENDING_DATA");
    return RESULT_SUCCESS;  /* Transition to SENDING_DATA */
}

Result tcp_state_handle_sending_data(void)
{
    const ModuleConfig *tcp_config = tcp_get_module_config();
    ModuleMessage       module_msgs[MAX_MESSAGES];
    UINT32              peeked = 0;
    int                 total_bytes;
    int                 bytes_sent;

    if (!tcp_config || !tcp_config->msg_q ||
        tcp_config->msg_q_config.element_size == 0U)
        return RESULT_BUSY;     /* queue not available, wait */

    /* Validate socket before sending */
    if (!tcp_validate_socket_for_send()) {
        LOG_WARN("TCP [STATE] SENDING_DATA: socket invalid, transitioning to ERROR");
        return RESULT_ERROR;
    }

    /* Peek head batch: rows stay in the queue until ACK, then queue_pop
     * (unacked rows survive a reconnect AND a persisted reboot) */
    memset(module_msgs, 0, sizeof(module_msgs));
    if (queue_peek_batch(tcp_config->msg_q, &tcp_config->msg_q_config,
                         module_msgs, MAX_MESSAGES, &peeked) != RESULT_SUCCESS ||
        peeked == 0U)
        return RESULT_BUSY;     /* no messages available */

    total_bytes = tcp_combine_messages(module_msgs, peeked,
                                       g_data_packet_buffer, MAX_COMBINED_SIZE);
    if (total_bytes == 0) {
        tcp_pop_send_queue_batch_and_free(tcp_config, peeked);
        LOG_WARN("TCP [STATE] SENDING_DATA: no valid messages to send, dropped peeked batch");
        return RESULT_BUSY;
    }

    total_bytes = tcp_prepend_live_first_queue_tail(
        tcp_config, g_data_packet_buffer, total_bytes, MAX_COMBINED_SIZE);

    bytes_sent = tcp_send_data(g_tcp_client.tcp_fd, g_data_packet_buffer,
                               total_bytes);
    if (bytes_sent > 0) {
        g_tcp_client.tcp_send_queue_batch_count = peeked;
        LOG_INFO("TCP [STATE] SENDING_DATA: data packet sent (%d bytes, %u rows), waiting for ACK",
                     bytes_sent, (unsigned)peeked);
        return RESULT_SUCCESS;  /* Transition to WAIT_ACK_DATA */
    }
    if (bytes_sent == 0) {
        LOG_DEBUG("TCP [STATE] SENDING_DATA: send buffer full, waiting for SENDPLUS");
        return RESULT_BUSY;
    }

    /* Rows stay in the queue (popped only on ACK) - resent after reconnect */
    LOG_ERROR("TCP [STATE] SENDING_DATA: failed to send data packet");
    return RESULT_ERROR;
}

Result tcp_state_handle_wait_ack_data(void)
{
    int err;

    /* CRITICAL: check if ACK has already been received (race protection) */
    if (tcp_check_ack_received()) {
        LOG_INFO("TCP [STATE] WAIT_ACK_DATA: ACK received (sent=%d, acked=%d), transitioning to ACK_RECEIVED",
                     g_tcp_client.tcp_bytes_sent, g_tcp_client.tcp_bytes_acked);
        return RESULT_SUCCESS;  /* queue pop + reset in ack_received */
    }

    /* Validate socket state */
    if (!tcp_validate_socket_for_send()) {
        LOG_WARN("TCP [STATE] WAIT_ACK_DATA: socket invalid, transitioning to ERROR");
        tcp_reset_send_tracking();
        return RESULT_ERROR;
    }

    /* Check socket error state */
    err = sdk_tcp_get_sock_errno(g_tcp_client.tcp_fd);
    if (err == ENOTCONN || err == ECONNRESET || err == ECONNABORTED || err == EPIPE) {
        LOG_WARN("TCP [STATE] WAIT_ACK_DATA: socket closed/reset (errno=%d), transitioning to ERROR", err);
        tcp_reset_send_tracking();
        return RESULT_ERROR;
    }
    if (err != 0 && err != EAGAIN && err != 11) {
        LOG_WARN("TCP [STATE] WAIT_ACK_DATA: socket error (errno=%d), transitioning to ERROR", err);
        tcp_reset_send_tracking();
        return RESULT_ERROR;
    }

    /* Validate that we have a pending send operation */
    if (g_tcp_client.tcp_bytes_sent <= 0) {
        LOG_WARN("TCP [STATE] WAIT_ACK_DATA: no pending send (tcp_bytes_sent=%d), transitioning to ERROR",
                        g_tcp_client.tcp_bytes_sent);
        tcp_reset_send_tracking();
        return RESULT_ERROR;
    }

    return RESULT_BUSY;  /* Wait for ACK callback */
}

Result tcp_state_handle_receiving(void)
{
    return RESULT_BUSY;  /* Receiving data from server */
}

Result tcp_state_handle_closed(void)
{
    LOG_INFO("TCP [STATE] CLOSED: closing connection");

    /* Reset send tracking */
    tcp_reset_send_tracking();

    /* Broadcast DISCONNECTED only on the edge (reference parity) */
    if (g_tcp_client.last_connected_state) {
        LOG_INFO("TCP [STATE] CLOSED: broadcasting DISCONNECTED event");
        event_manager_broadcast(EVENT_TCP_DISCONNECTED, "TCP Client", NULL, 0);
        g_tcp_client.last_connected_state = false;
    }
    g_tcp_client.session_up   = false;
    g_tcp_client.recv_pending = 0;
    /* Unacked send-queue rows stay queued (popped only on ACK) and are
     * resent after the reconnect */

    /* Close socket immediately (shutdown before close) */
    if (g_tcp_client.tcp_fd >= 0) {
        int fd = g_tcp_client.tcp_fd;
        lwip_shutdown(fd, SHUT_RDWR);
        tcp_close_socket(fd);
        g_tcp_client.tcp_fd = -1;
    }

    /* Initialize reconnect delay counter */
    g_tcp_client.reconnect_delay_cycles = 0;

    LOG_DEBUG("TCP socket closed");
    return RESULT_SUCCESS;
}

Result tcp_state_handle_error(void)
{
    LOG_ERROR("TCP error state");

    /* Close socket immediately if still open */
    if (g_tcp_client.tcp_fd >= 0) {
        int fd = g_tcp_client.tcp_fd;
        lwip_shutdown(fd, SHUT_RDWR);
        tcp_close_socket(fd);
        g_tcp_client.tcp_fd = -1;
    }

    /* Reset send tracking */
    tcp_reset_send_tracking();

    /* Initialize reconnect delay counter */
    g_tcp_client.reconnect_delay_cycles = 0;

    LOG_DEBUG("TCP recovering from error");
    return RESULT_SUCCESS;
}

Result tcp_state_handle_reconnect_delay(void)
{
    /* Cycle-counted like the reference; the target honours the configured
     * retry interval when available. */
    UINT32 target_cycles = RECONNECT_DELAY_CYCLES;

    if (g_tcp_client.config && g_tcp_client.config->retry_interval_ms > 0U)
        target_cycles = g_tcp_client.config->retry_interval_ms / TCP_TASK_INTERVAL_MS;
    if (target_cycles == 0U)
        target_cycles = 1U;

    g_tcp_client.reconnect_delay_cycles++;

    if (g_tcp_client.reconnect_delay_cycles >= target_cycles) {
        LOG_DEBUG("TCP reconnect delay done");
        g_tcp_client.reconnect_delay_cycles = 0;
        return RESULT_SUCCESS;
    }

    return RESULT_BUSY;
}

/*---------------------------------------------------------------
 * Command/response packet builders (reference: tcp_ops_lwip.c)
 *--------------------------------------------------------------*/
#define BLE_CMD_RESP_TCP_PACKET_TYPE 39

/* Max type-38/39 payload that fits a ModuleMessage inline buffer */
#define TCP_CMD_RESP_PAYLOAD_MAX ((int)sizeof(((ModuleMessage*)0)->message) - TCP_CMD_RESP_HEADER_SIZE)  /* 249 */

int tcp_command_response_packet_create(char *out, int out_size, const char *payload, int payload_len)
{
    if (!out || !payload)
        return 0;
    if (payload_len < 0)
        payload_len = (int)strlen(payload);
    if (payload_len > TCP_CMD_RESP_PAYLOAD_MAX)
        payload_len = TCP_CMD_RESP_PAYLOAD_MAX;
    int total = TCP_CMD_RESP_HEADER_SIZE + payload_len;
    if (out_size < total)
        return 0;
    out[0] = (char)GPS_PACKET_START_ID1;
    out[1] = (char)GPS_PACKET_START_ID2;
    out[2] = (char)TCP_CMD_RESP_PACKET_TYPE;
    out[3] = (char)(payload_len & 0xFF);
    out[4] = (char)((payload_len >> 8) & 0xFF);
    memcpy(out + 5, payload, (size_t)payload_len);
    out[total - 2] = (char)GPS_PACKET_STOP_ID1;
    out[total - 1] = (char)GPS_PACKET_STOP_ID2;
    return total;
}

int tcp_ble_cmd_response_packet_create(char *out, int out_size, const char *hex_str, int hex_len)
{
    if (!out || !hex_str)
        return 0;
    if (hex_len < 0)
        hex_len = (int)strlen(hex_str);
    if (hex_len <= 0 || out_size <= TCP_CMD_RESP_HEADER_SIZE)
        return 0;

    /* Decode hex directly into the packet body (out+5), bounded by the payload cap. */
    size_t bin_cap = (size_t)(out_size - TCP_CMD_RESP_HEADER_SIZE);
    if (bin_cap > (size_t)TCP_CMD_RESP_PAYLOAD_MAX)
        bin_cap = (size_t)TCP_CMD_RESP_PAYLOAD_MAX;

    size_t bin_len = 0;
    if (!utils_hex_str_to_bytes(hex_str, (size_t)hex_len, out + 5, bin_cap, &bin_len) || bin_len == 0)
        return 0;   /* malformed/empty hex -> caller drops + logs */

    int total = TCP_CMD_RESP_HEADER_SIZE + (int)bin_len;
    out[0] = (char)GPS_PACKET_START_ID1;
    out[1] = (char)GPS_PACKET_START_ID2;
    out[2] = (char)BLE_CMD_RESP_TCP_PACKET_TYPE;   /* 39 */
    out[3] = (char)(bin_len & 0xFF);
    out[4] = (char)((bin_len >> 8) & 0xFF);
    /* binary payload already written at out[5..] by the decoder */
    out[total - 2] = (char)GPS_PACKET_STOP_ID1;
    out[total - 1] = (char)GPS_PACKET_STOP_ID2;
    return total;
}
