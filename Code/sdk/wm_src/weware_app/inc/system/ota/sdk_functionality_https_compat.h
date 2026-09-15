/**
 * @file sdk_functionality_https_compat.h
 * @brief WALNUT HTTPS constants and types. Include after the kernel wm_sdk_https.h.
 *
 * The kernel has no vendor returncode enum (it reports wm_SdkResult), so the
 * generic mapping applies - same values as the CG Quectel compat header.
 *
 * WALNUT-specific: the kernel (lib_wmsrc_B.a, wm_sdk_https.c.obj) exports
 * wm_sdk_https_get_response / _get_response_len and the download trio
 * _configure_ssl / _get_file_size / _read_chunk, but with DIFFERENT semantics
 * from their CG namesakes (the download calls return 0 ok / <0 fail; CG
 * expects 1 ok / 0 fail). The remap below points the CG spelling of those
 * five names at the sdk_platform_https_* dispatchers, so CG callers keep the
 * CG names and CG semantics; sdk_walnut_https.c reaches the kernel versions
 * through their own wm_sdk_https_* names.
 */
#ifndef SDK_FUNCTIONALITY_HTTPS_COMPAT_H
#define SDK_FUNCTIONALITY_HTTPS_COMPAT_H

#define SDK_HTTPS_SUCCESS                   0
#define SDK_HTTPS_FAIL                      1
#define SDK_HTTPS_SERVICE_NOT_AVAILABLE     2
#define SDK_HTTPS_INVALID_PARAMETER         3
#define SDK_HTTPS_FILE_NOT_EXIST            4
#define SDK_HTTPS_WRITE_FILE_FAIL           5
#define SDK_HTTPS_READ_FILE_FAIL            6
#define SDK_HTTPS_DNS_PARSE_FAIL            7
#define SDK_HTTPS_CONNECT_FAIL              8
#define SDK_HTTPS_HANDSHAKE_FAILED          9
#define SDK_HTTPS_TRANSFER_ERROR            10

#define SDK_HTTPS_RETURNCODE_T_DEFINED 1
#define SDK_HTTPS_RESPONSE_T_DEFINED   1
typedef UINT32 sdk_https_returncode_t;
typedef struct { INT32 status_code; INT32 method; INT32 action_content_len; UINT8* data; INT32 dataLen; } sdk_https_response_t;

/* CG-name remap for the kernel-owned symbols whose semantics differ. */
#define sdk_https_get_response            sdk_platform_https_get_response
#define sdk_https_get_response_len        sdk_platform_https_get_response_len
#define sdk_https_download_configure_ssl  sdk_platform_https_download_configure_ssl
#define sdk_https_download_get_file_size  sdk_platform_https_download_get_file_size
#define sdk_https_download_read_chunk     sdk_platform_https_download_read_chunk

#endif /* SDK_FUNCTIONALITY_HTTPS_COMPAT_H */
