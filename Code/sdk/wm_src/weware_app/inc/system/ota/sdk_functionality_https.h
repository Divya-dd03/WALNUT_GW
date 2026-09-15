/**
 * @file sdk_functionality_https.h
 * @brief HTTPS functionality API - full request and chunked download
 *
 * Firmware uses this API only; implementations are per-SDK (SIMCOM, Quectel,
 * WALNUT).
 *
 * WALNUT notes:
 *  - The kernel (lib_wmsrc_B.a, wm_sdk_https.c.obj) exports the low-level
 *    HTTPS client as wm_sdk_https_*, a namespace separate from the CG
 *    sdk_https_* names below:
 *      - wm_sdk_https_init / _configure_ssl / _set_params / _set_data /
 *        _action / _read / _terminate are called directly, by CG callers and
 *        by the backend alike (their wm_SdkResult contract already matches
 *        CG's low-level API);
 *      - CG names the kernel has no equivalent for (sdk_https_request,
 *        sdk_https_download_init / _set_params / _terminate,
 *        sdk_https_handle_urc) are real functions in the dispatcher;
 *      - CG names whose kernel equivalent has DIFFERENT semantics are
 *        remapped by the compat header to sdk_platform_https_* dispatchers
 *        that keep the CG contract (see sdk_functionality_https_compat.h).
 *        CG callers keep writing the CG names below.
 */

#ifndef SDK_FUNCTIONALITY_HTTPS_H
#define SDK_FUNCTIONALITY_HTTPS_H

#include <stddef.h>

/* Kernel HTTPS API: low-level wm_sdk_https_* prototypes, session/mode constants
 * (WM_SDK_HTTPS_SESSION_MAX, WM_SDK_HTTPS_DOWNLOAD_INDEX, WM_SDK_HTTPS_MODE_*,
 * WM_SDK_HTTPS_ACTION_*) and wm_SdkResult/UINT32 via wm_sdk_types.h. Included BEFORE
 * the compat remap so the kernel prototypes keep their real names. */
#include "wm_sdk_https.h"

/* HTTPS return code constants and remap: WALNUT SDK compat header
 * (CG loads the chosen SDK's compat header via sdk_platform.h). */
#include "ota/sdk_functionality_https_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* HTTPS method constants (API contract, not SDK-specific) */
#define SDK_HTTPS_METHOD_GET  0
#define SDK_HTTPS_METHOD_POST 1

/* HTTPS data transmission channel */
#define SDK_HTTPS_DATA_TX_SERIAL 0
#define SDK_HTTPS_DATA_TX_USB20  1

/**
 * HTTPS functionality operations - implemented by each SDK.
 */
typedef struct SdkHttpsFunctionalityOps {
    int (*request)(int method, const char *url, const char *payload, const char *content_type,
                   char *resp_buf, size_t resp_buf_size, size_t *resp_len);
    const char* (*get_response)(void);
    UINT32 (*get_response_len)(void);

    sdk_https_returncode_t (*download_init)(int channel, void *msgq);
    int (*download_configure_ssl)(void);
    sdk_https_returncode_t (*download_set_params)(const char *url);
    int (*download_get_file_size)(UINT32 *file_size);
    int (*download_read_chunk)(UINT32 offset, UINT32 size, char *buffer, UINT32 *bytes_read);
    sdk_https_returncode_t (*download_terminate)(void);

    wm_SdkResult (*https_init)(UINT32 tx_mode, void* msgq);
    wm_SdkResult (*https_configure_ssl)(UINT32 ssl_index, const char* ca_cert, const char* client_cert, const char* client_key);
    wm_SdkResult (*https_set_params)(UINT32 ssl_index, const char* url, UINT32 timeout);
    wm_SdkResult (*https_set_data)(UINT32 ssl_index, const char* data, UINT32 data_len);
    wm_SdkResult (*https_action)(UINT32 ssl_index, UINT32 action);
    wm_SdkResult (*https_read)(UINT32 ssl_index, void* buffer, UINT32 size, UINT32* bytes_read);
    wm_SdkResult (*https_terminate)(UINT32 ssl_index);
    wm_SdkResult (*https_handle_urc)(void* msgq, void (*callback)(void*));
} SdkHttpsFunctionalityOps;

/**
 * Register HTTPS functionality ops (called by platform during SDK init).
 * Internal use only.
 */
void sdk_platform_register_https_ops(const SdkHttpsFunctionalityOps *ops);

/* --- Low-level HTTPS API (used by firmware https_ops and SDK impl) ---
 * WALNUT: wm_sdk_https_init/.../terminate come from the kernel (declared by
 * wm_sdk_https.h above); only the URC pump is dispatched. */
wm_SdkResult sdk_https_handle_urc(void* msgq, void (*callback)(void*));

/* --- Public API used by firmware --- */

int sdk_https_request(int method, const char *url, const char *payload, const char *content_type,
                      char *resp_buf, size_t resp_buf_size, size_t *resp_len);
const char* sdk_https_get_response(void);
UINT32 sdk_https_get_response_len(void);

sdk_https_returncode_t sdk_https_download_init(int channel, void *msgq);
int sdk_https_download_configure_ssl(void);
sdk_https_returncode_t sdk_https_download_set_params(const char *url);
int sdk_https_download_get_file_size(UINT32 *file_size);
int sdk_https_download_read_chunk(UINT32 offset, UINT32 size, char *buffer, UINT32 *bytes_read);
sdk_https_returncode_t sdk_https_download_terminate(void);

#ifdef __cplusplus
}
#endif

#endif /* SDK_FUNCTIONALITY_HTTPS_H */
