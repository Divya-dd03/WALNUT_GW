/**
  ******************************************************************************
  * @file    wm_ui_tcp.c
  * @author  Walnut Medical
  * @brief   Common Gateway (WEGW) reference application - TCP demo.
  *
  *          One menu handler covering the wm_sdk_tcp_* API: resolve a host, open an
  *          event-driven socket, echo a short payload, print the reply.
  *
  *          The handler returns to the menu as soon as the connect is under way.
  *          Everything after that arrives through the socket callback.
  *
  *          Note there are no lwIP includes: wm_sdk_tcp_connect_host() resolves the
  *          name, and WM_SDK_AF_INET / WM_SDK_SOCK_STREAM name the socket, so app code
  *          never sees a sockaddr. That matters because including lwip/sockets.h
  *          would macro-rewrite close/read/write for this whole file.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "wm_ui_tcp.h"

/*******************************************************************************
** Endpoint - retarget the demo by editing these
**
** A plain TCP echo service: whatever is sent comes straight back, which is
** enough to show CONNECT -> RECV -> CLOSE without a protocol on top.
******************************************************************************/
#define WM_TCP_HOST             "tcpbin.com"
#define WM_TCP_PORT             (4242u)

/* tcpbin echoes a line at a time, so the payload ends with a newline. */
#define WM_TCP_PAYLOAD          "12345678\n"

/*******************************************************************************
** Demo configuration
******************************************************************************/
#define WM_TCP_DNS_TIMEOUT_MS   (8000u)    /* budget for name resolution       */
#define WM_TCP_RECV_MAX         (128u)     /* per-event read size              */

/* The socket in flight, so a second run does not leak the first. */
static int s_tcp_fd = -1;

/*******************************************************************************
** Socket events
**
** Kept short: print, read, and hand the socket back.
******************************************************************************/
static void wm_tcp_event(int fd, int event, void *arg)
{
    char buf[WM_TCP_RECV_MAX + 1];
    int  n;

    (void)arg;      /* always NULL - see wm_SdkTcpSocketCallback */

    switch (event)
    {
    case WM_SDK_TCP_EVENT_CONNECT:
        wm_printf("tcp: connected (fd=%ld)\r\n", (long)fd);
        n = wm_sdk_tcp_send(fd, WM_TCP_PAYLOAD, (unsigned int)strlen(WM_TCP_PAYLOAD), 0);
        wm_printf("tcp: sent %ld of %lu bytes\r\n",
                  (long)n, (unsigned long)strlen(WM_TCP_PAYLOAD));
        break;

    case WM_SDK_TCP_EVENT_RECV:
        /* This call is also what re-arms the next RECV event. */
        n = wm_sdk_tcp_recv(fd, buf, WM_TCP_RECV_MAX, 0);
        if (n > 0)
        {
            buf[n] = '\0';
            wm_printf("tcp: recv %ld bytes: %s\r\n", (long)n, buf);
        }
        else
        {
            wm_printf("tcp: recv returned %ld (errno=%ld)\r\n",
                      (long)n, (long)wm_sdk_tcp_get_sock_errno(fd));
        }
        break;

    case WM_SDK_TCP_EVENT_CLOSE:
    case WM_SDK_TCP_EVENT_ERROR:
        wm_printf("tcp: %s (errno=%ld) -> closing\r\n",
                  (event == WM_SDK_TCP_EVENT_CLOSE) ? "peer closed" : "socket error",
                  (long)wm_sdk_tcp_get_sock_errno(fd));
        wm_printf("tcp: close -> rc=%ld\r\n", (long)wm_sdk_tcp_close(fd));
        s_tcp_fd = -1;
        break;

    default:
        wm_printf("tcp: unknown event %ld\r\n", (long)event);
        break;
    }
}

/*******************************************************************************
** TCP echo
******************************************************************************/
void wm_ui_tcp_demo(void)
{
    int fd;

    wm_printf("\r\n--- TCP: echo (event driven) ---\r\n");
    wm_printf("%s:%lu\r\n", WM_TCP_HOST, (unsigned long)WM_TCP_PORT);

    /* A socket left over from a previous run would still hold one of the
     * limited number of event-driven sockets. */
    if (s_tcp_fd >= 0)
    {
        wm_printf("closing previous socket -> rc=%ld\r\n", (long)wm_sdk_tcp_close(s_tcp_fd));
        s_tcp_fd = -1;
    }

    fd = wm_sdk_tcp_socket_create_with_callback(WM_SDK_AF_INET, WM_SDK_SOCK_STREAM, 0,
                                             wm_tcp_event);
    if (fd < 0)
    {
        wm_printf("socket create failed (all event slots in use?)\r\n");
        return;
    }
    wm_printf("socket created, fd=%ld\r\n", (long)fd);

    /* Resolution blocks here; the connect itself does not, so the result
     * arrives as a CONNECT or ERROR event. */
    if (wm_sdk_tcp_connect_host(fd, WM_TCP_HOST, (UINT16)WM_TCP_PORT,
                             WM_TCP_DNS_TIMEOUT_MS) != 0)
    {
        wm_printf("connect failed (DNS or no route) -> rc=%ld\r\n",
                  (long)wm_sdk_tcp_close(fd));
        return;
    }

    s_tcp_fd = fd;
    wm_printf("connect under way - the callback reports the outcome\r\n");
}
