/**
 * @file ota_file_download.h
 * @brief OTA HTTP + download for all device classes: the unified version-check
 *        request/response, plus the firmware download/write engine. Shared by
 *        the modem (SIMCOM/ST) and peripheral (nRF/ESP32/fuel) OTA flows.
 *
 * See docs/ota_unified_check_refactor.md for the change record and revert notes.
 */

#ifndef OTA_FILE_DOWNLOAD_H
#define OTA_FILE_DOWNLOAD_H

#include "common/types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run the version-check API for any device class and return the matching
 *        artifact's download URL (and CRC).
 *
 * One request shape is used for every device class:
 * @code
 *   {"currentFirmwareVersion":"<fw>","hardwareVersion":"<hw>",
 *    "artifactType":"BIN","imei":<imei|null>,"macId":<mac|null>}
 * @endcode
 * Exactly one identifier is real; pass the other as NULL (sent as JSON null):
 *   - modem (SIMCOM/ST): imei = real,  mac = NULL  -> "macId":null
 *   - peripheral:        imei = NULL,  mac = real  -> "imei":null
 *
 * The response's @c data.fileMetaDataList[] item whose @c "key" equals
 * @p artifact_key supplies @c "url" (+ @c "checkSum").
 *
 * @param fw            Pre-formatted firmware version (sent verbatim).
 * @param hw            Pre-formatted hardware version (sent verbatim).
 * @param imei          Device IMEI, or NULL for peripheral (-> JSON null).
 * @param mac           Peripheral MAC (uppercase), or NULL for modem (-> JSON null).
 * @param artifact_key  Which artifact to select: "cgsc" / "cgst" / "app".
 * @param resp_buf      Caller-owned scratch buffer for the HTTP response
 *                      (reused/over-written in place during parsing).
 * @param resp_buf_size Size of @p resp_buf.
 * @param url_out       Output buffer for the matched download URL.
 * @param url_size      Size of @p url_out.
 * @param crc_out       Optional: receives the parsed "checkSum" (NULL to skip).
 * @return  1  matching artifact found (url_out / crc_out filled),
 *          0  request OK but no matching artifact (no update),
 *         -1  request failed or response not successful.
 */
int ota_http_check_version(const char *fw, const char *hw,
                           const char *imei, const char *mac,
                           const char *artifact_key,
                           char *resp_buf, size_t resp_buf_size,
                           char *url_out, size_t url_size,
                           UINT32 *crc_out);

/**
 * @brief Verify flash has room for a download to @c FLASH_DIR_FOTA.
 * @param file_size_bytes Image size from the server (bytes).
 * @param replace_path    Existing staging file to delete first (may be NULL).
 * @return TRUE if free space is sufficient.
 */
BOOL ota_flash_has_space_for_download(UINT32 file_size_bytes, const char *replace_path);

/**
 * @brief Download a firmware image from @p url and write it to its destination
 *        (SIMCOM app-update partition, or the ST staging file).
 * @param upgrade_type  OTA_UPGRADE_SIMCOM or OTA_UPGRADE_ST.
 * @param url           Download URL (from the version check).
 * @param scratch       Caller-owned chunk scratch buffer.
 * @param scratch_size  Size of @p scratch.
 * @param out_size      Optional: receives the written byte count (NULL to skip).
 * @return 1 on success, 0 on failure.
 */
int ota_download_and_write_firmware(int upgrade_type, const char *url,
                                    char *scratch, size_t scratch_size,
                                    UINT32 *out_size);

/**
 * @brief Download a peripheral firmware image from @p url into a flash file.
 * @param url          Download URL.
 * @param dest_path    Destination flash path (e.g. PERI_FIRMWARE_FILE_PATH).
 * @param scratch      Caller-owned chunk scratch buffer.
 * @param scratch_size Size of @p scratch.
 * @return 1 on success, 0 on failure (partial file is deleted).
 */
int ota_download_peripheral_file(const char *url, const char *dest_path,
                                 char *scratch, size_t scratch_size);

#ifdef __cplusplus
}
#endif

#endif /* OTA_FILE_DOWNLOAD_H */
