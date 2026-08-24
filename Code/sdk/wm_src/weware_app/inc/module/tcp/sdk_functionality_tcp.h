/**
 * @file sdk_functionality_tcp.h
 * @brief TCP/socket functionality API - create with callback, connect, send,
 *        recv, close.
 *
 * Firmware uses this API for the TCP client; implementations are per-SDK
 * (SIMCOM, Quectel, WALNUT). Address is passed as void* + length to avoid
 * pulling in socket headers here.
 *
 * WALNUT notes:
 *  - The sdk_tcp_* names below are real functions and deliberately SHADOW
 *    the kernel wrappers in lib_wmsrc_B.a (sdk_tcp.c.obj): because this
 *    module also defines sdk_tcp_connect_host, no symbol of that archive
 *    member is ever undefined, the member is never pulled in and no
 *    duplicate-definition occurs. Consequence: EVERY sdk_tcp_* caller in
 *    the image (including the wm_ui_tcp demo) binds to this abstraction
 *    and receives SDK_NETCONN_EVT_* event codes, not the kernel's
 *    SDK_TCP_EVENT_* codes. Never include wm_src/sdk/inc/sdk_tcp.h in the
 *    same file as this header (conflicting prototypes).
 *  - The callback typedef is SdkTcpEventCallback: sdk_types.h (included
 *    everywhere via the sdk headers) owns the name SdkTcpSocketCallback for
 *    the kernel's incompatible 4-event variant.
 */

#ifndef SDK_FUNCTIONALITY_TCP_H
#define SDK_FUNCTIONALITY_TCP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Standard socket constants so firmware does not need socket headers */
#ifndef AF_INET
#define AF_INET       2
#endif
#ifndef SOCK_STREAM
#define SOCK_STREAM   1
#endif
#ifndef IPPROTO_TCP
#define IPPROTO_TCP   6
#endif

/** Callback for socket events (e.g. CONNECTED, SENDACKED, RCVPLUS, close). */
typedef void (*SdkTcpEventCallback)(int sock, int evt, unsigned short len);

/**
 * TCP/socket functionality operations - implemented by each SDK.
 * Standard socket semantics: fd < 0 on failure; connect/send/recv/close
 * return -1 on error; send returns 0 when the send buffer is full.
 */
typedef struct SdkTcpFunctionalityOps {
    /** Create TCP socket with event callback. Returns fd or -1. */
    int (*socket_create_with_callback)(int af, int type, int protocol,
                                       SdkTcpEventCallback callback);
    /** Connect socket. addr is struct sockaddr*, addrlen in bytes. */
    int (*connect)(int fd, const void *addr, unsigned int addrlen);
    /** Send data. Returns bytes sent, 0 if buffer full, or -1. */
    int (*send)(int fd, const void *buf, unsigned int len, int flags);
    /** Receive data. Returns bytes received, 0 if closed, -1 on error. */
    int (*recv)(int fd, void *buf, unsigned int len, int flags);
    /** Close socket. Returns 0 on success, -1 on error. */
    int (*close)(int fd);
    /** Get socket error number for fd (e.g. getsockerrno). */
    int (*get_sock_errno)(int fd);
    /** Get last socket/network errno (e.g. for connect). */
    int (*get_errno)(void);
} SdkTcpFunctionalityOps;

void sdk_platform_register_tcp_ops(const SdkTcpFunctionalityOps *ops);

/** Walnut backend ops table (sdk_walnut_tcp.c) - pass to the register call. */
const SdkTcpFunctionalityOps *sdk_walnut_get_tcp_functionality_ops(void);

/* --- Public API used by firmware --- */

int sdk_tcp_socket_create_with_callback(int af, int type, int protocol,
                                        SdkTcpEventCallback callback);
int sdk_tcp_connect(int fd, const void *addr, unsigned int addrlen);
int sdk_tcp_send(int fd, const void *buf, unsigned int len, int flags);
int sdk_tcp_recv(int fd, void *buf, unsigned int len, int flags);
int sdk_tcp_close(int fd);
int sdk_tcp_get_sock_errno(int fd);
int sdk_tcp_get_errno(void);

/**
 * Resolve @p host (retrying DNS until @p timeout_ms elapses) and start a
 * connect on @p fd. Returns 0 once the attempt is under way (outcome
 * arrives as a CONNECTED or ERROR_* event); < 0 on error.
 * Walnut: this definition is what shadows the kernel's sdk_tcp.c.obj -
 * it must stay in the image even if the weware TCP module stops using it.
 */
int sdk_tcp_connect_host(int fd, const char *host, unsigned short port,
                         unsigned int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* SDK_FUNCTIONALITY_TCP_H */
