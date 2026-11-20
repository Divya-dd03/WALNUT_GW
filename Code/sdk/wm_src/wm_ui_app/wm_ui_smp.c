/**
  ******************************************************************************
  * @file    wm_ui_smp.c
  * @author  Walnut Medical
  * @brief   Common Gateway (WEGW) reference application - MCUmgr SMP client.
  *
  *          Speaks the SMP protocol to an MCUboot / nRF Connect SDK peripheral
  *          over one BLE link, which is what "DFU" means for those devices.
  *
  *          An SMP packet is an 8-byte header followed by a CBOR map. There is
  *          no CBOR library in this SDK, so the encoder and the decoder below
  *          cover just the shapes the image and OS groups use.
  *
  *          The transfer is strictly request/response: nothing is sent until
  *          the previous reply has been consumed, and the offset the device
  *          returns is what drives the next write, never a local counter.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 Walnut Medical.
  * All rights reserved.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "wm_ui_smp.h"

/* Protocol constants */
/* SMP header is op, flags, length(2), group(2), seq, id - length and group BE. */
#define WM_SMP_HDR_LEN        8u

#define WM_SMP_OP_READ        0u
#define WM_SMP_OP_READ_RSP    1u
#define WM_SMP_OP_WRITE       2u
#define WM_SMP_OP_WRITE_RSP   3u

#define WM_SMP_GRP_OS         0u
#define WM_SMP_GRP_IMAGE      1u

#define WM_SMP_ID_OS_ECHO     0u
#define WM_SMP_ID_OS_RESET    5u

#define WM_SMP_ID_IMG_STATE   0u
#define WM_SMP_ID_IMG_UPLOAD  1u

/* Image bytes per upload packet. The first also carries len, image and sha. */
#define WM_SMP_CHUNK_FIRST    128u
#define WM_SMP_CHUNK_NEXT     200u

/* Bytes of an unparseable reply printed before it is given up on. */
#define WM_SMP_DUMP_MAX       48u

/* wm_sdk_ble_send reports BUSY while the radio buffer drains. */
#define WM_SMP_SEND_TRIES     50
#define WM_SMP_SEND_WAIT_MS   20u

/* CBOR major types. */
#define WM_CBOR_UINT          0u
#define WM_CBOR_NINT          1u
#define WM_CBOR_BYTES         2u
#define WM_CBOR_TEXT          3u
#define WM_CBOR_ARRAY         4u
#define WM_CBOR_MAP           5u
#define WM_CBOR_TAG           6u
#define WM_CBOR_SIMPLE        7u

/* State */
static int    s_cid  = -1;
static UINT8  s_seq  = 0u;
static void  *s_q    = NULL;

/* Built here rather than on the stack: BLEMON has little to spare. */
static UINT8  s_tx[WM_SMP_PKT_MAX];

/* Notifications land in s_rx until a whole packet is present, then in s_rsp. */
static UINT8  s_rx[WM_SMP_RX_MAX];
static UINT16 s_rx_len  = 0u;
static UINT8  s_rsp[WM_SMP_RX_MAX];
static UINT16 s_rsp_len = 0u;

/* One image chunk, read straight out of the staged file. */
static UINT8  s_img[WM_SMP_CHUNK_NEXT];

/* CBOR writer */
typedef struct
{
    UINT8 *buf;
    UINT32 cap;
    UINT32 len;
    BOOL   ovf;
} wm_cbw_t;

static void prv_w_init(wm_cbw_t *w, UINT8 *buf, UINT32 cap)
{
    w->buf = buf;
    w->cap = cap;
    w->len = 0u;
    w->ovf = FALSE;
}

static void prv_w_raw(wm_cbw_t *w, const UINT8 *d, UINT32 n)
{
    if ((w->len + n) > w->cap)
    {
        w->ovf = TRUE;
        return;
    }
    memcpy(&w->buf[w->len], d, (size_t)n);
    w->len += n;
}

/* Major type in the top 3 bits, then the argument in 0, 1, 2 or 4 bytes. */
static void prv_w_head(wm_cbw_t *w, UINT8 mt, UINT32 val)
{
    UINT8 h[5];
    UINT32 n;

    if (val < 24u)
    {
        h[0] = (UINT8)((mt << 5) | (UINT8)val);
        n = 1u;
    }
    else if (val <= 0xFFu)
    {
        h[0] = (UINT8)((mt << 5) | 24u);
        h[1] = (UINT8)val;
        n = 2u;
    }
    else if (val <= 0xFFFFu)
    {
        h[0] = (UINT8)((mt << 5) | 25u);
        h[1] = (UINT8)(val >> 8);
        h[2] = (UINT8)val;
        n = 3u;
    }
    else
    {
        h[0] = (UINT8)((mt << 5) | 26u);
        h[1] = (UINT8)(val >> 24);
        h[2] = (UINT8)(val >> 16);
        h[3] = (UINT8)(val >> 8);
        h[4] = (UINT8)val;
        n = 5u;
    }

    prv_w_raw(w, h, n);
}

static void prv_w_map(wm_cbw_t *w, UINT32 pairs)
{
    prv_w_head(w, WM_CBOR_MAP, pairs);
}

static void prv_w_key(wm_cbw_t *w, const char *k)
{
    UINT32 n = (UINT32)strlen(k);

    prv_w_head(w, WM_CBOR_TEXT, n);
    prv_w_raw(w, (const UINT8 *)k, n);
}

static void prv_w_uint(wm_cbw_t *w, UINT32 v)
{
    prv_w_head(w, WM_CBOR_UINT, v);
}

static void prv_w_bytes(wm_cbw_t *w, const UINT8 *d, UINT32 n)
{
    prv_w_head(w, WM_CBOR_BYTES, n);
    prv_w_raw(w, d, n);
}

static void prv_w_bool(wm_cbw_t *w, BOOL b)
{
    UINT8 v = b ? 0xF5u : 0xF4u;

    prv_w_raw(w, &v, 1u);
}

/* CBOR reader */
typedef struct
{
    const UINT8 *buf;
    UINT32       len;
    UINT32       pos;
} wm_cbr_t;

/* The byte that closes an indefinite-length map, array or string. */
#define WM_CBOR_BREAK         0xFFu

static BOOL prv_at_break(const wm_cbr_t *r)
{
    return (r->pos < r->len && r->buf[r->pos] == WM_CBOR_BREAK) ? TRUE : FALSE;
}

/* zcbor writes SMP replies as indefinite-length maps, so 'indef' is normal. */
static BOOL prv_r_head(wm_cbr_t *r, UINT8 *mt, UINT32 *val, BOOL *indef)
{
    UINT8  ib;
    UINT8  ai;
    UINT32 v;

    *indef = FALSE;
    *val   = 0u;

    if (r->pos >= r->len)
        return FALSE;

    ib = r->buf[r->pos++];
    *mt = (UINT8)(ib >> 5);
    ai  = (UINT8)(ib & 0x1Fu);

    if (ai == 31u)
    {
        *indef = TRUE;
        return TRUE;
    }

    if (ai < 24u)
    {
        *val = ai;
        return TRUE;
    }

    if (ai == 24u)
    {
        if ((r->pos + 1u) > r->len)
            return FALSE;
        *val = r->buf[r->pos++];
        return TRUE;
    }

    if (ai == 25u)
    {
        if ((r->pos + 2u) > r->len)
            return FALSE;
        v = ((UINT32)r->buf[r->pos] << 8) | (UINT32)r->buf[r->pos + 1u];
        r->pos += 2u;
        *val = v;
        return TRUE;
    }

    if (ai == 26u)
    {
        if ((r->pos + 4u) > r->len)
            return FALSE;
        v = ((UINT32)r->buf[r->pos] << 24) | ((UINT32)r->buf[r->pos + 1u] << 16) |
            ((UINT32)r->buf[r->pos + 2u] << 8) | (UINT32)r->buf[r->pos + 3u];
        r->pos += 4u;
        *val = v;
        return TRUE;
    }

    if (ai == 27u)
    {
        /* 64-bit: only the low half is kept, which is ample for these fields. */
        if ((r->pos + 8u) > r->len)
            return FALSE;
        v = ((UINT32)r->buf[r->pos + 4u] << 24) | ((UINT32)r->buf[r->pos + 5u] << 16) |
            ((UINT32)r->buf[r->pos + 6u] << 8) | (UINT32)r->buf[r->pos + 7u];
        r->pos += 8u;
        *val = v;
        return TRUE;
    }

    return FALSE;
}

static BOOL prv_r_skip(wm_cbr_t *r);

/* Skips the items of an indefinite-length container up to its break byte. */
static BOOL prv_skip_until_break(wm_cbr_t *r, UINT8 per_item)
{
    UINT8 k;

    for (;;)
    {
        if (prv_at_break(r))
        {
            r->pos++;
            return TRUE;
        }
        for (k = 0u; k < per_item; k++)
        {
            if (!prv_r_skip(r))
                return FALSE;
        }
    }
}

static BOOL prv_r_skip(wm_cbr_t *r)
{
    UINT8  mt;
    UINT32 val;
    UINT32 i;
    BOOL   indef;

    if (!prv_r_head(r, &mt, &val, &indef))
        return FALSE;

    switch (mt)
    {
        case WM_CBOR_UINT:
        case WM_CBOR_NINT:
            return indef ? FALSE : TRUE;

        case WM_CBOR_SIMPLE:
            /* An indefinite head here is a stray break, which cannot stand alone. */
            return indef ? FALSE : TRUE;

        case WM_CBOR_BYTES:
        case WM_CBOR_TEXT:
            if (indef)
                return prv_skip_until_break(r, 1u);
            if ((r->pos + val) > r->len)
                return FALSE;
            r->pos += val;
            return TRUE;

        case WM_CBOR_ARRAY:
            if (indef)
                return prv_skip_until_break(r, 1u);
            for (i = 0u; i < val; i++)
            {
                if (!prv_r_skip(r))
                    return FALSE;
            }
            return TRUE;

        case WM_CBOR_MAP:
            if (indef)
                return prv_skip_until_break(r, 2u);
            for (i = 0u; i < val; i++)
            {
                if (!prv_r_skip(r) || !prv_r_skip(r))
                    return FALSE;
            }
            return TRUE;

        case WM_CBOR_TAG:
            return prv_r_skip(r);

        default:
            return FALSE;
    }
}

/* Position 'out' on the value of 'key' in the map starting at 'map_pos'. */
static BOOL prv_map_seek(const UINT8 *buf, UINT32 len, UINT32 map_pos,
                         const char *key, wm_cbr_t *out)
{
    wm_cbr_t r;
    UINT8    mt;
    UINT32   pairs;
    UINT32   i = 0u;
    UINT32   klen = (UINT32)strlen(key);
    BOOL     indef;

    r.buf = buf;
    r.len = len;
    r.pos = map_pos;

    if (!prv_r_head(&r, &mt, &pairs, &indef) || mt != WM_CBOR_MAP)
        return FALSE;

    while (indef || i < pairs)
    {
        UINT8  kmt;
        UINT32 n;
        UINT32 kpos;
        BOOL   kindef;

        /* An indefinite map ends at its break byte rather than after N pairs. */
        if (indef && prv_at_break(&r))
            return FALSE;

        if (!prv_r_head(&r, &kmt, &n, &kindef) || kmt != WM_CBOR_TEXT || kindef)
            return FALSE;
        if ((r.pos + n) > r.len)
            return FALSE;

        kpos = r.pos;
        r.pos += n;

        if (n == klen && memcmp(&buf[kpos], key, (size_t)n) == 0)
        {
            *out = r;
            return TRUE;
        }

        if (!prv_r_skip(&r))
            return FALSE;

        i++;
    }

    return FALSE;
}

static BOOL prv_r_uint(wm_cbr_t *r, UINT32 *v)
{
    UINT8 mt;
    BOOL  indef;

    if (!prv_r_head(r, &mt, v, &indef) || indef)
        return FALSE;

    return (mt == WM_CBOR_UINT) ? TRUE : FALSE;
}

static BOOL prv_r_bytes(wm_cbr_t *r, const UINT8 **p, UINT32 *n)
{
    UINT8 mt;
    BOOL  indef;

    if (!prv_r_head(r, &mt, n, &indef) || mt != WM_CBOR_BYTES || indef)
        return FALSE;
    if ((r->pos + *n) > r->len)
        return FALSE;

    *p = &r->buf[r->pos];
    return TRUE;
}

/* Transport */
void wm_ui_smp_rx(const UINT8 *data, UINT16 len)
{
    if (data == NULL || len == 0u)
        return;

    /* A reply that cannot fit is dropped whole; the transfer then times out. */
    if ((UINT32)(s_rx_len + len) > (UINT32)WM_SMP_RX_MAX)
    {
        s_rx_len = 0u;
        return;
    }

    memcpy(&s_rx[s_rx_len], data, (size_t)len);
    s_rx_len = (UINT16)(s_rx_len + len);

    /* One notification may carry part of a packet, so wait for all of it. */
    while (s_rx_len >= (UINT16)WM_SMP_HDR_LEN)
    {
        UINT16 plen  = (UINT16)(((UINT16)s_rx[2] << 8) | (UINT16)s_rx[3]);
        UINT32 total = (UINT32)WM_SMP_HDR_LEN + (UINT32)plen;

        if (total > (UINT32)WM_SMP_RX_MAX)
        {
            s_rx_len = 0u;
            return;
        }
        if ((UINT32)s_rx_len < total)
            return;

        memcpy(s_rsp, s_rx, (size_t)total);
        s_rsp_len = (UINT16)total;

        s_rx_len = (UINT16)(s_rx_len - (UINT16)total);
        if (s_rx_len > 0u)
            memmove(s_rx, &s_rx[total], (size_t)s_rx_len);

        if (s_q != NULL)
        {
            UINT32 token = 1u;

            (void)wm_sdk_msgq_send(s_q, &token, 0u);
        }
    }
}

static wm_SdkResult prv_send_all(const UINT8 *d, UINT16 n)
{
    wm_SdkResult sr = WM_SDK_RESULT_ERROR;
    int          i;

    for (i = 0; i < WM_SMP_SEND_TRIES; i++)
    {
        sr = wm_sdk_ble_send(s_cid, d, n);
        if (sr != WM_SDK_RESULT_BUSY)
            return sr;
        wm_sdk_task_sleep(WM_SMP_SEND_WAIT_MS);
    }

    return sr;
}

/* One request, one reply. On success 'rsp' points at the reply's CBOR map. */
static wm_SdkResult prv_xfer(UINT8 op, UINT16 group, UINT8 id,
                             const UINT8 *pl, UINT16 pl_len, UINT32 timeout_ms,
                             const UINT8 **rsp, UINT16 *rsp_len)
{
    UINT32       token;
    wm_SdkResult sr;
    UINT8        seq;

    if (s_cid < 0 || s_q == NULL)
        return WM_SDK_RESULT_NOT_INITIALIZED;
    if (((UINT32)WM_SMP_HDR_LEN + (UINT32)pl_len) > (UINT32)WM_SMP_PKT_MAX)
        return WM_SDK_RESULT_INVALID_PARAM;

    seq = s_seq++;

    s_tx[0] = op;
    s_tx[1] = 0u;
    s_tx[2] = (UINT8)(pl_len >> 8);
    s_tx[3] = (UINT8)pl_len;
    s_tx[4] = (UINT8)(group >> 8);
    s_tx[5] = (UINT8)group;
    s_tx[6] = seq;
    s_tx[7] = id;

    if (pl != NULL && pl_len > 0u)
        memcpy(&s_tx[WM_SMP_HDR_LEN], pl, (size_t)pl_len);

    /* Drop anything stale so a late reply cannot be read as this one's. */
    while (wm_sdk_msgq_recv(s_q, &token, 0u) == WM_SDK_RESULT_SUCCESS)
        ;
    s_rx_len  = 0u;
    s_rsp_len = 0u;

    sr = prv_send_all(s_tx, (UINT16)(WM_SMP_HDR_LEN + pl_len));
    if (sr != WM_SDK_RESULT_SUCCESS)
        return sr;

    /* A reset reply is allowed to go missing, so the caller may skip the wait. */
    if (rsp == NULL)
        return WM_SDK_RESULT_SUCCESS;

    if (wm_sdk_msgq_recv(s_q, &token, timeout_ms) != WM_SDK_RESULT_SUCCESS)
        return WM_SDK_RESULT_TIMEOUT;

    if (s_rsp_len < (UINT16)WM_SMP_HDR_LEN)
        return WM_SDK_RESULT_ERROR;
    if (s_rsp[6] != seq)
        return WM_SDK_RESULT_ERROR;
    if ((((UINT16)s_rsp[4] << 8) | (UINT16)s_rsp[5]) != group)
        return WM_SDK_RESULT_ERROR;

    *rsp     = &s_rsp[WM_SMP_HDR_LEN];
    *rsp_len = (UINT16)(s_rsp_len - (UINT16)WM_SMP_HDR_LEN);

    return WM_SDK_RESULT_SUCCESS;
}

/* Prints a reply the decoder could not make sense of, so it can be read off. */
static void prv_dump(const char *what, const UINT8 *d, UINT16 len)
{
    UINT16 n = (len > WM_SMP_DUMP_MAX) ? (UINT16)WM_SMP_DUMP_MAX : len;
    UINT16 i;

    wm_printf("smp: %s %u bytes:", what, (unsigned)len);
    for (i = 0u; i < n; i++)
        wm_printf(" %02X", (unsigned)d[i]);
    if (n < len)
        wm_printf(" ...");
    wm_printf("\r\n");
}

/* Failure is a non-zero "rc", or an SMP v2 "err" map holding group and rc. */
static wm_SdkResult prv_check_rc(const UINT8 *rsp, UINT16 len)
{
    wm_cbr_t v;
    UINT32   rc = 0u;
    UINT32   grp = 0u;

    if (prv_map_seek(rsp, (UINT32)len, 0u, "err", &v))
    {
        UINT32 epos = v.pos;

        if (prv_map_seek(rsp, (UINT32)len, epos, "rc", &v))
            (void)prv_r_uint(&v, &rc);
        if (prv_map_seek(rsp, (UINT32)len, epos, "group", &v))
            (void)prv_r_uint(&v, &grp);

        wm_printf("smp: device returned error group=%lu rc=%lu\r\n",
                  (unsigned long)grp, (unsigned long)rc);
        return WM_SDK_RESULT_ERROR;
    }

    if (!prv_map_seek(rsp, (UINT32)len, 0u, "rc", &v))
        return WM_SDK_RESULT_SUCCESS;

    if (!prv_r_uint(&v, &rc))
        return WM_SDK_RESULT_ERROR;

    if (rc != 0u)
    {
        wm_printf("smp: device rejected the request, rc=%lu\r\n", (unsigned long)rc);
        return WM_SDK_RESULT_ERROR;
    }

    return WM_SDK_RESULT_SUCCESS;
}

/* Client */
wm_SdkResult wm_ui_smp_open(int cid)
{
    if (cid < 0)
        return WM_SDK_RESULT_INVALID_PARAM;

    if (s_q == NULL)
        s_q = wm_sdk_msgq_create("smpq", sizeof(UINT32), 4, 0);
    if (s_q == NULL)
    {
        wm_printf("smp: response queue create failed\r\n");
        return WM_SDK_RESULT_ERROR;
    }

    s_cid     = cid;
    s_rx_len  = 0u;
    s_rsp_len = 0u;

    return WM_SDK_RESULT_SUCCESS;
}

void wm_ui_smp_close(void)
{
    s_cid     = -1;
    s_rx_len  = 0u;
    s_rsp_len = 0u;
}

wm_SdkResult wm_ui_smp_echo(UINT32 timeout_ms)
{
    UINT8        pl[32];
    wm_cbw_t     w;
    const UINT8 *rsp = NULL;
    UINT16       rlen = 0u;
    wm_SdkResult sr;

    prv_w_init(&w, pl, sizeof(pl));
    prv_w_map(&w, 1u);
    prv_w_key(&w, "d");
    prv_w_key(&w, "wegw");

    if (w.ovf)
        return WM_SDK_RESULT_ERROR;

    sr = prv_xfer(WM_SMP_OP_WRITE, WM_SMP_GRP_OS, WM_SMP_ID_OS_ECHO,
                  pl, (UINT16)w.len, timeout_ms, &rsp, &rlen);
    if (sr != WM_SDK_RESULT_SUCCESS)
        return sr;

    /* Any well-formed reply proves the framing, even one refusing the command. */
    /* A build without the OS group answers with an error, which still passes. */
    if (prv_check_rc(rsp, rlen) == WM_SDK_RESULT_SUCCESS)
    {
        wm_cbr_t v;
        UINT32   n = 0u;
        UINT8    mt;
        BOOL     indef;
        char     txt[24];

        /* Echo returns what was sent, so finding it also proves the decoder. */
        if (prv_map_seek(rsp, (UINT32)rlen, 0u, "r", &v) &&
            prv_r_head(&v, &mt, &n, &indef) && mt == WM_CBOR_TEXT && !indef &&
            (v.pos + n) <= v.len)
        {
            UINT32 copy = (n < (UINT32)(sizeof(txt) - 1u)) ? n : (UINT32)(sizeof(txt) - 1u);

            memcpy(txt, &v.buf[v.pos], (size_t)copy);
            txt[copy] = '\0';
            wm_printf("smp: echo returned \"%s\" - decoder ok\r\n", txt);
        }
        else
        {
            wm_printf("smp: echo reply did not decode - check the dump below\r\n");
            prv_dump("echo reply", rsp, rlen);
        }
    }

    return WM_SDK_RESULT_SUCCESS;
}

wm_SdkResult wm_ui_smp_upload(const char *path, UINT32 total, UINT32 timeout_ms)
{
    UINT8        pl[WM_SMP_PKT_MAX];
    void        *f;
    UINT32       off   = 0u;
    UINT32       shown = 0u;
    UINT32       cur   = 0u;
    UINT32       t0;
    BOOL         first = TRUE;

    if (path == NULL || total == 0u)
        return WM_SDK_RESULT_INVALID_PARAM;

    f = wm_sdk_file_open(path, "rb");
    if (f == NULL)
    {
        wm_printf("smp: cannot open %s\r\n", path);
        return WM_SDK_RESULT_ERROR;
    }

    t0 = wm_sdk_get_ticks();

    while (off < total)
    {
        wm_cbw_t     w;
        const UINT8 *rsp  = NULL;
        UINT16       rlen = 0u;
        wm_cbr_t     v;
        UINT32       want;
        UINT32       got  = 0u;
        UINT32       next = 0u;
        wm_SdkResult sr;

        want = first ? (UINT32)WM_SMP_CHUNK_FIRST : (UINT32)WM_SMP_CHUNK_NEXT;
        if ((off + want) > total)
            want = total - off;

        /* The device drives the offset, but it advances by exactly what was */
        /* sent, so the cursor is already in place and no seek is needed. */
        if (off != cur)
        {
            /* Only on a resume. wm_sdk_file_seek reports the resulting offset */
            /* as a failure on this volume, so the read below is the real test. */
            (void)wm_sdk_file_seek(f, (INT32)off, 0u);
            cur = off;
        }

        if (wm_sdk_file_read(f, s_img, want, &got) != WM_SDK_RESULT_SUCCESS ||
            got != want)
        {
            wm_printf("smp: read %lu of %lu bytes at %lu\r\n",
                      (unsigned long)got, (unsigned long)want, (unsigned long)off);
            wm_sdk_file_close(f);
            return WM_SDK_RESULT_ERROR;
        }

        cur = off + got;

        prv_w_init(&w, pl, sizeof(pl));

        if (first)
        {
            prv_w_map(&w, 4u);
            prv_w_key(&w, "image");
            prv_w_uint(&w, 0u);
            prv_w_key(&w, "len");
            prv_w_uint(&w, total);
        }
        else
        {
            prv_w_map(&w, 2u);
        }

        prv_w_key(&w, "off");
        prv_w_uint(&w, off);
        prv_w_key(&w, "data");
        prv_w_bytes(&w, s_img, want);

        if (w.ovf)
        {
            wm_printf("smp: upload packet overflow\r\n");
            wm_sdk_file_close(f);
            return WM_SDK_RESULT_ERROR;
        }

        sr = prv_xfer(WM_SMP_OP_WRITE, WM_SMP_GRP_IMAGE, WM_SMP_ID_IMG_UPLOAD,
                      pl, (UINT16)w.len, timeout_ms, &rsp, &rlen);
        if (sr != WM_SDK_RESULT_SUCCESS)
        {
            wm_printf("smp: upload at %lu failed rc=%ld\r\n",
                      (unsigned long)off, (long)sr);
            wm_sdk_file_close(f);
            return sr;
        }

        if (prv_check_rc(rsp, rlen) != WM_SDK_RESULT_SUCCESS)
        {
            wm_sdk_file_close(f);
            return WM_SDK_RESULT_ERROR;
        }

        if (!prv_map_seek(rsp, (UINT32)rlen, 0u, "off", &v) || !prv_r_uint(&v, &next))
        {
            wm_printf("smp: reply at %lu carried no offset\r\n", (unsigned long)off);
            prv_dump("upload reply", rsp, rlen);
            wm_sdk_file_close(f);
            return WM_SDK_RESULT_ERROR;
        }

        /* No forward movement means the device is refusing; stop rather than spin. */
        if (next <= off)
        {
            wm_printf("smp: stalled at %lu (device asked for %lu)\r\n",
                      (unsigned long)off, (unsigned long)next);
            wm_sdk_file_close(f);
            return WM_SDK_RESULT_ERROR;
        }

        off   = next;
        first = FALSE;

        if ((off - shown) >= 16384u || off >= total)
        {
            UINT32 ms = wm_sdk_get_ticks() - t0;

            shown = off;
            wm_printf("  %lu/%lu bytes", (unsigned long)off, (unsigned long)total);
            if (ms > 0u)
            {
                /* 64-bit so a multi-megabyte image cannot wrap the rate. */
                unsigned long long rate = ((unsigned long long)off * 1000ull) / ms;

                wm_printf(" (%lu B/s)", (unsigned long)rate);
            }
            wm_printf("\r\n");
        }
    }

    wm_sdk_file_close(f);
    return WM_SDK_RESULT_SUCCESS;
}

wm_SdkResult wm_ui_smp_image_hash(UINT8 slot, UINT8 *hash_out, UINT32 timeout_ms)
{
    const UINT8 *rsp  = NULL;
    UINT16       rlen = 0u;
    wm_cbr_t     arr;
    wm_SdkResult sr;
    UINT8        mt;
    UINT32       count;
    UINT32       i = 0u;
    BOOL         indef;

    if (hash_out == NULL)
        return WM_SDK_RESULT_INVALID_PARAM;

    sr = prv_xfer(WM_SMP_OP_READ, WM_SMP_GRP_IMAGE, WM_SMP_ID_IMG_STATE,
                  NULL, 0u, timeout_ms, &rsp, &rlen);
    if (sr != WM_SDK_RESULT_SUCCESS)
        return sr;
    if (prv_check_rc(rsp, rlen) != WM_SDK_RESULT_SUCCESS)
        return WM_SDK_RESULT_ERROR;

    if (!prv_map_seek(rsp, (UINT32)rlen, 0u, "images", &arr))
        return WM_SDK_RESULT_ERROR;
    if (!prv_r_head(&arr, &mt, &count, &indef) || mt != WM_CBOR_ARRAY)
        return WM_SDK_RESULT_ERROR;

    /* The array is indefinite-length too, so it ends at a break, not a count. */
    while (indef || i < count)
    {
        UINT32       elem = arr.pos;
        wm_cbr_t     v;
        UINT32       sl = 0xFFFFu;
        const UINT8 *h  = NULL;
        UINT32       hn = 0u;

        if (indef && prv_at_break(&arr))
            break;

        if (prv_map_seek(rsp, (UINT32)rlen, elem, "slot", &v))
            (void)prv_r_uint(&v, &sl);

        if (sl == (UINT32)slot &&
            prv_map_seek(rsp, (UINT32)rlen, elem, "hash", &v) &&
            prv_r_bytes(&v, &h, &hn) && hn == (UINT32)WM_SMP_HASH_LEN)
        {
            memcpy(hash_out, h, (size_t)hn);
            return WM_SDK_RESULT_SUCCESS;
        }

        arr.pos = elem;
        if (!prv_r_skip(&arr))
            return WM_SDK_RESULT_ERROR;

        i++;
    }

    wm_printf("smp: no image reported in slot %u\r\n", (unsigned)slot);
    return WM_SDK_RESULT_ERROR;
}

/* Writes the image state: 'permanent' false marks for test, true confirms. */
static wm_SdkResult prv_image_mark(const UINT8 *hash, BOOL permanent,
                                   UINT32 timeout_ms)
{
    UINT8        pl[64];
    wm_cbw_t     w;
    const UINT8 *rsp  = NULL;
    UINT16       rlen = 0u;
    wm_SdkResult sr;

    prv_w_init(&w, pl, sizeof(pl));
    prv_w_map(&w, (hash != NULL) ? 2u : 1u);

    if (hash != NULL)
    {
        prv_w_key(&w, "hash");
        prv_w_bytes(&w, hash, WM_SMP_HASH_LEN);
    }

    prv_w_key(&w, "confirm");
    prv_w_bool(&w, permanent);

    if (w.ovf)
        return WM_SDK_RESULT_ERROR;

    sr = prv_xfer(WM_SMP_OP_WRITE, WM_SMP_GRP_IMAGE, WM_SMP_ID_IMG_STATE,
                  pl, (UINT16)w.len, timeout_ms, &rsp, &rlen);
    if (sr != WM_SDK_RESULT_SUCCESS)
        return sr;

    return prv_check_rc(rsp, rlen);
}

wm_SdkResult wm_ui_smp_image_test(const UINT8 *hash, UINT32 timeout_ms)
{
    if (hash == NULL)
        return WM_SDK_RESULT_INVALID_PARAM;

    return prv_image_mark(hash, FALSE, timeout_ms);
}

wm_SdkResult wm_ui_smp_image_confirm(const UINT8 *hash, UINT32 timeout_ms)
{
    return prv_image_mark(hash, TRUE, timeout_ms);
}

wm_SdkResult wm_ui_smp_reset(UINT32 timeout_ms)
{
    UINT8    pl[4];
    wm_cbw_t w;

    (void)timeout_ms;

    prv_w_init(&w, pl, sizeof(pl));
    prv_w_map(&w, 0u);

    /* The peripheral reboots as it answers, so no reply is waited for. */
    return prv_xfer(WM_SMP_OP_WRITE, WM_SMP_GRP_OS, WM_SMP_ID_OS_RESET,
                    pl, (UINT16)w.len, 0u, NULL, NULL);
}
