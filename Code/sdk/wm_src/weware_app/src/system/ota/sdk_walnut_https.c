/**
 * @file sdk_walnut_https.c
 * @brief WALNUT HTTPS functionality - full request and chunked download.
 *
 * Implements the HTTPS functionality API using the kernel sdk_https_* client
 * (lib_wmsrc_B.a) and native APIs where needed (e.g. CONTENT type override).
 *
 * Behaviour vs the SIMCOM implementation:
 * - request(): SIMCOM drives an init/para/action/read URC dance over a
 *   message queue; here the whole exchange runs synchronously on a
 *   general-purpose session. As in CG, only a 2xx status counts as success
 *   and the response (first 4 KB) is kept in a static buffer that
 *   get_response() serves after the session is torn down.
 * - download_*(): CG return conventions (1 ok / 0 fail for
 *   get_file_size/read_chunk, 0 ok for configure_ssl) over the kernel's
 *   0 ok / <0 fail ranged-download calls on the reserved
 *   SDK_HTTPS_DOWNLOAD_INDEX session.
 * - configure_ssl: CG/SIMCOM configures TLS without server verification and
 *   reports success; the kernel namesake merely reports whether a root CA is
 *   present. To keep the CG download flow working (no CA is provisioned
 *   through this path), the adapter configures the session with no CA and
 *   returns 0 - encrypted but unauthenticated, exactly like the SIMCOM
 *   build. Wire a stored root CA in here later to close that gap.
 * - SIMCOM-only concepts accepted and ignored: the TX channel (USB/serial)
 *   and the URC message queue - the sync kernel client needs neither, so
 *   download_init(channel, msgq) succeeds on the first call and the OTA
 *   engine's USB->serial fallback never runs.
 *
 * Task/stack note: sdk_https_action() in sync mode needs ~16 KB of stack and
 * sdk_https_read() ~8 KB, so call sdk_https_request() / the download engine
 * only from a task sized accordingly.
 */

#include "wm_global.h"
#include "ota/sdk_functionality_https.h"
#include "ota/sdk_walnut_https.h"
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
    /* No URC pump on walnut: the sync client completes in the caller and
     * the async client posts SdkHttpsEvent to the caller's queue itself. */
    (void)msgq;
    (void)callback;
    return SDK_RESULT_SUCCESS;
}

static int sdk_walnut_https_request_impl(int method, const char *url, const char *payload,
                                         const char *content_type, char *resp_buf,
                                         size_t resp_buf_size, size_t *resp_len)
{
    const UINT32 session = 0;   /* general-purpose kernel session */
    UINT32 action;
    INT32 status;
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

    /* Idempotent; rejects only a request already in flight. */
    if (sdk_https_init(SDK_HTTPS_MODE_SYNC, NULL) != SDK_RESULT_SUCCESS)
        return 0;
    g_https_initialized = TRUE;

    /* TLS without server verification - CG/SIMCOM behaviour (sslversion +
     * SNI only, no trust anchor). */
    if (sdk_https_configure_ssl(session, NULL, NULL, NULL) != SDK_RESULT_SUCCESS) {
        sdk_https_terminate(session);
        g_https_initialized = FALSE;
        return 0;
    }

    if (sdk_https_set_params(session, url, 0) != SDK_RESULT_SUCCESS) {
        sdk_https_terminate(session);
        g_https_initialized = FALSE;
        return 0;
    }

    /* SIMCOM sends ACCEPT */
    if (sdk_https_set_header(session, "Accept: */*") != SDK_RESULT_SUCCESS) {
        sdk_https_terminate(session);
        g_https_initialized = FALSE;
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
        /* Not copied by the kernel; stays valid - the action below is
         * synchronous within this call. */
        if (sdk_https_set_data(session, payload, payload_len) != SDK_RESULT_SUCCESS) {
            sdk_https_terminate(session);
            g_https_initialized = FALSE;
            return 0;
        }
    }

    memset(g_https_resp_buf, 0, sizeof(g_https_resp_buf));
    g_https_resp_len = 0;

    if (sdk_https_action(session, action) != SDK_RESULT_SUCCESS) {
        sdk_log_error("HTTPS request: action failed (err=%ld)",
                      (long)sdk_https_get_last_error(session));
        sdk_https_terminate(session);
        g_https_initialized = FALSE;
        return 0;
    }

    /* CG counts only a 2xx as success (SIMCOM checks the action URC's
     * status code); the kernel reports the exchange and the verdict
     * separately. */
    status = sdk_https_get_status_code(session);
    if (status < 200 || status >= 300) {
        sdk_log_error("HTTPS request: status %ld", (long)status);
        sdk_https_terminate(session);
        g_https_initialized = FALSE;
        return 0;
    }

    /* Drain the body into the static buffer; like SIMCOM, anything past
     * the buffer is dropped. */
    while (used < (UINT32)sizeof(g_https_resp_buf) - 1) {
        UINT32 n = 0;
        if (sdk_https_read(session, g_https_resp_buf + used,
                           (UINT32)sizeof(g_https_resp_buf) - 1 - used,
                           &n) != SDK_RESULT_SUCCESS) {
            sdk_https_terminate(session);
            g_https_initialized = FALSE;
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
     * on a URC queue; the walnut client is in-process and synchronous, so
     * both are accepted and ignored. */
    (void)channel;
    (void)msgq;

    if (sdk_https_init(SDK_HTTPS_MODE_SYNC, NULL) != SDK_RESULT_SUCCESS)
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

static sdk_https_returncode_t sdk_walnut_https_download_set_params_impl(const char *url)
{
    if (!url || strlen(url) == 0) return SDK_HTTPS_INVALID_PARAMETER;
    return (sdk_https_set_params(SDK_HTTPS_DOWNLOAD_INDEX, url, 0) == SDK_RESULT_SUCCESS) ?
           SDK_HTTPS_SUCCESS : SDK_HTTPS_FAIL;
}

static int sdk_walnut_https_download_get_file_size_impl(UINT32 *file_size)
{
    if (!file_size || !g_download_initialized) return 0;
    /* Kernel: 0 ok / <0 fail -> CG: 1 ok / 0 fail. A size of 0 is a
     * failure in CG (the server cannot serve ranges either). */
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
    /* Each call is a self-contained ranged request; kernel: 0 ok / <0 fail. */
    if (sdk_https_download_read_chunk(offset, size, buffer, bytes_read) != 0) {
        sdk_log_error("HTTPS download: chunk at %lu failed (http=%ld err=%ld)",
                      (unsigned long)offset,
                      (long)sdk_https_get_status_code(SDK_HTTPS_DOWNLOAD_INDEX),
                      (long)sdk_https_get_last_error(SDK_HTTPS_DOWNLOAD_INDEX));
        *bytes_read = 0;
        return 0;
    }
    return 1;
}

static sdk_https_returncode_t sdk_walnut_https_download_terminate_impl(void)
{
    if (!g_download_initialized) return SDK_HTTPS_FAIL;
    SdkResult ret = sdk_https_terminate(SDK_HTTPS_DOWNLOAD_INDEX);
    g_download_initialized = FALSE;
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
