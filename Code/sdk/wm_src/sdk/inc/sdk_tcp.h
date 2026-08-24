/**
 ******************************************************************************
 * @file    sdk_tcp.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - TCP (lwIP / BSD sockets) API.
 *
 *          NOTE: these follow BSD socket conventions, NOT SdkResult:
 *          >= 0 on success, < 0 on error.
 *
 *          A data path must be up before any of this can succeed - see
 *          sdk_network.h, and sdk_network_get_network_status() for the combined
 *          "registered and PDP active" check.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __SDK_TCP_H__
#define __SDK_TCP_H__

#include "sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Create a TCP socket and register an async event callback.
 *
 *         With @p callback non-NULL the socket becomes event-driven: the SDK
 *         adds it to an internal registry watched by the "TCPMON" task, which
 *         reports SDK_TCP_EVENT_CONNECT / _RECV / _CLOSE / _ERROR. Only a fixed
 *         number of such sockets exist; creation fails once they are all in use.
 *         With @p callback NULL an ordinary blocking socket is returned and
 *         nothing is monitored.
 *
 * @note   The callback runs on the "TCPMON" task, not on the caller's - keep the
 *         handler short, as with the sdk_gps fix callback. Its @c arg argument is
 *         always NULL: this create call takes no user pointer, so key any
 *         per-connection context off the fd.
 *
 * @note   After SDK_TCP_EVENT_RECV no further RECV is reported for that socket
 *         until sdk_tcp_recv() is called on it.
 *
 * @param  af        address family (e.g. AF_INET).
 * @param  type      socket type (e.g. SOCK_STREAM).
 * @param  protocol  protocol (0 = default TCP).
 * @param  callback  fired on socket events, or NULL for a plain socket.
 * @return socket fd (>= 0) on success; < 0 on error.
 */
int sdk_tcp_socket_create_with_callback(int af, int type, int protocol,
                                        SdkTcpSocketCallback callback);

/**
 * @brief  Connect a socket to a remote address.
 *
 *         @p addr is a BSD @c struct sockaddr (@c sockaddr_in for AF_INET).
 *         Building one requires the lwIP headers, so prefer
 *         sdk_tcp_connect_host() unless the address is already in hand.
 *
 * @note   On an event-driven socket this does not block: it returns 0 once the
 *         attempt is under way, and the outcome arrives later as
 *         SDK_TCP_EVENT_CONNECT or SDK_TCP_EVENT_ERROR.
 *
 * @return 0 on success; < 0 on error.
 */
int sdk_tcp_connect(int fd, const void *addr, unsigned int addrlen);

/**
 * @brief  Resolve @p host and connect @p fd to it, retrying DNS.
 *
 *         Resolves to IPv4 and connects to the first answer. DNS commonly fails
 *         for the first second or two after the PDP context comes up, so the
 *         lookup is retried until it succeeds or @p timeout_ms elapses.
 *
 * @note   Resolution always blocks the caller (there is no async form). The
 *         connect stage then behaves as sdk_tcp_connect() above, so on an
 *         event-driven socket this can return 0 with the connection still
 *         pending.
 *
 * @note   On failure the socket may be unusable: close it and retry
 *         create+connect rather than calling this again on the same fd.
 *
 * @param  fd          socket from sdk_tcp_socket_create_with_callback().
 * @param  host        hostname or dotted-decimal address.
 * @param  port        remote TCP port.
 * @param  timeout_ms  budget for name resolution.
 * @return 0 on success; < 0 on error.
 */
int sdk_tcp_connect_host(int fd, const char *host, UINT16 port, UINT32 timeout_ms);

/**
 * @brief  Send bytes on a connected socket.
 *
 *         Bounded by the send timeout applied at creation, so a stalled peer
 *         cannot block the calling task indefinitely.
 *
 * @return bytes sent (>= 0); < 0 on error.
 */
int sdk_tcp_send(int fd, const void *buf, unsigned int len, int flags);

/**
 * @brief  Receive bytes from a socket.
 *
 *         Bounded by the receive timeout applied at creation. On an event-driven
 *         socket this also re-arms SDK_TCP_EVENT_RECV.
 *
 * @return bytes received (>0); 0 = peer closed; < 0 on error.
 */
int sdk_tcp_recv(int fd, void *buf, unsigned int len, int flags);

/**
 * @brief  Close a socket and release its resources.
 *
 *         Also removes the socket from the event registry, so no further
 *         callback is delivered for it. Sockets reported as CLOSE or ERROR still
 *         have to be closed here.
 *
 * @return 0 on success; < 0 on error.
 */
int sdk_tcp_close(int fd);

/**
 * @brief  Get the pending error (SO_ERROR) for a specific socket.
 * @return errno value for that socket.
 */
int sdk_tcp_get_sock_errno(int fd);

#ifdef __cplusplus
}
#endif

#endif /* __SDK_TCP_H__ */
