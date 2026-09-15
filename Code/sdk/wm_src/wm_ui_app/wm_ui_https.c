/**
  ******************************************************************************
  * @file    wm_ui_https.c
  * @author  Walnut Medical
  * @brief   Common Gateway (WEGW) reference application - HTTPS demos.
  *
  *          Four menu handlers covering the wm_sdk_https_* API:
  *
  *            GET       a JSON endpoint, synchronously
  *            POST      a JSON body with an API-key header
  *            GET       the same endpoint asynchronously, result via a queue
  *            download  a file by ranges into storage, then verify the saved
  *                      file with wm_compare_sha256()
  *
  *          The two general-purpose sessions are split between them - the
  *          synchronous demos use one and the async demo the other - so queueing
  *          a request and then picking another menu option does not collide.
  ******************************************************************************
  * @attention
  *
  * @warning The endpoints below are development services and the API key,
  *          client id and client secret are compiled into the image in clear.
  *          They are bench credentials: rotate them before this reaches a
  *          fleet, and take them from provisioned storage rather than from a
  *          literal (see wm_sdk_storage.h) in shipping firmware.
  *
  * Copyright (c) 2026 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "wm_ui_https.h"
#include "wm_demo_certs.h"   /* wm_cacert - Amazon Root CA 1 */

/*******************************************************************************
** Endpoints - retarget the demos by editing these
******************************************************************************/
/* Returns {"timestamp":<ms since epoch>}. */
#define WM_HTTPS_GET_URL      "https://dev.walnutmedical.ai/api/soundbox/unix_timestamp"

/* Exchanges a client id/secret pair for a token. The API key travels as its own
 * header; Content-Type is emitted by the SDK because the request has a body. */
#define WM_HTTPS_POST_URL     "https://dev.walnutmedical.ai/api/soundbox/oauth"
#define WM_HTTPS_POST_HEADER  "x-api-key: ff4700d009e7ae464330665e3b163048"
#define WM_HTTPS_POST_TYPE    "application/json"
#define WM_HTTPS_POST_BODY \
    "{\"client_id\":\"f3a9cdb87256ebf142de3087\"," \
    "\"client_secret\":\"b7e23fd9ca514b6de98cb4f125987e31\"}"

/* A 6679-byte file the server serves with Accept-Ranges: bytes, so the ranged
 * download path applies. The digest is what the whole file must hash to. */
#define WM_HTTPS_FILE_URL \
    "https://walnutmedical.website/downloadDevData/1785144955097_wm_demo_certs.c?token=WgMLYoPjS56HqtD"
#define WM_HTTPS_FILE_SHA256 \
    "e50433990cdde2f55a2615050dbf7eca664d0aade747f32b887ec8a38d0b61fc"

/* Where the download lands. wm_compare_sha256() hashes a file on disk, so the
 * chunks have to be written out before they can be verified - which is what a
 * real download does anyway. */
#define WM_HTTPS_FILE_PATH    "C:/wegw_download.bin"

/*******************************************************************************
** Demo configuration
******************************************************************************/
#define WM_HTTPS_TIMEOUT        (30u)      /* seconds                          */
#define WM_HTTPS_SESSION_SYNC   (0u)       /* GET / POST                       */
#define WM_HTTPS_SESSION_ASYNC  (1u)       /* queued GET                       */
#define WM_HTTPS_PRINT_MAX      (512u)     /* console preview per response     */
#define WM_HTTPS_MON_STACK      (1024 * 12)

/* One ranged request per chunk, sized like a flash write rather than to fill
 * the session buffer - it stays under WM_SDK_HTTPS_RESP_BUF_SIZE so each chunk
 * completes in a single pass. */
#define WM_HTTPS_CHUNK          (2048u)

/*******************************************************************************
** Trust anchors, per host
**
** dev.walnutmedical.ai chains to Amazon Root CA 1 (leaf -> Amazon RSA 2048 M01
** -> Amazon Root CA 1), which is exactly the anchor wm_demo_certs.c already
** carries for the MQTT demo. The API calls therefore run with the server
** properly verified.
**
** walnutmedical.website is signed by Let's Encrypt (ISRG Root X1), which this
** image does not carry, so the download runs encrypted but with the peer
** unauthenticated. Compile that root in and point WM_HTTPS_FILE_CA at it to
** close the gap - shipping firmware should not leave a download unverified.
**
** To use the anchor provisioned on the unit instead, read it with
** wm_sdk_storage_cred_read(WM_SDK_STORAGE_CRED_ROOT_CA, ...) into a buffer that
** outlives the session and pass that below - but only for a host that chains to
** it, or verification fails and takes the handshake with it.
******************************************************************************/
#define WM_HTTPS_API_CA         wm_cacert
#define WM_HTTPS_FILE_CA        NULL

/* Async completion queue and the task that drains it; both created once. */
static void *s_https_q;
static void *s_https_mon;

/*******************************************************************************
** Shared helpers
******************************************************************************/
/* Point a session at a URL, verifying the server against @p ca (NULL to skip
 * verification - see the trust-anchor notes above). */
static BOOL wm_https_open(UINT32 idx, const char *url, const char *ca)
{
    wm_printf("tls: server verification %s\r\n", (ca != NULL) ? "on" : "OFF");

    if (wm_sdk_https_configure_ssl(idx, ca, NULL, NULL) != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("configure_ssl failed\r\n");
        return FALSE;
    }
    if (wm_sdk_https_set_params(idx, url, WM_HTTPS_TIMEOUT) != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("set_params failed (a request may still be in flight)\r\n");
        return FALSE;
    }
    return TRUE;
}

/* Why a request failed. The status separates "the server said no" from "we
 * never got that far", and the raw client code names the transport fault -
 * which is the difference between a wrong trust anchor and a missing route. */
static void wm_https_explain(UINT32 idx, const char *what)
{
    wm_printf("%s failed: http=%ld err=%ld\r\n", what,
              (long)wm_sdk_https_get_status_code(idx),
              (long)wm_sdk_https_get_last_error(idx));
    wm_printf("(err -1 connect/TLS, -2 closed by peer, -4 protocol, "
              "-5 DNS, -6 bad URL, -7 out of memory)\r\n");
}

/* The exchange result, the server's verdict on it, and the raw client code. */
static void wm_https_report(UINT32 idx, wm_SdkResult rc)
{
    wm_printf("rc=%ld http=%ld err=%ld\r\n", (long)rc,
              (long)wm_sdk_https_get_status_code(idx),
              (long)wm_sdk_https_get_last_error(idx));
}

/* Read the body to the end, previewing the first WM_HTTPS_PRINT_MAX bytes and
 * counting the rest. Reading to the end is also what releases the connection. */
static void wm_https_drain(UINT32 idx)
{
    char   buf[129];
    UINT32 n = 0, total = 0, shown = 0;

    while (1)
    {
        if (wm_sdk_https_read(idx, buf, sizeof(buf) - 1, &n) != WM_SDK_RESULT_SUCCESS)
        {
            wm_printf("\r\n<read failed after %lu bytes>\r\n", (unsigned long)total);
            return;
        }
        if (n == 0u)
            break;      /* end of body */

        total += n;
        if (shown < WM_HTTPS_PRINT_MAX)
        {
            buf[n] = '\0';
            wm_printf("%s", buf);
            shown += n;
        }
    }

    wm_printf("\r\n<body %lu bytes%s>\r\n", (unsigned long)total,
              (shown < total) ? ", truncated above" : "");
}

/*******************************************************************************
** GET (synchronous)
******************************************************************************/
void wm_ui_https_get_demo(void)
{
    wm_SdkResult rc;

    wm_printf("\r\n--- HTTPS: GET (sync) ---\r\n");
    wm_printf("%s\r\n", WM_HTTPS_GET_URL);

    if (!gf_pdp_ready)
        wm_printf("note: no PDP context is up, so this will fail\r\n");

    if (wm_sdk_https_init(WM_SDK_HTTPS_MODE_SYNC, NULL) != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("init failed\r\n");
        return;
    }
    if (!wm_https_open(WM_HTTPS_SESSION_SYNC, WM_HTTPS_GET_URL, WM_HTTPS_API_CA))
        return;

    rc = wm_sdk_https_action(WM_HTTPS_SESSION_SYNC, WM_SDK_HTTPS_ACTION_GET);
    wm_https_report(WM_HTTPS_SESSION_SYNC, rc);
    if (rc == WM_SDK_RESULT_SUCCESS)
        wm_https_drain(WM_HTTPS_SESSION_SYNC);
    else
        wm_https_explain(WM_HTTPS_SESSION_SYNC, "GET");

    wm_sdk_https_terminate(WM_HTTPS_SESSION_SYNC);
}

/*******************************************************************************
** POST (synchronous)
******************************************************************************/
void wm_ui_https_post_demo(void)
{
    /* Static because wm_sdk_https_set_data() keeps the pointer instead of copying:
     * the body must outlive the call that sets it. */
    static const char body[] = WM_HTTPS_POST_BODY;
    wm_SdkResult rc;

    wm_printf("\r\n--- HTTPS: POST ---\r\n");
    wm_printf("%s\r\n", WM_HTTPS_POST_URL);

    if (!gf_pdp_ready)
        wm_printf("note: no PDP context is up, so this will fail\r\n");

    if (wm_sdk_https_init(WM_SDK_HTTPS_MODE_SYNC, NULL) != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("init failed\r\n");
        return;
    }
    if (!wm_https_open(WM_HTTPS_SESSION_SYNC, WM_HTTPS_POST_URL, WM_HTTPS_API_CA))
        return;

    if (wm_sdk_https_set_header(WM_HTTPS_SESSION_SYNC, WM_HTTPS_POST_HEADER) != WM_SDK_RESULT_SUCCESS ||
        wm_sdk_https_set_content_type(WM_HTTPS_SESSION_SYNC, WM_HTTPS_POST_TYPE) != WM_SDK_RESULT_SUCCESS ||
        wm_sdk_https_set_data(WM_HTTPS_SESSION_SYNC, body, (UINT32)(sizeof(body) - 1u))
            != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("request setup failed\r\n");
        wm_sdk_https_terminate(WM_HTTPS_SESSION_SYNC);
        return;
    }

    wm_printf("body=%s\r\n", body);

    rc = wm_sdk_https_action(WM_HTTPS_SESSION_SYNC, WM_SDK_HTTPS_ACTION_POST);
    wm_https_report(WM_HTTPS_SESSION_SYNC, rc);
    if (rc == WM_SDK_RESULT_SUCCESS)
        wm_https_drain(WM_HTTPS_SESSION_SYNC);
    else
        wm_https_explain(WM_HTTPS_SESSION_SYNC, "POST");

    wm_sdk_https_terminate(WM_HTTPS_SESSION_SYNC);
}

/*******************************************************************************
** GET (asynchronous)
**
** The request runs on an SDK task and reports back on a queue, so the menu is
** free again the moment it is queued. The blocking receive lives in this task
** rather than in the dispatcher, for the same reason the URC demo uses a
** monitor: the dispatcher has to stay responsive.
******************************************************************************/
static void wm_https_monitor_task(void *arg)
{
    wm_SdkHttpsEvent ev;

    (void)arg;

    while (1)
    {
        if (wm_sdk_msgq_recv(s_https_q, &ev, SC_SUSPEND) != WM_SDK_RESULT_SUCCESS)
        {
            wm_sdk_task_sleep(100);   /* never spin if the receive errors out */
            continue;
        }

        wm_printf("\r\n[HTTPS] session=%u rc=%ld http=%ld ready=%lu bytes\r\n",
                  (unsigned)ev.ssl_index, (long)ev.status, (long)ev.http_code,
                  (unsigned long)ev.length);

        if (ev.status == WM_SDK_RESULT_SUCCESS)
            wm_https_drain(ev.ssl_index);

        wm_sdk_https_terminate(ev.ssl_index);
    }
}

void wm_ui_https_async_demo(void)
{
    wm_SdkResult rc;

    wm_printf("\r\n--- HTTPS: GET (async) ---\r\n");
    wm_printf("%s\r\n", WM_HTTPS_GET_URL);

    if (!gf_pdp_ready)
        wm_printf("note: no PDP context is up, so this will fail\r\n");

    if (s_https_q == NULL)
    {
        s_https_q = wm_sdk_msgq_create("HTTPSEVT", sizeof(wm_SdkHttpsEvent), 4, 0);
        if (s_https_q == NULL)
        {
            wm_printf("event queue create failed\r\n");
            return;
        }
    }

    if (s_https_mon == NULL)
    {
        s_https_mon = wm_sdk_task_create(wm_https_monitor_task, NULL, "HTTPMON", NULL,
                                      WM_HTTPS_MON_STACK, TP_TIMED_ACTIVITY);
        if (s_https_mon == NULL)
        {
            wm_printf("monitor task create failed\r\n");
            return;
        }
    }

    if (wm_sdk_https_init(WM_SDK_HTTPS_MODE_ASYNC, s_https_q) != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("init failed (a request may still be in flight)\r\n");
        return;
    }
    if (!wm_https_open(WM_HTTPS_SESSION_ASYNC, WM_HTTPS_GET_URL, WM_HTTPS_API_CA))
        return;

    rc = wm_sdk_https_action(WM_HTTPS_SESSION_ASYNC, WM_SDK_HTTPS_ACTION_GET);
    wm_printf("queued -> rc=%ld; the result will print when it arrives\r\n", (long)rc);
}

/*******************************************************************************
** Ranged download to a file, verified by SHA-256
******************************************************************************/
void wm_ui_https_download_demo(void)
{
    static char chunk[WM_HTTPS_CHUNK];
    void       *f;
    UINT32      size = 0, off = 0, n = 0, written = 0;
    int         rc;
    BOOL        complete, verified;

    wm_printf("\r\n--- HTTPS: ranged download + SHA-256 ---\r\n");
    wm_printf("%s\r\n", WM_HTTPS_FILE_URL);

    if (!gf_pdp_ready)
        wm_printf("note: no PDP context is up, so this will fail\r\n");

    /* The download calls are synchronous whatever mode is set; selecting sync
     * here just leaves the client where the other demos expect it. */
    if (wm_sdk_https_init(WM_SDK_HTTPS_MODE_SYNC, NULL) != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("init failed\r\n");
        return;
    }
    if (!wm_https_open(WM_SDK_HTTPS_DOWNLOAD_INDEX, WM_HTTPS_FILE_URL, WM_HTTPS_FILE_CA))
        return;

    /* Confirms the trust anchor wm_https_open() just set; -1 simply means none
     * was supplied, so the transfer will not authenticate the server. */
    rc = wm_sdk_https_download_configure_ssl();
    wm_printf("download tls configured -> %d%s\r\n", rc,
              (rc < 0) ? " (no CA supplied; server not authenticated)" : "");

    if (wm_sdk_https_download_get_file_size(&size) != 0 || size == 0u)
    {
        wm_https_explain(WM_SDK_HTTPS_DOWNLOAD_INDEX, "size query");
        wm_printf("either the server does not support ranges, or the trust "
                  "anchor does not sign it\r\n");
        wm_sdk_https_terminate(WM_SDK_HTTPS_DOWNLOAD_INDEX);
        return;
    }
    wm_printf("file size = %lu bytes, chunk = %lu\r\n",
              (unsigned long)size, (unsigned long)sizeof(chunk));

    /* Start from nothing: the verification hashes whatever is on disk at the
     * path, so a leftover from an earlier run would be hashed instead. */
    (void)wm_sdk_file_delete(WM_HTTPS_FILE_PATH);

    f = wm_sdk_file_open(WM_HTTPS_FILE_PATH, "wb+");
    if (f == NULL)
    {
        wm_printf("cannot open %s for writing\r\n", WM_HTTPS_FILE_PATH);
        wm_sdk_https_terminate(WM_SDK_HTTPS_DOWNLOAD_INDEX);
        return;
    }

    /* Each chunk goes straight out to flash, so the file is never held whole in
     * RAM - the shape a firmware or resource download takes. */
    while (off < size)
    {
        UINT32 want = ((size - off) < sizeof(chunk)) ? (size - off) : (UINT32)sizeof(chunk);

        if (wm_sdk_https_download_read_chunk(off, want, chunk, &n) != 0 || n == 0u)
        {
            wm_printf("chunk at offset %lu ", (unsigned long)off);
            wm_https_explain(WM_SDK_HTTPS_DOWNLOAD_INDEX, "read");
            break;
        }

        if (wm_sdk_file_write(f, chunk, n, &written) != WM_SDK_RESULT_SUCCESS || written != n)
        {
            wm_printf("write failed at offset %lu (%lu of %lu bytes)\r\n",
                      (unsigned long)off, (unsigned long)written, (unsigned long)n);
            break;
        }

        off += n;
        wm_printf("  %lu/%lu bytes\r\n", (unsigned long)off, (unsigned long)size);
    }

    complete = (off == size) ? TRUE : FALSE;

    wm_sdk_file_sync(f);
    wm_sdk_file_close(f);
    wm_sdk_https_terminate(WM_SDK_HTTPS_DOWNLOAD_INDEX);

    if (!complete)
    {
        wm_printf("download incomplete: %lu of %lu bytes - not verifying\r\n",
                  (unsigned long)off, (unsigned long)size);
        return;
    }

    wm_printf("saved to %s\r\n", WM_HTTPS_FILE_PATH);
    wm_printf("expected = %s\r\n", WM_HTTPS_FILE_SHA256);
    wm_printf("computed = ");

    /* Hashes the saved file, printing the digest it computed and its own
     * pass/fail line; the verdict below just restates it in one place. */
    verified = wm_compare_sha256((char *)WM_HTTPS_FILE_PATH,
                                 (char *)WM_HTTPS_FILE_SHA256);

    wm_printf("SHA-256 %s\r\n", verified ? "MATCH - download verified"
                                         : "MISMATCH - download corrupt");
}
