/**
 * @file sdk_walnut_tcp.c
 * @brief WALNUT TCP functionality - wraps the kernel-exported lwIP socket
 *        API (event delivery via sdk_walnut_socket_poll).
 *
 *        Platform quirks handled here so consumers stay portable:
 *        - kernel lwIP uses the BSD sa_len sockaddr layout: a portable
 *          sockaddr with u16 family == AF_INET(2) is rewritten to 0x0210
 *          ({len 16, family 2}); kernel-format addresses (e.g. straight
 *          from lwip_getaddrinfo) pass through untouched;
 *        - a full send buffer surfaces as -1/EWOULDBLOCK from lwip_send:
 *          mapped to the abstraction's "0 = buffer full" convention;
 *        - direct lwip_* calls do not set the libc errno, so get_errno
 *          returns 0 and callers fall through to get_sock_errno (the
 *          reference error cascade already does exactly that).
 */

#include <string.h>
#include <stdio.h>

/* lwIP socket layer (kernel-format structs + the errno values that
 * lwip_getsockerrno returns; do NOT add newlib <errno.h>) */
#include "lwip/sockets.h"
#include "lwip/netdb.h"

// sdk
#include "wm_global.h"
#include "wm_sdk_os.h"
#include "wm_sdk_log.h"

// app
#include "tcp/sdk_functionality_tcp.h"
#include "tcp/sdk_walnut_socket_poll.h"

#define LOG_TAG "TCP_DRV"
#define LOG_MODULE_LEVEL LOG_LEVEL_DEBUG
#include "module/log/log.h"

/* Exported by the kernel (core_stub.o) but not declared in lwIP headers */
extern int lwip_getsockerrno(int s);

static int socket_create_impl(int af, int type, int protocol,
                              SdkTcpEventCallback callback)
{
    (void)af;
    (void)type;
    (void)protocol;
    if (!callback)
        return -1;
    return sdk_walnut_socket_with_callback(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                                           callback);
}

static int connect_impl(int fd, const void *addr, unsigned int addrlen)
{
    struct sockaddr kaddr;
    int             ret;

    if (!addr || addrlen == 0U || addrlen > sizeof(kaddr))
        return -1;

    /* Kernel lwIP expects the BSD sa_len layout ({len u8, family u8}).
     * A portable sockaddr carries u16 family == AF_INET(2): rewrite it to
     * 0x0210. Kernel-format input (e.g. from lwip_getaddrinfo) already
     * reads 0x0210 here and passes through unchanged. */
    memcpy(&kaddr, addr, addrlen);
    {
        unsigned short *family = (unsigned short *)&kaddr;
        if (*family == 2U)
            *family = 0x0210U;
        else if (*family == 10U)
            *family = 0x1018U;  /* AF_INET6 */
    }

    ret = lwip_connect(fd, &kaddr, (socklen_t)addrlen);

    /* Arm CONNECTED detection whether the connect completed inline or is
     * in flight (EINPROGRESS) - the poll task reports the outcome via a
     * CONNECTED or ERROR_* event either way. */
    sdk_walnut_socket_poll_mark_connecting(fd);
    return ret;
}

static int send_impl(int fd, const void *buf, unsigned int len, int flags)
{
    int sent = lwip_send(fd, buf, len, flags);

    if (sent < 0) {
        int err = lwip_getsockerrno(fd);
        if (err == EAGAIN || err == EWOULDBLOCK)
            return 0;   /* abstraction convention: 0 = send buffer full */
        return sent;
    }
    if (sent > 0)
        sdk_walnut_socket_poll_note_sent(fd, sent);
    return sent;
}

static int recv_impl(int fd, void *buf, unsigned int len, int flags)
{
    return lwip_recv(fd, buf, len, flags);
}

static int close_impl(int fd)
{
    sdk_walnut_socket_poll_release(fd);     /* deregister first */
    if (fd < 0)
        return 0;
    return lwip_close(fd);
}

static int get_sock_errno_impl(int fd)
{
    /* Returned in the lwIP arch.h errno domain (EINPROGRESS 115, ENOTCONN
     * 107, ECONNABORTED 103, ...): every consumer includes lwIP headers and
     * compares against these values - do NOT translate to newlib numbers. */
    if (fd < 0)
        return 0;
    return lwip_getsockerrno(fd);
}

static int get_errno_impl(void)
{
    return 0;   /* fall through to get_sock_errno (see file header) */
}

static const SdkTcpFunctionalityOps s_walnut_tcp_ops = {
    .socket_create_with_callback = socket_create_impl,
    .connect                     = connect_impl,
    .send                        = send_impl,
    .recv                        = recv_impl,
    .close                       = close_impl,
    .get_sock_errno              = get_sock_errno_impl,
    .get_errno                   = get_errno_impl,
};

const SdkTcpFunctionalityOps *sdk_walnut_get_tcp_functionality_ops(void)
{
    return &s_walnut_tcp_ops;
}

/*---------------------------------------------------------------
 * sdk_tcp_connect_host
 *
 * Defining this here is what allows the abstraction to own the plain
 * sdk_tcp_* names: it is the only symbol of the kernel's wm_sdk_tcp.c.obj
 * (lib_wmsrc_B.a) referenced from app code (wm_ui_tcp demo), so with a
 * local definition that archive member is never pulled in and the
 * remaining sdk_tcp_* definitions cannot collide.
 *
 * Kernel-compatible semantics: DNS commonly fails for the first second
 * or two after the PDP context comes up, so resolution is retried until
 * it succeeds or timeout_ms elapses; the connect itself is non-blocking
 * and reports its outcome as a CONNECTED or ERROR_* event.
 *--------------------------------------------------------------*/
#define WALNUT_TCP_DNS_RETRY_MS  500U

int sdk_tcp_connect_host(int fd, const char *host, unsigned short port,
                         unsigned int timeout_ms)
{
    struct addrinfo  hints;
    struct addrinfo *result = NULL;
    char             portstr[6];
    UINT32           start;
    int              ret;

    if (fd < 0 || !host || host[0] == '\0')
        return -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    snprintf(portstr, sizeof(portstr), "%u", (unsigned)port);

    start = wm_sdk_get_ticks();
    for (;;) {
        ret = lwip_getaddrinfo(host, portstr, &hints, &result);
        if (ret == 0 && result != NULL)
            break;
        if ((wm_sdk_get_ticks() - start) >= timeout_ms) {
            LOG_ERROR("TCP connect_host: DNS failed for %s (ret=%d)", host, ret);
            return -1;
        }
        wm_sdk_task_sleep(WALNUT_TCP_DNS_RETRY_MS);
    }

    ret = connect_impl(fd, result->ai_addr, (unsigned int)result->ai_addrlen);
    lwip_freeaddrinfo(result);

    if (ret < 0) {
        int err = lwip_getsockerrno(fd);
        if (err == EINPROGRESS || err == EAGAIN || err == EWOULDBLOCK)
            return 0;   /* attempt under way - outcome arrives as an event */
        LOG_ERROR("TCP connect_host: connect failed (errno=%d)", err);
        return -1;
    }
    return 0;
}
