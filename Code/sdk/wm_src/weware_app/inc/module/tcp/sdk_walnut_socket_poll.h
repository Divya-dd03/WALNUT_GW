/**
 * @file sdk_walnut_socket_poll.h
 * @brief Walnut app-space socket_with_callback via lwip_socket() + select()
 *        polling (the walnut counterpart of sdk_quectel_socket_poll).
 */

#ifndef SDK_WALNUT_SOCKET_POLL_H
#define SDK_WALNUT_SOCKET_POLL_H

#include "tcp/sdk_functionality_tcp.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a non-blocking TCP socket, register @p callback for it and start
 * the TCPMON poll task (first call). Returns fd or -1.
 */
int sdk_walnut_socket_with_callback(int domain, int type, int protocol,
                                    SdkTcpEventCallback callback);

/** Deregister @p fd - no further events are delivered for it. */
void sdk_walnut_socket_poll_release(int fd);

/** Arm CONNECTED/ERROR detection for an in-flight non-blocking connect. */
void sdk_walnut_socket_poll_mark_connecting(int fd);

/**
 * Note @p len bytes accepted by the stack on @p fd: arms SENDACKED
 * detection via SO_SNDBUF drain (real peer TCP acks) when the kernel
 * supports it, otherwise emits SENDACKED immediately.
 */
void sdk_walnut_socket_poll_note_sent(int fd, int len);

#ifdef __cplusplus
}
#endif

#endif /* SDK_WALNUT_SOCKET_POLL_H */
