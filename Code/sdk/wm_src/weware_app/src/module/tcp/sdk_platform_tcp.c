/**
 * @file sdk_platform_tcp.c
 * @brief TCP functionality dispatcher - forwards to the registered SDK
 *        implementation.
 *
 *        The sdk_tcp_* symbols defined here shadow the kernel wrappers in
 *        lib_wmsrc_B.a (see sdk_functionality_tcp.h). Walnut is the only
 *        backend on this board, so it is also the lazy default - callers
 *        that run before weware_tcp_init() (e.g. the demo menu) still work;
 *        sdk_platform_register_tcp_ops() can override at any time.
 *
 *        Deliberately includes NO lwIP or platform headers: the ops-struct
 *        member names (send/recv/close/connect) would otherwise be mangled
 *        by the lwIP compat function-like macros.
 */

#include <stddef.h>

#include "tcp/sdk_functionality_tcp.h"

static const SdkTcpFunctionalityOps *g_tcp_ops = NULL;

void sdk_platform_register_tcp_ops(const SdkTcpFunctionalityOps *ops)
{
    g_tcp_ops = ops;
}

static const SdkTcpFunctionalityOps *tcp_ops(void)
{
    if (!g_tcp_ops)
        g_tcp_ops = sdk_walnut_get_tcp_functionality_ops();
    return g_tcp_ops;
}

int sdk_tcp_socket_create_with_callback(int af, int type, int protocol,
                                        SdkTcpEventCallback callback)
{
    const SdkTcpFunctionalityOps *ops = tcp_ops();
    if (!ops || !ops->socket_create_with_callback)
        return -1;
    return ops->socket_create_with_callback(af, type, protocol, callback);
}

int sdk_tcp_connect(int fd, const void *addr, unsigned int addrlen)
{
    const SdkTcpFunctionalityOps *ops = tcp_ops();
    if (!ops || !ops->connect)
        return -1;
    return ops->connect(fd, addr, addrlen);
}

int sdk_tcp_send(int fd, const void *buf, unsigned int len, int flags)
{
    const SdkTcpFunctionalityOps *ops = tcp_ops();
    if (!ops || !ops->send)
        return -1;
    return ops->send(fd, buf, len, flags);
}

int sdk_tcp_recv(int fd, void *buf, unsigned int len, int flags)
{
    const SdkTcpFunctionalityOps *ops = tcp_ops();
    if (!ops || !ops->recv)
        return -1;
    return ops->recv(fd, buf, len, flags);
}

int sdk_tcp_close(int fd)
{
    const SdkTcpFunctionalityOps *ops = tcp_ops();
    if (!ops || !ops->close)
        return -1;
    return ops->close(fd);
}

int sdk_tcp_get_sock_errno(int fd)
{
    const SdkTcpFunctionalityOps *ops = tcp_ops();
    if (!ops || !ops->get_sock_errno)
        return 0;
    return ops->get_sock_errno(fd);
}

int sdk_tcp_get_errno(void)
{
    const SdkTcpFunctionalityOps *ops = tcp_ops();
    if (!ops || !ops->get_errno)
        return 0;
    return ops->get_errno();
}
