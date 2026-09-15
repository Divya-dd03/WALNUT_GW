/**
 ******************************************************************************
 * @file    wm_sdk_ota.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - OTA firmware download / app-package API.
 *
 *          A firmware update runs in three separate, explicit stages:
 *
 *            1. DOWNLOAD  fetch the image over HTTPS into a staging file on the
 *                         C: file system. wm_sdk_ota_download_init() ->
 *                         _configure_ssl() -> _set_params() -> _set_image_type()
 *                         -> _action() -> _terminate().
 *            2. VERIFY    wm_sdk_ota_verify_image() checks the staged file against
 *                         an expected SHA-256. Nothing else validates the image,
 *                         so skipping this stage means flashing unchecked bytes.
 *            3. APPLY     wm_sdk_ota_app_update() arms the bootloader, then
 *                         wm_sdk_ota_app_update_restart() reboots into the update.
 *
 *          This file-staging flow applies to the application image only. The
 *          kernel delta patch (DFOTA) is not applied this way on this platform
 *          - see wm_sdk_ota_mini_dfota_init()/_start() below.
 *
 *          Transport and staging are built on the SDK's own HTTPS and file APIs:
 *          the download is a sequence of ranged GETs on the reserved
 *          WM_SDK_HTTPS_DOWNLOAD_INDEX session (wm_sdk_https_download_*), each chunk
 *          written straight out with wm_sdk_file_write(), so the image is never held
 *          whole in RAM.
 *
 *          Threading: one global download session, serialised by a module mutex.
 *          A second caller gets WM_SDK_RESULT_BUSY rather than a corrupted staging
 *          file. wm_sdk_ota_download_action() blocks for the whole transfer - which
 *          for a full application image is minutes, not seconds - so run it on a
 *          task of its own, never on a UI or dispatcher task.
 *
 *          Stack: wm_sdk_ota_download_action() and wm_sdk_ota_download_read_chunk()
 *          inherit the HTTPS stack budget of roughly 16 KB (see wm_sdk_https.h).
 *
 *          The ssl_index argument on the download calls is accepted for API
 *          compatibility and ignored: the download always runs on the reserved
 *          WM_SDK_HTTPS_DOWNLOAD_INDEX session.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __WM_SDK_OTA_H__
#define __WM_SDK_OTA_H__

#include "wm_sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** Download
******************************************************************************/
/**
 * @brief  Initialise the OTA download client and clear any state left by a
 *         previous transfer. Delegates to wm_sdk_https_init(), so it shares the
 *         HTTPS client with the rest of the application.
 * @param  tx_mode  wm_SdkHttpsTxMode. The download path is synchronous whichever
 *                  mode is selected; this only decides where unrelated HTTPS
 *                  requests report their completion.
 * @param  msgq     completion queue for async mode, else NULL.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_BUSY if a request is in flight on
 *                     any HTTPS session.
 */
wm_SdkResult wm_sdk_ota_download_init(UINT32 tx_mode, void *msgq);

/**
 * @brief  Set the trust anchor for the OTA download server.
 *
 *         @p ca_cert is passed through to the HTTPS download session unchanged,
 *         including NULL - which is a valid, explicit choice meaning "negotiate
 *         TLS but do not authenticate the server". Nothing is read from
 *         credential storage on your behalf: to use the anchor provisioned on
 *         the unit, read it yourself with
 *         wm_sdk_storage_cred_read(WM_SDK_STORAGE_CRED_ROOT_CA, ...) and pass it here.
 *
 *         The PEM is NOT copied. It stays CALLER-OWNED and must remain valid,
 *         NUL-terminated, until wm_sdk_ota_download_terminate() - which includes the
 *         whole of wm_sdk_ota_download_action(). A buffer local to the calling
 *         function will dangle if the transfer runs on another task.
 *
 *         wm_sdk_ota_download_terminate() drops the reference, so a retry after it
 *         must call this again.
 *
 * @param  ssl_index  ignored; see the file header.
 * @param  ca_cert    root CA in PEM, or NULL for no server verification.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_BUSY if a transfer is in flight.
 */
wm_SdkResult wm_sdk_ota_download_configure_ssl(UINT32 ssl_index, const char *ca_cert);

/**
 * @brief  Set the OTA image URL and per-operation timeout. The URL is copied.
 *         Calling this discards any cached remote size and rewinds the sequential
 *         read cursor used by wm_sdk_ota_download_read_chunk().
 * @param  ssl_index  ignored; see the file header.
 * @param  url        image URL, at most WM_SDK_HTTPS_URL_MAX-1 characters.
 * @param  timeout    transport timeout in seconds; 0 selects the HTTPS default.
 *                    This bounds each chunk request, not the transfer as a whole.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL/empty or
 *                     oversize URL; WM_SDK_RESULT_BUSY if a transfer is in flight.
 */
wm_SdkResult wm_sdk_ota_download_set_params(UINT32 ssl_index, const char *url, UINT32 timeout);

/**
 * @brief  Download the whole image to the staging file for the current image
 *         type, then return. BLOCKS until the transfer finishes or fails.
 *
 *         Queries the remote size, deletes any stale staging file, then loops
 *         ranged GET -> file write until the byte count matches. A short or
 *         failed transfer leaves a partial file behind and reports failure; the
 *         staged file is only ever trustworthy once wm_sdk_ota_verify_image() passes.
 *
 *         The HTTPS session is left open so a failed transfer can be retried
 *         without re-supplying the URL and CA; call
 *         wm_sdk_ota_download_terminate() when done either way.
 *
 * @param  ssl_index  ignored; see the file header.
 * @return wm_SdkResult - 0 success (the complete image is staged);
 *                     WM_SDK_RESULT_INVALID_PARAM if no URL has been set;
 *                     WM_SDK_RESULT_BUSY if another task holds the OTA lock;
 *                     WM_SDK_RESULT_ERROR on a size query, transport or write
 *                     failure.
 */
wm_SdkResult wm_sdk_ota_download_action(UINT32 ssl_index);

/**
 * @brief  Query the size of the remote OTA image, in bytes. The result is cached
 *         for the sequential reader.
 * @param  ssl_index  ignored; see the file header.
 * @param  file_size  [out] image size in bytes.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_ERROR if the size could not be
 *                     determined, which also means the server will not honour
 *                     the ranged reads the download depends on.
 */
wm_SdkResult wm_sdk_ota_download_get_file_size(UINT32 ssl_index, UINT32 *file_size);

/**
 * @brief  Read the next chunk of the remote OTA image, for callers that would
 *         rather drive the transfer than hand it to wm_sdk_ota_download_action().
 *
 *         Reads are SEQUENTIAL: an internal cursor advances by the bytes
 *         returned, starting from 0 and rewound by wm_sdk_ota_download_set_params().
 *         Each call issues one self-contained ranged request, so a failed chunk
 *         can be retried by simply calling again.
 *
 *         End of image is success with @p bytes_read set to 0, not an error.
 *
 * @param  ssl_index   ignored; see the file header.
 * @param  buffer      [out] chunk buffer, at least @p size bytes.
 * @param  size        maximum bytes to read; the tail chunk returns fewer.
 * @param  bytes_read  [out] bytes placed in @p buffer; 0 at end of image.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL argument or
 *                     zero size; WM_SDK_RESULT_BUSY if another task holds the lock;
 *                     WM_SDK_RESULT_ERROR on a size query or transport failure.
 */
wm_SdkResult wm_sdk_ota_download_read_chunk(UINT32 ssl_index, void *buffer, UINT32 size, UINT32 *bytes_read);

/**
 * @brief  End the OTA download session: closes the staging file if it is still
 *         open, closes the transport and releases the session's buffers, and
 *         drops the trust-anchor reference set by
 *         wm_sdk_ota_download_configure_ssl(). The staged image file is left in
 *         place - terminating the download does not discard what it fetched.
 * @param  ssl_index  ignored; see the file header.
 * @return wm_SdkResult - 0 success.
 */
wm_SdkResult wm_sdk_ota_download_terminate(UINT32 ssl_index);

/*******************************************************************************
** App package (the staged image file)
******************************************************************************/
/**
 * @brief  Open the staging file for the current image type. Pair with
 *         wm_sdk_app_package_close(); only one package handle may be open at a time.
 * @param  mode  fopen-style mode string, e.g. "wb+" to stage, "rb" to read back.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL mode;
 *                     WM_SDK_RESULT_BUSY if a package is already open;
 *                     WM_SDK_RESULT_ERROR if the file could not be opened.
 */
wm_SdkResult wm_sdk_app_package_open(const char *mode);

/**
 * @brief  Read from the open app package at the current file position.
 * @param  data        [out] destination buffer.
 * @param  size        bytes to read.
 * @param  bytes_read  [out] bytes actually read; 0 at end of file.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_NOT_INITIALIZED if no package is
 *                     open; WM_SDK_RESULT_INVALID_PARAM on a NULL argument.
 */
wm_SdkResult wm_sdk_app_package_read(void *data, UINT32 size, UINT32 *bytes_read);

/**
 * @brief  Append firmware bytes to the open app package. A short write is
 *         reported as a failure rather than silently truncating the image.
 * @param  data  bytes to write.
 * @param  size  number of bytes; must be non-zero.
 * @return wm_SdkResult - 0 success (all @p size bytes written);
 *                     WM_SDK_RESULT_NOT_INITIALIZED if no package is open;
 *                     WM_SDK_RESULT_INVALID_PARAM on a NULL/zero argument;
 *                     WM_SDK_RESULT_ERROR on a short or failed write.
 */
wm_SdkResult wm_sdk_app_package_write(const void *data, UINT32 size);

/**
 * @brief  Close the app package, committing buffered data to flash.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_NOT_INITIALIZED if none was open.
 */
wm_SdkResult wm_sdk_app_package_close(void);

/*******************************************************************************
** Image selection, staging and verification
******************************************************************************/
/**
 * @brief  Select which image the download and app-package calls operate on.
 *         Defaults to WM_SDK_OTA_IMAGE_APP. Changing it discards the cached remote
 *         size and rewinds the sequential read cursor.
 * @param  image_type  an wm_SdkOtaImageType value.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on an unknown type;
 *                     WM_SDK_RESULT_BUSY while a package handle is open.
 */
wm_SdkResult wm_sdk_ota_set_image_type(UINT32 image_type);

/**
 * @brief  Get the staging-file path for an image type. This is the same storage
 *         the apply step reads, so it is the authoritative destination.
 * @param  image_type  an wm_SdkOtaImageType value; an unknown value maps to
 *                     WM_SDK_OTA_IMAGE_APP.
 * @return path string, never NULL.
 */
const char *wm_sdk_ota_get_image_path(UINT32 image_type);

/**
 * @brief  Verify a staged image against an expected SHA-256 digest. This is the
 *         only integrity check available on this platform, so a firmware update
 *         should treat a failure here as fatal and not apply the image.
 * @param  image_type  an wm_SdkOtaImageType value.
 * @param  sha256_hex  expected digest, exactly WM_SDK_OTA_SHA256_HEX_LEN hex chars.
 * @return wm_SdkResult - 0 the staged image matches;
 *                     WM_SDK_RESULT_INVALID_PARAM if @p sha256_hex is NULL or not
 *                     the right length;
 *                     WM_SDK_RESULT_ERROR if no image is staged or the digest
 *                     differs.
 */
wm_SdkResult wm_sdk_ota_verify_image(UINT32 image_type, const char *sha256_hex);

/**
 * @brief  Compute the SHA-256 of a staged image and return it as lowercase hex.
 *         Useful when a verification failure needs diagnosing - printing the
 *         digest that was actually computed says far more than "mismatch".
 * @param  image_type  an wm_SdkOtaImageType value.
 * @param  hash        [out] buffer for the digest, NUL-terminated.
 * @param  hash_size   size of @p hash; must be at least
 *                     WM_SDK_OTA_SHA256_HEX_LEN + 1.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL or
 *                     undersized buffer; WM_SDK_RESULT_ERROR if no image is staged
 *                     or it could not be hashed.
 */
wm_SdkResult wm_sdk_ota_get_image_hash(UINT32 image_type, char *hash, UINT32 hash_size);

/*******************************************************************************
** Apply / flash
******************************************************************************/
/**
 * @brief  Arm the bootloader to install the staged application image on the next
 *         boot. Does not reboot: call wm_sdk_ota_app_update_restart() to do that.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_ERROR if no application image is
 *                     staged or the bootloader could not be armed.
 */
wm_SdkResult wm_sdk_ota_app_update(void);

/**
 * @brief  Reboot into the application update armed by wm_sdk_ota_app_update().
 *         Does not return.
 */
void wm_sdk_ota_app_update_restart(void);

/*******************************************************************************
** MINI FOTA - the DFOTA (kernel patch) path on this platform. The module
** fetches and applies the package itself over HTTP; nothing is staged or
** verified locally. It reboots mid-upgrade, so the outcome arrives on the
** callback - typically on a LATER boot than the _start() that triggered it.
******************************************************************************/
/**
 * @brief  Upgrade outcome. 'status' is 0 on success, else a module error code
 *         (raw, not an wm_SdkResult). Runs in kernel callback context: keep it
 *         short, do not start another upgrade from inside it.
 */
typedef void (*wm_SdkOtaMiniDfotaCb)(int status);

/**
 * @brief  Register the MINI FOTA result callback ('cb' may be NULL to only
 *         log the result). Call once during start-up; idempotent.
 * @return wm_SdkResult - 0 success.
 */
wm_SdkResult wm_sdk_ota_mini_dfota_init(wm_SdkOtaMiniDfotaCb cb);

/**
 * @brief  Start a MINI FOTA upgrade of the package at 'url'. Returns once the
 *         module has ACCEPTED the request - "started", not "upgraded".
 * @param  url  package URL, at most WM_MINI_FOTA_URL_MAX-1 characters.
 * @return 0 on acceptance; else a negative module error code, NOT an wm_SdkResult.
 */
int wm_sdk_ota_mini_dfota_start(const char *url);

/*******************************************************************************
** Versions
******************************************************************************/
/**
 * @brief  Read the customer application version string, e.g. "WMZ_DEV_1.0.2".
 * @param  version       [out] buffer for the version string.
 * @param  version_size  size of the buffer in bytes (WM_SDK_OTA_VERSION_MAX
 *                       guarantees it fits).
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL/zero-size
 *                     buffer; WM_SDK_RESULT_ERROR if no version is set.
 */
wm_SdkResult wm_sdk_ota_get_app_version(char *version, UINT32 version_size);

/**
 * @brief  Read the platform SDK version string, e.g. "WM_SDK_C_260721".
 *
 *         Normally populated during system start-up. If it is not yet available
 *         this issues the underlying version query, which briefly blocks on the
 *         AT channel; once populated it is a plain buffer copy.
 *
 * @param  version       [out] buffer for the version string.
 * @param  version_size  size of the buffer in bytes (WM_SDK_OTA_VERSION_MAX
 *                       guarantees it fits).
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL/zero-size
 *                     buffer; WM_SDK_RESULT_ERROR if the version is unavailable.
 */
wm_SdkResult wm_sdk_ota_get_sdk_version(char *version, UINT32 version_size);

/*******************************************************************************
** Not supported on this platform
******************************************************************************/
/**
 * @brief  Disable the fallback/backup-firmware feature.
 * @return WM_SDK_RESULT_NOT_SUPPORTED - the vendor layer exposes no fallback
 *         firmware control on this module.
 */
wm_SdkResult wm_sdk_ota_fbf_disable(void);

#ifdef __cplusplus
}
#endif

#endif /* __WM_SDK_OTA_H__ */
