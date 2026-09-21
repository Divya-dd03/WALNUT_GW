/**
 * @file sdk_walnut_socket_poll.c
 * @brief Walnut app-space socket_with_callback via lwip_socket() + select()
 *        polling.
 *
 * The walnut kernel exports only the lwIP BSD socket layer (verified in
 * core_stub.o) - no socket_with_callback and no netconn callback layer.
 * This shim registers per-fd callbacks and synthesizes NETCONN-style events
 * from a "TCPMON" poll task:
 *   CONNECTED            select() writable + SO_ERROR == 0 (marked pending)
 *   RCVPLUS              select() readable + FIONREAD > 0
 *   CLOSE_NORMAL         select() readable + FIONREAD == 0 (peer FIN)
 *   ERROR_RST/ABRT/CLSD  SO_ERROR mapped by errno
 *   SENDPLUS/SENDMINUS   writability edges
 *   SENDACKED            SO_SNDBUF drain-back = peer TCP acks (probed per
 *                        socket; without SO_SNDBUF support, emitted right
 *                        away from sdk_walnut_socket_poll_note_sent())
 */

#include <string.h>

/* lwIP socket layer: kernel-format structs, FD_SET macros, and the errno
 * values lwip_getsockerrno returns (do NOT add newlib <errno.h>). */
#include "lwip/sockets.h"

// sdk
#include "wm_global.h"
#include "wm_sdk_os.h"
#include "wm_sdk_log.h"

// app
#include "tcp/sdk_walnut_socket_poll.h"
#include "tcp/sdk_functionality_network_compat.h"

#define LOG_TAG "TCP_SKT"
#define LOG_MODULE_LEVEL LOG_LEVEL_DEBUG
#include "module/log/log.h"

/* Exported by the kernel (core_stub.o) but not declared in lwIP headers */
extern int lwip_getsockerrno(int s);

/*---------------------------------------------------------------
 * Configuration
 *--------------------------------------------------------------*/
#define SOCKET_POLL_MAX        2      /* event-driven socket slots        */
#define SOCKET_POLL_WAIT_MS    100U   /* select() budget per pass         */
#define SOCKET_POLL_STACK      4096U

/*---------------------------------------------------------------
 * Registry
 *--------------------------------------------------------------*/
typedef struct {
    volatile int        fd;              /* -1 = slot free                  */
    SdkTcpEventCallback cb;
    volatile unsigned char connect_pending; /* emit CONNECTED when writable */
    unsigned char       last_writable;   /* SENDPLUS/SENDMINUS edge state   */
    unsigned char       fin_emitted;     /* one CLOSE per socket generation */
    unsigned char       err_emitted;     /* one ERROR_* per generation      */
    /* SENDACKED via SO_SNDBUF drain (per-socket probe) */
    unsigned char       sndbuf_supported;
    int                 sndbuf_capacity; /* SO_SNDBUF with empty queue      */
    volatile int        sndbuf_last;     /* last observed SO_SNDBUF         */
    volatile int        unacked;         /* sent bytes not yet acked        */
    /* Without SO_SNDBUF the ack is synthesized - but it must arrive
     * ASYNCHRONOUSLY (from the TCPMON task) like a real vendor event: a
     * synchronous callback inside send fires before the consumer records
     * the send and gets dropped ("SENDACKED with no pending send"). */
    volatile int        synth_ack_pending; /* bytes to ack on next pass     */
} SocketPollSlot;

static SocketPollSlot g_socket_slots[SOCKET_POLL_MAX] = {
    [0 ... SOCKET_POLL_MAX - 1] = { .fd = -1 },
};
static void          *g_socket_poll_task    = NULL;
static unsigned char  g_socket_poll_started = 0;

/*---------------------------------------------------------------
 * Internal helpers
 *--------------------------------------------------------------*/
static SocketPollSlot *socket_poll_find(int fd)
{
    int i;

    if (fd < 0)
        return NULL;
    for (i = 0; i < SOCKET_POLL_MAX; i++) {
        if (g_socket_slots[i].fd == fd)
            return &g_socket_slots[i];
    }
    return NULL;
}

static SocketPollSlot *socket_poll_alloc(void)
{
    int i;

    for (i = 0; i < SOCKET_POLL_MAX; i++) {
        if (g_socket_slots[i].fd < 0)
            return &g_socket_slots[i];
    }
    return NULL;
}

/** Available send-buffer space, or -1 when SO_SNDBUF is unsupported. */
static int sndbuf_avail(int fd)
{
    int       value = 0;
    socklen_t olen  = sizeof(value);

    if (lwip_getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &value, &olen) != 0)
        return -1;
    return (value > 0) ? value : -1;
}

/** Map a pending socket errno to the matching netconn error event. */
static int errno_to_error_event(int err)
{
    if (err == ECONNRESET)
        return SDK_NETCONN_EVT_ERROR_RST;
    if (err == ECONNABORTED)
        return SDK_NETCONN_EVT_ERROR_ABRT;
    if (err == ENOTCONN || err == EPIPE)
        return SDK_NETCONN_EVT_ERROR_CLSD;
    return SDK_NETCONN_EVT_ERROR;
}

/*---------------------------------------------------------------
 * One poll-pass over a single slot
 *--------------------------------------------------------------*/
static void socket_poll_service_slot(SocketPollSlot *slot,
                                     int readable, int writable, int excepted)
{
    int fd = slot->fd;

    if (fd < 0 || !slot->cb)
        return;

    /* --- Errors first: exception set with a REAL pending errno ----------
     * This kernel also raises the except flag spuriously right after
     * connect with SO_ERROR still 0 (observed on-device; possibly a
     * connect-notification semantic). Only a nonzero SO_ERROR is trusted;
     * everything else is logged once and ignored - a genuinely dead socket
     * is caught by the consumer's send failures / errno checks / timeouts. */
    if (excepted) {
        if (!slot->err_emitted) {
            int       so_error = 0;
            socklen_t err_len  = (socklen_t)sizeof(so_error);
            int       gs_ret   = lwip_getsockopt(fd, SOL_SOCKET, SO_ERROR,
                                                 &so_error, &err_len);
            if (gs_ret == 0 && so_error != 0) {
                slot->err_emitted = 1;
                slot->cb(fd, errno_to_error_event(so_error), 0);
            } else {
                slot->err_emitted = 1;   /* log-once latch */
                LOG_DEBUG("TCPMON except-suspect fd=%d gs=%d so_err=%d (ignored)",
                                fd, gs_ret, so_error);
            }
        }
        return;
    }

    /* --- CONNECTED: writable while a connect is in flight --------------- */
    if (slot->connect_pending) {
        if (writable) {
            int             so_error = 0;
            socklen_t       err_len  = (socklen_t)sizeof(so_error);
            int             gs_ret   = lwip_getsockopt(fd, SOL_SOCKET, SO_ERROR,
                                                       &so_error, &err_len);
            struct sockaddr peer;
            socklen_t       peer_len = (socklen_t)sizeof(peer);

            if (gs_ret == 0 && so_error != 0) {
                /* Handshake failed (RST/refused/unreachable) */
                slot->connect_pending = 0;
                LOG_DEBUG("TCPMON conn-check fd=%d failed so_err=%d",
                                fd, so_error);
                slot->cb(fd, errno_to_error_event(so_error), 0);
                slot->last_writable = (unsigned char)writable;
                return;
            }

            /* This kernel's select reports SYN_SENT sockets writable with
             * SO_ERROR still 0 (observed on-device: premature CONNECTED,
             * then recv ENOTCONN misread as FIN). getpeername succeeds only
             * once the handshake has actually completed - gate on it and
             * keep waiting while it reports ENOTCONN. */
            if (lwip_getpeername(fd, &peer, &peer_len) != 0) {
                LOG_DEBUG("TCPMON conn-check fd=%d w=1 but not connected yet (errno=%d)",
                                fd, lwip_getsockerrno(fd));
                slot->last_writable = (unsigned char)writable;
                return;     /* still SYN_SENT - re-check next pass */
            }

            slot->connect_pending = 0;
            LOG_DEBUG("TCPMON conn-check fd=%d connected (peer verified)", fd);
            slot->cb(fd, SDK_NETCONN_EVT_CONNECTED, 0);
        }
        slot->last_writable = (unsigned char)writable;
        return;
    }

    /* --- RCVPLUS / CLOSE: readable means data or FIN --------------------- */
    if (readable) {
        int io_ret = -1;
        int avail  = 0;
        io_ret = lwip_ioctl(fd, FIONREAD, &avail);
        if (io_ret == 0 && avail > 0) {
            slot->cb(fd, SDK_NETCONN_EVT_RCVPLUS,
                     (unsigned short)((avail > 0xFFFF) ? 0xFFFF : avail));
        } else if (!slot->fin_emitted) {
            /* readable with 0 bytes *should* mean FIN, but this kernel
             * raises it spuriously on healthy connections, so no CLOSE is
             * synthesized - liveness is judged by the trustworthy signals:
             * SO_ERROR (RST/abort), send failures, the state machine's
             * per-cycle errno checks and ack/overall timeouts.
             * NOTE: no probe recv here - a MSG_PEEK attempt on this state
             * sets the socket's lingering errno to ENOTCONN(107), poisoning
             * every later getsockerrno-based check. Log once, look only. */
            slot->fin_emitted = 1;   /* log-once latch */
            LOG_DEBUG("TCPMON fin-suspect fd=%d io=%d avail=%d (no CLOSE synthesized)",
                            fd, io_ret, avail);
        }
    }

    /* --- SENDACKED: send-buffer drain equals peer TCP acks --------------- */
    if (slot->sndbuf_supported && slot->unacked > 0) {
        int cur = sndbuf_avail(fd);
        if (cur > slot->sndbuf_last) {
            int delta = cur - slot->sndbuf_last;
            if (delta > slot->unacked)
                delta = slot->unacked;
            slot->sndbuf_last = cur;
            slot->unacked    -= delta;
            slot->cb(fd, SDK_NETCONN_EVT_SENDACKED,
                     (unsigned short)((delta > 0xFFFF) ? 0xFFFF : delta));
        } else if (cur == slot->sndbuf_capacity) {
            /* Fully drained but delta accounting missed acks (fast RTT
             * between send and first snapshot) - top up the remainder */
            int remain = slot->unacked;
            slot->unacked     = 0;
            slot->sndbuf_last = cur;
            slot->cb(fd, SDK_NETCONN_EVT_SENDACKED,
                     (unsigned short)((remain > 0xFFFF) ? 0xFFFF : remain));
        }
    }

    /* --- SENDPLUS / SENDMINUS: writability edges -------------------------- */
    if ((unsigned char)writable != slot->last_writable) {
        slot->last_writable = (unsigned char)writable;
        slot->cb(fd, writable ? SDK_NETCONN_EVT_SENDPLUS
                              : SDK_NETCONN_EVT_SENDMINUS, 0);
    }
}

/*---------------------------------------------------------------
 * TCPMON poll task
 *--------------------------------------------------------------*/
static void socket_poll_loop(void *arg)
{
    (void)arg;
    LOG_INFO("TCPMON task started");

    for (;;) {
        struct timeval tv;
        fd_set         read_fds;
        fd_set         write_fds;
        fd_set         except_fds;
        int            max_fd = -1;
        int            i;
        int            ret;

        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        FD_ZERO(&except_fds);

        for (i = 0; i < SOCKET_POLL_MAX; i++) {
            int fd = g_socket_slots[i].fd;
            if (fd < 0)
                continue;
            FD_SET(fd, &read_fds);
            FD_SET(fd, &write_fds);
            FD_SET(fd, &except_fds);
            if (fd > max_fd)
                max_fd = fd;
        }

        if (max_fd < 0) {
            wm_sdk_task_sleep(SOCKET_POLL_WAIT_MS);
            continue;
        }

        tv.tv_sec  = (int)(SOCKET_POLL_WAIT_MS / 1000U);
        tv.tv_usec = (int)((SOCKET_POLL_WAIT_MS % 1000U) * 1000U);

        ret = lwip_select(max_fd + 1, &read_fds, &write_fds, &except_fds, &tv);

        /* Deliver deferred synthesized SENDACKEDs first - independent of
         * select's outcome (the consumer has recorded the send by now). */
        for (i = 0; i < SOCKET_POLL_MAX; i++) {
            SocketPollSlot *slot = &g_socket_slots[i];
            int pending = slot->synth_ack_pending;
            if (slot->fd >= 0 && slot->cb && pending > 0) {
                slot->synth_ack_pending -= pending;
                slot->cb(slot->fd, SDK_NETCONN_EVT_SENDACKED,
                         (unsigned short)((pending > 0xFFFF) ? 0xFFFF : pending));
            }
        }

        if (ret < 0) {
            LOG_DEBUG("TCPMON select failed");
            wm_sdk_task_sleep(SOCKET_POLL_WAIT_MS);
            continue;
        }
        if (ret == 0)
            continue;   /* timeout - select provided the pacing */

        for (i = 0; i < SOCKET_POLL_MAX; i++) {
            int fd = g_socket_slots[i].fd;
            if (fd < 0)
                continue;   /* released while select() slept */
            socket_poll_service_slot(&g_socket_slots[i],
                                     FD_ISSET(fd, &read_fds)   ? 1 : 0,
                                     FD_ISSET(fd, &write_fds)  ? 1 : 0,
                                     FD_ISSET(fd, &except_fds) ? 1 : 0);
        }

        /* Pace every pass. select() alone cannot: it is level-triggered,
         * and this kernel can leave a socket persistently flagged (e.g.
         * readable-with-0-bytes after connect), so select returns instantly
         * on each pass and this loop busy-spins - starving same-priority
         * tasks (observed on-device: TCP client task frozen in CONNECTING,
         * login never sent). */
        wm_sdk_task_sleep(SOCKET_POLL_WAIT_MS);
    }
}

static int socket_poll_start(void)
{
    if (g_socket_poll_started)
        return 1;

    g_socket_poll_task = wm_sdk_task_create(socket_poll_loop, NULL, "TCPMON",
                                         NULL, SOCKET_POLL_STACK,
                                         TP_TIMED_ACTIVITY);
    if (g_socket_poll_task == NULL) {
        LOG_ERROR("TCPMON task create failed");
        return 0;
    }
    g_socket_poll_started = 1;
    return 1;
}

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/
int sdk_walnut_socket_with_callback(int domain, int type, int protocol,
                                    SdkTcpEventCallback callback)
{
    SocketPollSlot *slot;
    int             nonblock = 1;
    int             fd;
    int             cap;

    (void)domain;
    (void)type;
    (void)protocol;

    if (!callback)
        return -1;
    if (!socket_poll_start())
        return -1;

    slot = socket_poll_alloc();
    if (!slot) {
        LOG_ERROR("TCP no free event-socket slot (max %d)", SOCKET_POLL_MAX);
        return -1;
    }

    fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
        return fd;

    /* Non-blocking: connect returns EINPROGRESS, send/recv return EAGAIN */
    if (lwip_ioctl(fd, FIONBIO, &nonblock) != 0) {
        LOG_ERROR("TCP non-blocking setup failed (errno=%d)",
                      lwip_getsockerrno(fd));
        lwip_close(fd);
        return -1;
    }

    /* Probe SO_SNDBUF once: with the queue empty the returned value is the
     * full capacity - the SENDACKED drain-detection baseline. */
    cap = sndbuf_avail(fd);

    memset(slot, 0, sizeof(*slot));
    slot->cb               = callback;
    slot->sndbuf_supported = (cap > 0) ? 1 : 0;
    slot->sndbuf_capacity  = cap;
    slot->sndbuf_last      = cap;
    slot->fd               = fd;    /* last: publishes the slot to TCPMON */

    LOG_DEBUG("TCP walnut socket fd=%d (sndbuf=%s cap=%d)",
                    fd, slot->sndbuf_supported ? "yes" : "no", cap);
    return fd;
}

void sdk_walnut_socket_poll_release(int fd)
{
    SocketPollSlot *slot = socket_poll_find(fd);

    if (slot) {
        memset(slot, 0, sizeof(*slot));
        slot->fd = -1;
    }
}

void sdk_walnut_socket_poll_mark_connecting(int fd)
{
    SocketPollSlot *slot = socket_poll_find(fd);

    if (slot) {
        slot->fin_emitted     = 0;
        slot->err_emitted     = 0;
        slot->connect_pending = 1;
    }
}

void sdk_walnut_socket_poll_note_sent(int fd, int len)
{
    SocketPollSlot *slot = socket_poll_find(fd);

    if (!slot || len <= 0)
        return;

    slot->connect_pending = 0;      /* activity proves connected */

    if (slot->sndbuf_supported) {
        int after = sndbuf_avail(fd);
        slot->sndbuf_last = (after >= 0) ? after
                                         : (slot->sndbuf_capacity - len);
        slot->unacked += len;
    } else {
        /* No peer-ack visibility without SO_SNDBUF: ack = accepted by the
         * stack; delivery failures surface via send errors / timeouts.
         * Deferred to the TCPMON pass so it lands after send returns. */
        slot->synth_ack_pending += len;
    }
}
