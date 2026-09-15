/**
 * @file sdk_functionality_https.c
 * @brief HTTPS functionality dispatcher - forwards to SDK implementation
 *        (CG file name: sdk_platform_https.c).
 *
 *        Walnut is the only backend on this board, so it is also the lazy
 *        default (same pattern as sdk_platform_tcp.c);
 *        sdk_platform_register_https_ops() can override at any time.
 *
 *        WALNUT: the CG low-level dispatchers (wm_sdk_https_init/.../terminate)
 *        are absent on purpose - the kernel wrappers in lib_wmsrc_B.a own
 *        those names with a CG-compatible contract and cannot be shadowed
 *        (wm_sdk_ota.c.obj references that archive member). The five functions
 *        whose CG names collide with kernel symbols of different semantics
 *        are written with their CG names below; the compat remap in
 *        sdk_functionality_https_compat.h renames them to
 *        sdk_platform_https_* at compile time.
 */

#include "ota/sdk_functionality_https.h"
#include "ota/sdk_walnut_https.h"
#include <string.h>

static const SdkHttpsFunctionalityOps *g_https_ops = NULL;

void sdk_platform_register_https_ops(const SdkHttpsFunctionalityOps *ops)
{
    g_https_ops = ops;
}

static const SdkHttpsFunctionalityOps *https_ops(void)
{
    if (!g_https_ops)
        g_https_ops = sdk_walnut_get_https_functionality_ops();
    return g_https_ops;
}

int sdk_https_request(int method, const char *url, const char *payload, const char *content_type,
                      char *resp_buf, size_t resp_buf_size, size_t *resp_len)
{
    const SdkHttpsFunctionalityOps *ops = https_ops();
    if (!ops || !ops->request)
        return 0;
    return ops->request(method, url, payload, content_type, resp_buf, resp_buf_size, resp_len);
}

const char* sdk_https_get_response(void)
{
    const SdkHttpsFunctionalityOps *ops = https_ops();
    if (!ops || !ops->get_response)
        return NULL;
    return ops->get_response();
}

UINT32 sdk_https_get_response_len(void)
{
    const SdkHttpsFunctionalityOps *ops = https_ops();
    if (!ops || !ops->get_response_len)
        return 0;
    return ops->get_response_len();
}

sdk_https_returncode_t sdk_https_download_init(int channel, void *msgq)
{
    const SdkHttpsFunctionalityOps *ops = https_ops();
    if (!ops || !ops->download_init)
        return SDK_HTTPS_FAIL;
    return ops->download_init(channel, msgq);
}

int sdk_https_download_configure_ssl(void)
{
    const SdkHttpsFunctionalityOps *ops = https_ops();
    if (!ops || !ops->download_configure_ssl)
        return -1;
    return ops->download_configure_ssl();
}

sdk_https_returncode_t sdk_https_download_set_params(const char *url)
{
    const SdkHttpsFunctionalityOps *ops = https_ops();
    if (!ops || !ops->download_set_params)
        return SDK_HTTPS_FAIL;
    return ops->download_set_params(url);
}

int sdk_https_download_get_file_size(UINT32 *file_size)
{
    const SdkHttpsFunctionalityOps *ops = https_ops();
    if (!ops || !ops->download_get_file_size || !file_size)
        return 0;
    return ops->download_get_file_size(file_size);
}

int sdk_https_download_read_chunk(UINT32 offset, UINT32 size, char *buffer, UINT32 *bytes_read)
{
    const SdkHttpsFunctionalityOps *ops = https_ops();
    if (!ops || !ops->download_read_chunk || !buffer || !bytes_read)
        return 0;
    return ops->download_read_chunk(offset, size, buffer, bytes_read);
}

sdk_https_returncode_t sdk_https_download_terminate(void)
{
    const SdkHttpsFunctionalityOps *ops = https_ops();
    if (!ops || !ops->download_terminate)
        return SDK_HTTPS_FAIL;
    return ops->download_terminate();
}

wm_SdkResult sdk_https_handle_urc(void* msgq, void (*callback)(void*))
{
    const SdkHttpsFunctionalityOps *ops = https_ops();
    if (!ops || !ops->https_handle_urc)
        return WM_SDK_RESULT_NOT_SUPPORTED;
    return ops->https_handle_urc(msgq, callback);
}
