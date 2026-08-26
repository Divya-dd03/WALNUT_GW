/**
 * @file sdk_walnut_https.c
 * @brief WALNUT HTTPS functionality - full request and chunked download.
 *
 * Implements the HTTPS functionality API using the kernel sdk_https_* client
 * (lib_wmsrc_B.a) and native APIs where needed (e.g. CONTENT type override).
 *
 * Behaviour vs the SIMCOM implementation (sdk_simcom_https.c):
 * - request(): same shape as SIMCOM. SIMCOM: sAPI_HttpAction() then block on
 *   the URC message queue for the action result, then sAPI_HttpRead() and
 *   block again for the read URC. Walnut: the client is initialised in
 *   SDK_HTTPS_MODE_ASYNC with an adapter-owned queue; sdk_https_action()
 *   returns immediately (the request - DNS/TLS/exchange - runs on the kernel
 *   HTTPS worker task, which carries the ~16 KB TLS stack), and the adapter
 *   blocks on sdk_msgq_recv() for the SdkHttpsEvent, exactly where SIMCOM
 *   waits for its action URC. The body is then drained with sdk_https_read()
 *   (needs ~8 KB of caller stack) into the same static 4 KB response buffer.
 *   As in CG, only a 2xx status counts as success; the SIMCOM
 *   sdk_https_response_t URC payload maps to SdkHttpsEvent.status/http_code.
 * - download_*(): CG return conventions (1 ok / 0 fail for
 *   get_file_size/read_chunk, 0 ok for configure_ssl) over the kernel's
 *   0 ok / <0 fail ranged-download calls on the reserved
 *   SDK_HTTPS_DOWNLOAD_INDEX session. These kernel helpers are SYNCHRONOUS
 *   regardless of the init mode (sdk_https.h) and need ~16 KB of caller
 *   stack; there is no async variant and no access to the Content-Length /
 *   Content-Range headers that would allow an async ranged download to be
 *   built on the general sessions. The size query is the only source of the
 *   file size, so the caller (OTA on WEMAIN) must be stack-sized for it.
 * - configure_ssl: CG/SIMCOM configures TLS without server verification and
 *   reports success; the kernel namesake merely reports whether a root CA is
 *   present. To keep the CG download flow working (no CA is provisioned
 *   through this path), the adapter configures the session with no CA and
 *   returns 0 - encrypted but unauthenticated, exactly like the SIMCOM
 *   build. Wire a stored root CA in here later to close that gap.
 * - SIMCOM-only concepts accepted and ignored: the TX channel (USB/serial)
 *   and the caller's URC message queue (the kernel needs a queue created
 *   with msg_size == sizeof(SdkHttpsEvent), so the adapter owns its own,
 *   as SIMCOM's request path owns g_https_urc_msgq). download_init(channel,
 *   msgq) therefore succeeds on the first call and the OTA engine's
 *   USB->serial fallback never runs.
 */

#include "wm_global.h"
#include "ota/sdk_functionality_https.h"
#include "ota/sdk_walnut_https.h"
#include "sdk_os.h"
#include "sdk_log.h"
#include <string.h>

/* This file implements the walnut backend and talks to the kernel
 * sdk_https_* API under its real names - drop the CG compat remap
 * (see sdk_functionality_https_compat.h). */
#undef sdk_https_get_response
#undef sdk_https_get_response_len
#undef sdk_https_download_configure_ssl
#undef sdk_https_download_get_file_size
#undef sdk_https_download_read_chunk

#define HTTPS_RESP_BUF_SIZE 4096

/* Max wait for the async completion event. SIMCOM waits 30 s for the action
 * URC; the kernel's per-operation transport timeout is 30 s
 * (SDK_HTTPS_DEFAULT_TIMEOUT) and a request has several operations (DNS,
 * connect, TLS, send, receive), so allow for a couple of them. */
#define HTTPS_EVENT_WAIT_MS         60000U
#define HTTPS_EVENT_QUEUE_DEPTH     4U

/* General-purpose session used by request(); the download path uses the
 * reserved SDK_HTTPS_DOWNLOAD_INDEX. */
#define HTTPS_REQUEST_SESSION       0U

static void *g_https_evt_msgq = NULL;    /* SdkHttpsEvent completion queue */
static char g_https_resp_buf[HTTPS_RESP_BUF_SIZE];
static UINT32 g_https_resp_len = 0;
static BOOL g_https_initialized = FALSE;

static BOOL g_download_initialized = FALSE;

/* Low-level HTTPS functionality impl: sdk_https_init / _configure_ssl /
 * _set_params / _set_data / _action / _read / _terminate are the kernel
 * wrappers themselves (CG-compatible contract) - no per-call *_impl
 * wrappers are needed; the ops table points at them directly. */

static SdkResult https_handle_urc_impl(void* msgq, void (*callback)(void*))
{
    /* No URC pump on walnut: the kernel posts the SdkHttpsEvent to the
     * adapter's queue itself and request() consumes it inline. */
    (void)msgq;
    (void)callback;
    return SDK_RESULT_SUCCESS;
}

/* Lazily create the completion queue and put the client in async mode.
 * sdk_https_init is idempotent; it rejects only a request already in flight. */
static BOOL https_client_init_async(void)
{
    if (!g_https_evt_msgq) {
        g_https_evt_msgq = sdk_msgq_create("https_evt_msgq", sizeof(SdkHttpsEvent),
                                           HTTPS_EVENT_QUEUE_DEPTH, 0);
        if (!g_https_evt_msgq) {
            sdk_log_error("HTTPS: event queue create failed");
            return FALSE;
        }
    }
    if (sdk_https_init(SDK_HTTPS_MODE_ASYNC, g_https_evt_msgq) != SDK_RESULT_SUCCESS) {
        sdk_log_error("HTTPS: init failed (request in flight?)");
        return FALSE;
    }
    return TRUE;
}

/*
 * Wait for the completion event of @p session (SIMCOM: https_handle_action_urc).
 * Events for other sessions are discarded - only one request is driven at a
 * time through this adapter. Returns 1 when the exchange completed with a 2xx
 * status, 0 otherwise.
 */
static int https_wait_action_event(UINT32 session)
{
    SdkHttpsEvent ev;
    UINT32 waited_ms = 0;

    while (waited_ms < HTTPS_EVENT_WAIT_MS) {
        UINT32 t0 = sdk_get_ticks();
        memset(&ev, 0, sizeof(ev));
        if (sdk_msgq_recv(g_https_evt_msgq, &ev, HTTPS_EVENT_WAIT_MS - waited_ms)
                != SDK_RESULT_SUCCESS) {
            sdk_log_error("HTTPS request: no completion event within %lu ms",
                          (unsigned long)HTTPS_EVENT_WAIT_MS);
            return 0;
        }
        if (ev.type == SDK_HTTPS_EVT_ACTION_DONE && ev.ssl_index == session)
            break;
        sdk_log_warning("HTTPS request: ignoring event type %u for session %u",
                        (unsigned)ev.type, (unsigned)ev.ssl_index);
        waited_ms += sdk_get_ticks() - t0;
    }
    if (waited_ms >= HTTPS_EVENT_WAIT_MS)
        return 0;

    if (ev.status != SDK_RESULT_SUCCESS) {
        sdk_log_error("HTTPS request: action failed (rc=%ld err=%ld)",
                      (long)ev.status, (long)sdk_https_get_last_error(session));
        return 0;
    }
    /* CG counts only a 2xx as success (SIMCOM checks the action URC's
     * status code); the kernel reports the exchange and the verdict
     * separately. */
    if (ev.http_code < 200 || ev.http_code >= 300) {
        sdk_log_error("HTTPS request: status %ld", (long)ev.http_code);
        return 0;
    }
    return 1;
}

static void https_request_abort(UINT32 session)
{
    /* Terminate rejects a request in flight (SDK_RESULT_BUSY); in that case
     * the session is released when the event arrives on the next request. */
    sdk_https_terminate(session);
    g_https_initialized = FALSE;
}

static int sdk_walnut_https_request_impl(int method, const char *url, const char *payload,
                                         const char *content_type, char *resp_buf,
                                         size_t resp_buf_size, size_t *resp_len)
{
    const UINT32 session = HTTPS_REQUEST_SESSION;
    UINT32 action;
    UINT32 used = 0;

    if (!url || strlen(url) == 0) return 0;
    if (method == SDK_HTTPS_METHOD_POST && (!payload || strlen(payload) == 0)) return 0;
    if (method == SDK_HTTPS_METHOD_GET)
        action = SDK_HTTPS_ACTION_GET;
    else if (method == SDK_HTTPS_METHOD_POST)
        action = SDK_HTTPS_ACTION_POST;
    else
        return 0;
    if (!content_type) content_type = (method == SDK_HTTPS_METHOD_POST) ? "application/json" : "*/*";

    if (!https_client_init_async())
        return 0;
    g_https_initialized = TRUE;

    /* TLS without server verification - CG/SIMCOM behaviour (sslversion +
     * SNI only, no trust anchor). */
    if (sdk_https_configure_ssl(session, NULL, NULL, NULL) != SDK_RESULT_SUCCESS) {
        https_request_abort(session);
        return 0;
    }

    if (sdk_https_set_params(session, url, 0) != SDK_RESULT_SUCCESS) {
        https_request_abort(session);
        return 0;
    }

    /* SIMCOM sends ACCEPT */
    if (sdk_https_set_header(session, "Accept: */*") != SDK_RESULT_SUCCESS) {
        https_request_abort(session);
        return 0;
    }

    /* WALNUT-specific: override CONTENT type for POST */
    if (method == SDK_HTTPS_METHOD_POST && content_type) {
        if (sdk_https_set_content_type(session, content_type) != SDK_RESULT_SUCCESS) {
            /* continue anyway */
        }
    }

    if (method == SDK_HTTPS_METHOD_POST && payload) {
        UINT32 payload_len = (UINT32)strlen(payload);
        /* Not copied by the kernel: the caller's buffer must stay valid until
         * the completion event - guaranteed, since this call blocks on it. */
        if (sdk_https_set_data(session, payload, payload_len) != SDK_RESULT_SUCCESS) {
            https_request_abort(session);
            return 0;
        }
    }

    memset(g_https_resp_buf, 0, sizeof(g_https_resp_buf));
    g_https_resp_len = 0;

    /* Async: returns as soon as the request is queued on the kernel worker. */
    if (sdk_https_action(session, action) != SDK_RESULT_SUCCESS) {
        sdk_log_error("HTTPS request: action not accepted (err=%ld)",
                      (long)sdk_https_get_last_error(session));
        https_request_abort(session);
        return 0;
    }

    /* SIMCOM: wait for the action URC. */
    if (!https_wait_action_event(session)) {
        https_request_abort(session);
        return 0;
    }

    /* SIMCOM: read + read URC. Drain the body into the static buffer; like
     * SIMCOM, anything past the buffer is dropped. */
    while (used < (UINT32)sizeof(g_https_resp_buf) - 1) {
        UINT32 n = 0;
        if (sdk_https_read(session, g_https_resp_buf + used,
                           (UINT32)sizeof(g_https_resp_buf) - 1 - used,
                           &n) != SDK_RESULT_SUCCESS) {
            https_request_abort(session);
            g_https_resp_len = 0;
            return 0;
        }
        if (n == 0)
            break;
        used += n;
    }
    g_https_resp_buf[used] = '\0';
    g_https_resp_len = used;

    if (resp_buf && resp_buf_size > 0 && g_https_resp_len > 0) {
        size_t copy_len = (g_https_resp_len < resp_buf_size) ? g_https_resp_len : (resp_buf_size - 1);
        memcpy(resp_buf, g_https_resp_buf, copy_len);
        resp_buf[copy_len] = '\0';
        if (resp_len) *resp_len = copy_len;
    } else if (resp_len) {
        *resp_len = g_https_resp_len;
    }

    sdk_https_terminate(session);
    g_https_initialized = FALSE;
    return 1;
}

static const char* sdk_walnut_https_get_response_impl(void)
{
    return (g_https_resp_len > 0) ? g_https_resp_buf : NULL;
}

static UINT32 sdk_walnut_https_get_response_len_impl(void)
{
    return g_https_resp_len;
}

static sdk_https_returncode_t sdk_walnut_https_download_init_impl(int channel, void *msgq)
{
    /* SIMCOM routes the client through USB or serial and delivers results
     * on the caller's URC queue; the walnut ranged-download helpers are
     * in-process and synchronous, so both are accepted and ignored. The
     * client is (re)initialised in async mode with the adapter queue so the
     * mode is consistent with request(); the download helpers do not use it. */
    (void)channel;
    (void)msgq;

    if (!https_client_init_async())
        return SDK_HTTPS_FAIL;
    g_download_initialized = TRUE;
    return SDK_HTTPS_SUCCESS;
}

static int sdk_walnut_https_download_configure_ssl_impl(void)
{
    if (!g_download_initialized)
        return -1;
    /* No CA: encrypted, server unauthenticated - see the file header. */
    return (sdk_https_configure_ssl(SDK_HTTPS_DOWNLOAD_INDEX, NULL, NULL, NULL)
            == SDK_RESULT_SUCCESS) ? 0 : -1;
}

/* Download URL, kept so every ranged chunk can re-arm the session.
 *
 * Field observation (26 Aug 2026, stage nginx server): size query and the
 * chunk at offset 0 succeed, then the chunk at offset 1024 gets HTTP 404 with
 * err=0 - although the same ranges served by curl (incl. two on one keep-alive
 * connection) all return 206. The path appears to be lost by the kernel when
 * it re-issues a request on the already-configured session / reused
 * connection. Work around it by re-pointing the session at the URL and forcing
 * "Connection: close" before every chunk, so each range is a fresh request. */
static char g_download_url[SDK_HTTPS_URL_MAX] = {0};

static BOOL https_download_rearm(void)
{
    if (g_download_url[0] == '\0')
        return FALSE;
    if (sdk_https_set_params(SDK_HTTPS_DOWNLOAD_INDEX, g_download_url, 0) != SDK_RESULT_SUCCESS) {
        sdk_log_error("HTTPS download: re-arm set_params failed (err=%ld)",
                      (long)sdk_https_get_last_error(SDK_HTTPS_DOWNLOAD_INDEX));
        return FALSE;
    }
    /* Not fatal if the download session ignores custom headers. */
    (void)sdk_https_set_header(SDK_HTTPS_DOWNLOAD_INDEX, "Connection: close");
    return TRUE;
}

static sdk_https_returncode_t sdk_walnut_https_download_set_params_impl(const char *url)
{
    if (!url || strlen(url) == 0) return SDK_HTTPS_INVALID_PARAMETER;
    if (strlen(url) >= sizeof(g_download_url)) {
        sdk_log_error("HTTPS download: URL too long (%u >= %u)",
                      (unsigned)strlen(url), (unsigned)sizeof(g_download_url));
        return SDK_HTTPS_INVALID_PARAMETER;
    }
    strncpy(g_download_url, url, sizeof(g_download_url) - 1);
    g_download_url[sizeof(g_download_url) - 1] = '\0';
    return https_download_rearm() ? SDK_HTTPS_SUCCESS : SDK_HTTPS_FAIL;
}

static int sdk_walnut_https_download_get_file_size_impl(UINT32 *file_size)
{
    if (!file_size || !g_download_initialized) return 0;
    /* Kernel: 0 ok / <0 fail -> CG: 1 ok / 0 fail. A size of 0 is a
     * failure in CG (the server cannot serve ranges either). Synchronous
     * (blocks the caller for one HEAD/ranged exchange). */
    if (sdk_https_download_get_file_size(file_size) != 0) {
        sdk_log_error("HTTPS download: size query failed (err=%ld)",
                      (long)sdk_https_get_last_error(SDK_HTTPS_DOWNLOAD_INDEX));
        return 0;
    }
    if (*file_size == 0) return 0;
    return 1;
}

static int sdk_walnut_https_download_read_chunk_impl(UINT32 offset, UINT32 size, char *buffer,
                                                     UINT32 *bytes_read)
{
    if (!buffer || !bytes_read || size == 0 || !g_download_initialized) {
        if (bytes_read) *bytes_read = 0;
        return 0;
    }
    /* Each call is a self-contained synchronous ranged request; kernel:
     * 0 ok / <0 fail. Re-arm the session first (see g_download_url) and
     * retry once - the kernel documents a failed chunk as simply retryable. */
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!https_download_rearm()) {
            *bytes_read = 0;
            return 0;
        }
        if (sdk_https_download_read_chunk(offset, size, buffer, bytes_read) == 0) {
            if (attempt > 0)
                sdk_log_warning("HTTPS download: chunk at %lu ok on retry", (unsigned long)offset);
            return 1;
        }
        sdk_log_error("HTTPS download: chunk at %lu failed (attempt %d, http=%ld err=%ld)",
                      (unsigned long)offset, attempt + 1,
                      (long)sdk_https_get_status_code(SDK_HTTPS_DOWNLOAD_INDEX),
                      (long)sdk_https_get_last_error(SDK_HTTPS_DOWNLOAD_INDEX));
    }
    *bytes_read = 0;
    return 0;
}

static sdk_https_returncode_t sdk_walnut_https_download_terminate_impl(void)
{
    if (!g_download_initialized) return SDK_HTTPS_FAIL;
    SdkResult ret = sdk_https_terminate(SDK_HTTPS_DOWNLOAD_INDEX);
    g_download_initialized = FALSE;
    g_download_url[0] = '\0';
    return (ret == SDK_RESULT_SUCCESS) ? SDK_HTTPS_SUCCESS : SDK_HTTPS_FAIL;
}

static const SdkHttpsFunctionalityOps s_walnut_https_ops = {
    .request                 = sdk_walnut_https_request_impl,
    .get_response             = sdk_walnut_https_get_response_impl,
    .get_response_len         = sdk_walnut_https_get_response_len_impl,
    .download_init            = sdk_walnut_https_download_init_impl,
    .download_configure_ssl   = sdk_walnut_https_download_configure_ssl_impl,
    .download_set_params      = sdk_walnut_https_download_set_params_impl,
    .download_get_file_size   = sdk_walnut_https_download_get_file_size_impl,
    .download_read_chunk      = sdk_walnut_https_download_read_chunk_impl,
    .download_terminate       = sdk_walnut_https_download_terminate_impl,
    .https_init               = sdk_https_init,
    .https_configure_ssl       = sdk_https_configure_ssl,
    .https_set_params         = sdk_https_set_params,
    .https_set_data           = sdk_https_set_data,
    .https_action             = sdk_https_action,
    .https_read               = sdk_https_read,
    .https_terminate          = sdk_https_terminate,
    .https_handle_urc         = https_handle_urc_impl,
};

const SdkHttpsFunctionalityOps* sdk_walnut_get_https_functionality_ops(void)
{
    return &s_walnut_https_ops;
}
