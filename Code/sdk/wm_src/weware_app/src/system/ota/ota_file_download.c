/**
 * @file ota_file_download.c
 * @brief OTA HTTP + firmware download for all device classes.
 *
 * Holds the unified version-check (one request format for every device class —
 * the unused identifier is sent as JSON null — and one key-matched
 * fileMetaDataList parser) plus the firmware download/write engine. Consolidates
 * logic that previously lived in ota_manager.c and peri_ota_manager.c.
 * See docs/ota_unified_check_refactor.md.
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>

#include "sdk_platform.h"
#include "functionality/sdk_functionality_os.h"
#include "functionality/sdk_functionality_file.h"
#include "functionality/sdk_functionality_ota.h"
#include "module/https/https_ops.h"
#include "common/utils.h"
#include "system/storage/file_system.h"
#include "system/storage/flash_paths.h"
#include "system/ota/ota_manager.h"   /* OTA_CHECK_VERSION_URL, OTA_UPGRADE_* */
#include "system/ota/ota_file_download.h"

#define LOG_TAG "OTA"
#define LOG_MODULE_LEVEL LOG_LEVEL_DEBUG
#include "module/log/log.h"

#define OTA_HTTP_MAX_META_ITEMS  10
#define OTA_ARTIFACT_TYPE_BIN    "BIN"

/*
 * OTA_DOWNLOAD_TEST: when defined, the SIMCOM image is downloaded to a plain
 * flash file (OTA_DOWNLOAD_TEST_FILE_PATH) instead of being streamed into the
 * SIMCOM app package (sdk_app_package_*). Lets the download path be validated
 * end-to-end without touching the update partition. Undefine for production.
 */
#define OTA_DOWNLOAD_TEST
#define OTA_DOWNLOAD_TEST_FILE_PATH  FLASH_DIR_FOTA "simcom_test.bin"

/*---------------------------------------------------------------
 * Response helpers
 *--------------------------------------------------------------*/

/** @return 1 if the response carries success:true/1, else 0. */
static int ota_http_is_success(const char *response)
{
    if (!response) return 0;
    char success_value[16] = {0};
    if (utils_extract_json_string(response, "success", success_value, sizeof(success_value)) != 0)
        return 0;
    return (strcmp(success_value, "true") == 0 || strcmp(success_value, "1") == 0);
}

/** @return pointer to the '{' of the "data" object, or NULL. */
static const char *ota_http_find_data(const char *response)
{
    if (!response) return NULL;
    const char *data_start = strstr(response, "\"data\"");
    if (!data_start) return NULL;
    data_start = strchr(data_start, '{');
    if (!data_start) SDK_DEBUG_PRINT("OTA: data object start not found");
    return data_start;
}

/**
 * @brief Parse a "checkSum" string into a CRC32.
 * Hex first (handles optional 0x/0X prefix and plain hex), decimal fallback.
 */
static int ota_http_extract_checksum(const char *item, UINT32 *crc32)
{
    if (!item || !crc32) return 0;
    char crc_str[32] = {0};
    if (utils_extract_json_string(item, "checkSum", crc_str, sizeof(crc_str)) != 0)
        return 0;

    char *end = NULL;
    unsigned long v = strtoul(crc_str, &end, 16);
    if (end == crc_str) {
        v = strtoul(crc_str, &end, 10);
        if (end == crc_str) return 0;
    }
    *crc32 = (UINT32)v;
    return 1;
}

/**
 * @brief Walk data.fileMetaDataList[] and return the item whose "key" matches
 *        @p artifact_key. Bounded by @p data_end so a truncated response can't
 *        over-read. Items are null-terminated in place inside the (mutable)
 *        response buffer for field extraction, then restored.
 * @return 1 if found (url_out/crc_out filled), 0 otherwise.
 */
static int ota_http_find_artifact(char *data_start, char *data_end,
                                  const char *artifact_key,
                                  char *url_out, size_t url_size, UINT32 *crc32)
{
    if (!data_start || !artifact_key || !url_out || url_size == 0) return 0;

    /* Locate the fileMetaDataList array */
    const char *array_pos = strstr(data_start, "\"fileMetaDataList\"");
    if (!array_pos) return 0;
    array_pos = strchr(array_pos, '[');
    if (!array_pos) return 0;
    array_pos++; /* skip '[' */

    /* Bound the array */
    char *array_end = NULL;
    for (char *p = (char *)array_pos; p < data_end; p++) {
        if (*p == ']') { array_end = p; break; }
    }
    if (!array_end) {
        SDK_DEBUG_PRINT("OTA: fileMetaDataList incomplete (response truncated?)");
        return 0;
    }

    char *cur = (char *)array_pos;
    for (int i = 0; i < OTA_HTTP_MAX_META_ITEMS; i++) {
        while (cur < array_end && (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n' || *cur == ','))
            cur++;
        if (cur >= array_end || *cur != '{') break;

        /* Match braces to find this item's end (within array bounds) */
        char *item_start = cur;
        char *item_end = NULL;
        int brace = 0;
        for (char *p = item_start; p < array_end; p++) {
            if (*p == '{') brace++;
            else if (*p == '}') { brace--; if (brace == 0) { item_end = p + 1; break; } }
        }
        if (!item_end || item_end > array_end) break;

        /* Temporarily null-terminate the item for field extraction, then restore */
        char saved = *item_end;
        *item_end = '\0';

        char extracted_key[16] = {0};
        int matched = 0;
        if (utils_extract_json_string(item_start, "key", extracted_key, sizeof(extracted_key)) == 0 &&
            strcmp(extracted_key, artifact_key) == 0) {
            if (utils_extract_json_string(item_start, "url", url_out, url_size) == 0 &&
                strlen(url_out) > 0) {
                if (crc32 && !ota_http_extract_checksum(item_start, crc32))
                    *crc32 = 0;
                matched = 1;
            }
        }

        *item_end = saved;
        if (matched) return 1;

        cur = item_end;
    }

    return 0;
}

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/

int ota_http_check_version(const char *fw, const char *hw,
                           const char *imei, const char *mac,
                           const char *artifact_key,
                           char *resp_buf, size_t resp_buf_size,
                           char *url_out, size_t url_size,
                           UINT32 *crc_out)
{
    if (!fw || !hw || !artifact_key || !resp_buf || resp_buf_size == 0 ||
        !url_out || url_size == 0)
        return -1;

    url_out[0] = '\0';
    if (crc_out) *crc_out = 0;

    /* Unified request: the real identifier is a quoted string, the unused one is
     * the JSON literal null (modem: macId=null, peripheral: imei=null). */
    char imei_field[24];
    char mac_field[20];
    if (imei && imei[0]) snprintf(imei_field, sizeof(imei_field), "\"%s\"", imei);
    else                 utils_strncpy_safe(imei_field, "null", sizeof(imei_field));
    if (mac && mac[0])   snprintf(mac_field, sizeof(mac_field), "\"%s\"", mac);
    else                 utils_strncpy_safe(mac_field, "null", sizeof(mac_field));

    char payload[256] = {0};
    int n = snprintf(payload, sizeof(payload),
                     "{\"currentFirmwareVersion\":\"%s\",\"hardwareVersion\":\"%s\","
                     "\"artifactType\":\"%s\",\"imei\":%s,\"macId\":%s}",
                     fw, hw, OTA_ARTIFACT_TYPE_BIN,
                     imei_field, mac_field);
    if (n < 0 || (size_t)n >= sizeof(payload)) {
        sdk_log_error("OTA: version-check payload too large");
        return -1;
    }

    sdk_debug_print("OTA: version-check POST key=%s", artifact_key);

    size_t resp_len = 0;
    int ok = sdk_https_request(SDK_HTTPS_METHOD_POST, OTA_CHECK_VERSION_URL_STAGE,
                               payload, "application/json",
                               resp_buf, resp_buf_size, &resp_len);
    if (!ok || resp_len == 0) {
        sdk_debug_print("OTA: version-check request failed");
        return -1;
    }

    /* Null-terminate (and note any truncation) */
    size_t end = (resp_len < resp_buf_size) ? resp_len : (resp_buf_size - 1);
    resp_buf[end] = '\0';
    if (resp_len >= resp_buf_size) {
        SDK_DEBUG_PRINT("OTA: version-check response truncated (%zu >= %zu)", resp_len, resp_buf_size);
        resp_len = end;
    }

    if (!ota_http_is_success(resp_buf)) {
        sdk_debug_print("OTA: version-check success=false");
        return -1;
    }

    char *data_start = (char *)ota_http_find_data(resp_buf);
    if (!data_start) {
        sdk_debug_print("OTA: no data object — no update");
        return 0;   /* no update available */
    }

    char *buf_end  = resp_buf + resp_buf_size;
    char *data_end = (resp_buf + resp_len < buf_end) ? (resp_buf + resp_len) : buf_end;

    if (!ota_http_find_artifact(data_start, data_end, artifact_key, url_out, url_size, crc_out)) {
        sdk_debug_print("OTA: no artifact for key %s", artifact_key);
        return 0;   /* no matching artifact — no update */
    }

    sdk_debug_print("OTA: artifact URL found for key %s", artifact_key);
    return 1;
}

/*---------------------------------------------------------------
 * Firmware download / write engine
 *--------------------------------------------------------------*/

#define OTA_DL_CHUNK_SIZE               1024
#define OTA_FLASH_DOWNLOAD_MARGIN_BYTES (64u * 1024u)

BOOL ota_flash_has_space_for_download(UINT32 file_size_bytes, const char *replace_path)
{
    if (file_size_bytes == 0) {
        sdk_log_error("OTA: invalid download size 0");
        return FALSE;
    }

    if (replace_path && replace_path[0] != '\0') {
        char probe[1] = {0};
        UINT32 probe_len = 0;
        if (file_system_read_file(replace_path, probe, sizeof(probe), &probe_len) == RESULT_SUCCESS)
            (void)file_system_delete(replace_path);
    }

    INT64 total = 0;
    INT64 freeb = 0;
    INT64 used = 0;
    if (file_system_get_disk_info(FLASH_ROOT, &total, &freeb, &used) != RESULT_SUCCESS) {
        sdk_log_error("OTA: flash disk info unavailable");
        return FALSE;
    }

    UINT64 need = (UINT64)file_size_bytes + (UINT64)OTA_FLASH_DOWNLOAD_MARGIN_BYTES;
    if (freeb < 0 || (UINT64)freeb < need) {
        sdk_log_error("OTA: insufficient flash (need %llu B incl. margin, free %lld B)",
                  (unsigned long long)need, (long long)freeb);
        return FALSE;
    }

    sdk_log_info("OTA: flash OK for download (%u B, free %lld B)",
             file_size_bytes, (long long)freeb);
    return TRUE;
}

static int ota_download_init_https(void **msgq)
{
    *msgq = sdk_msgq_create("ota_download_msgq", sizeof(sdk_msg_t), 8, 0);
    if (!*msgq)
    {
        SDK_DEBUG_PRINT("Failed to create HTTPS URC message queue");
        return 0;
    }

    sdk_https_returncode_t https_error = sdk_https_download_init(SDK_HTTPS_DATA_TX_USB20, *msgq);
    if (https_error != SDK_HTTPS_SUCCESS)
    {
        SDK_DEBUG_PRINT("HTTPS service with USB2.0 failed, trying serial port...");
        https_error = sdk_https_download_init(SDK_HTTPS_DATA_TX_SERIAL, *msgq);
        if (https_error != SDK_HTTPS_SUCCESS)
        {
            SDK_DEBUG_PRINT("HTTPS service initialization failed");
            sdk_msgq_delete(*msgq);
            *msgq = NULL;
            return 0;
        }
    }

    if (sdk_https_download_configure_ssl() != SDK_SUCCESS)
    {
        SDK_DEBUG_PRINT("Failed to configure SSL");
        sdk_https_download_terminate();
        sdk_msgq_delete(*msgq);
        *msgq = NULL;
        return 0;
    }

    return 1;
}

static void ota_download_cleanup_https(void *msgq)
{
    sdk_https_download_terminate();
    if (msgq)
    {
        sdk_msgq_delete(msgq);
    }
}

static int ota_download_read_chunk(UINT32 offset, UINT32 chunk_size, char *buffer, size_t buffer_size, UINT32 *bytes_read)
{
    if (!buffer || !bytes_read || buffer_size == 0)
    {
        return 0;
    }

    if (chunk_size > buffer_size)
    {
        SDK_DEBUG_PRINT("Chunk size (%u) exceeds buffer size (%zu), limiting to buffer size", chunk_size, buffer_size);
        chunk_size = (UINT32)buffer_size;
    }

    if (!sdk_https_download_read_chunk(offset, chunk_size, buffer, bytes_read))
    {
        SDK_DEBUG_PRINT("HTTPS read chunk failed at offset %u", offset);
        return 0;
    }

    if (*bytes_read == 0)
    {
        SDK_DEBUG_PRINT("No data read at offset %u", offset);
        return 0;
    }

    if (*bytes_read > buffer_size)
    {
        SDK_DEBUG_PRINT("Bytes read (%u) exceeds buffer size (%zu), truncating", *bytes_read, buffer_size);
        *bytes_read = (UINT32)buffer_size;
    }

    return 1;
}

/*
 * Shared chunked-download engine. The sink (app-package partition vs flash file)
 * is supplied as a callback; the caller provides the scratch buffer.
 */
typedef int (*ota_chunk_sink_fn)(void *sink, const char *data, UINT32 len);

static int ota_download_stream(UINT32 file_size, const char *label,
                               char *scratch, size_t scratch_size,
                               ota_chunk_sink_fn sink_write, void *sink)
{
    UINT32 download_offset = 0;
    UINT32 last_logged_pct = 0;

    while (download_offset < file_size)
    {
        UINT32 remaining  = file_size - download_offset;
        UINT32 chunk_size = (remaining > OTA_DL_CHUNK_SIZE) ? OTA_DL_CHUNK_SIZE : remaining;
        UINT32 bytes_read = 0;

        if (!ota_download_read_chunk(download_offset, chunk_size,
                                     scratch, scratch_size, &bytes_read))
        {
            sdk_log_error("OTA: %s firmware download failed at offset %u", label, download_offset);
            return 0;
        }

        if (!sink_write(sink, scratch, bytes_read))
        {
            sdk_log_error("OTA: Failed to write %s firmware chunk at offset %u", label, download_offset);
            return 0;
        }

        download_offset += bytes_read;

        UINT32 progress_pct = (download_offset * 100) / file_size;
        if (progress_pct >= last_logged_pct + 10 || download_offset >= file_size)
        {
            sdk_log_info("OTA: Downloading %s firmware: %u%% (%u/%u bytes)",
                     label, progress_pct, download_offset, file_size);
            last_logged_pct = progress_pct;
        }
    }

    return 1;
}

static int ota_sink_app_package(void *sink, const char *data, UINT32 len)
{
    (void)sink;
    return (sdk_app_package_write(data, len) == SDK_RESULT_SUCCESS) ? 1 : 0;
}

static int ota_sink_file(void *sink, const char *data, UINT32 len)
{
    UINT32 written = 0;
    return (sdk_file_write(sink, data, len, &written) == SDK_RESULT_SUCCESS && written == len) ? 1 : 0;
}

static int ota_download_write_simcom(UINT32 file_size, char *scratch, size_t scratch_size)
{
    sdk_debug_print("OTA: download_write_simcom entry size=%u", file_size);
#ifdef OTA_DOWNLOAD_TEST
    /* Test mode: download to a flash file only, do not touch the app package. */
    sdk_log_warning("OTA: OTA_DOWNLOAD_TEST active - writing SIMCOM image to %s (no package write)",
                 OTA_DOWNLOAD_TEST_FILE_PATH);

    if (!ota_flash_has_space_for_download(file_size, OTA_DOWNLOAD_TEST_FILE_PATH))
    {
        return 0;
    }

    void *test_file = sdk_file_open(OTA_DOWNLOAD_TEST_FILE_PATH, "wb");
    if (test_file == NULL)
    {
        sdk_log_error("OTA: failed to open %s for write", OTA_DOWNLOAD_TEST_FILE_PATH);
        return 0;
    }

    sdk_log_info("OTA: Starting SIMCOM test download (%u bytes)", file_size);

    if (!ota_download_stream(file_size, "SIMCOM(test)", scratch, scratch_size, ota_sink_file, test_file))
    {
        sdk_file_close(test_file);
        file_system_delete(OTA_DOWNLOAD_TEST_FILE_PATH);
        return 0;
    }

    if (sdk_file_close(test_file) != SDK_RESULT_SUCCESS)
    {
        sdk_log_error("OTA: Failed to close SIMCOM test file");
        file_system_delete(OTA_DOWNLOAD_TEST_FILE_PATH);
        return 0;
    }

    sdk_log_info("OTA: SIMCOM test download complete (%u bytes) -> %s",
                 file_size, OTA_DOWNLOAD_TEST_FILE_PATH);
    return 1;
#else
    SdkResult open_ret = sdk_app_package_open("w");
    if (open_ret != SDK_RESULT_SUCCESS)
    {
        sdk_debug_print("OTA: failed to open app package for writing (result=%d)", (int)open_ret);
        sdk_log_error("OTA: sdk_app_package_open failed - check SIMCOM app update API in sdk_simcom_ota.c");
        return 0;
    }

    sdk_log_info("OTA: Starting SIMCOM firmware download (%u bytes)", file_size);

    if (!ota_download_stream(file_size, "SIMCOM", scratch, scratch_size, ota_sink_app_package, NULL))
    {
        sdk_log_error("OTA: SIMCOM firmware download/write failed");
        sdk_app_package_close();
        return 0;
    }

    if (sdk_app_package_close() != SDK_RESULT_SUCCESS)
    {
        sdk_log_error("OTA: Failed to close SIMCOM firmware package");
        return 0;
    }

    sdk_log_info("OTA: SIMCOM firmware download complete (%u bytes)", file_size);
    return 1;
#endif /* OTA_DOWNLOAD_TEST */
}

static int ota_download_write_st(UINT32 file_size, char *scratch, size_t scratch_size, UINT32 *out_size)
{
    sdk_debug_print("OTA: download_write_st entry size=%u", file_size);
    char check_buffer[1] = {0};
    UINT32 read_len = 0;
    if (file_system_read_file(ST_FIRMWARE_FILE_PATH, check_buffer, sizeof(check_buffer), &read_len) == RESULT_SUCCESS)
    {
        if (file_system_delete(ST_FIRMWARE_FILE_PATH) != RESULT_SUCCESS)
        {
            sdk_debug_print("OTA: failed to delete existing ST file");
            return 0;
        }
    }

    void* st_file = sdk_file_open(ST_FIRMWARE_FILE_PATH, "wb");
    if (st_file == NULL)
    {
        sdk_debug_print("OTA: failed to open ST file for write");
        return 0;
    }

    sdk_log_info("OTA: Starting ST firmware download (%u bytes)", file_size);

    if (!ota_download_stream(file_size, "ST", scratch, scratch_size, ota_sink_file, st_file))
    {
        sdk_file_close(st_file);
        file_system_delete(ST_FIRMWARE_FILE_PATH);
        return 0;
    }

    if (sdk_file_close(st_file) != SDK_RESULT_SUCCESS)
    {
        sdk_log_error("OTA: Failed to close ST firmware file");
        file_system_delete(ST_FIRMWARE_FILE_PATH);
        return 0;
    }

    sdk_log_info("OTA: ST firmware download complete (%u bytes)", file_size);

    if (out_size) *out_size = file_size;
    return 1;
}

int ota_download_and_write_firmware(int upgrade_type, const char *url,
                                    char *scratch, size_t scratch_size, UINT32 *out_size)
{
    if (out_size) *out_size = 0;

    if (!url || strlen(url) == 0)
    {
        SDK_DEBUG_PRINT("ota_download_and_write_firmware: No URL");
        return 0;
    }
    if (!scratch || scratch_size == 0)
    {
        return 0;
    }
    if (upgrade_type != OTA_UPGRADE_SIMCOM && upgrade_type != OTA_UPGRADE_ST)
    {
        SDK_DEBUG_PRINT("ota_download_and_write_firmware: Invalid upgrade type: %d", upgrade_type);
        return 0;
    }

    /* Disable FBF before download (required by SIMCOM API) */
    sdk_ota_fbf_disable();

    void *msgq = NULL;
    if (!ota_download_init_https(&msgq))
    {
        return 0;
    }

    sdk_log_info("OTA: download URL: %s", url);
    if (sdk_https_download_set_params(url) != SDK_HTTPS_SUCCESS)
    {
        SDK_DEBUG_PRINT("Failed to set download parameters");
        ota_download_cleanup_https(msgq);
        return 0;
    }

    UINT32 file_size = 0;
    if (!sdk_https_download_get_file_size(&file_size) || file_size == 0)
    {
        sdk_log_error("OTA: GET request returned zero/invalid file size");
        ota_download_cleanup_https(msgq);
        return 0;
    }

    sdk_log_info("OTA: Firmware file size: %u bytes, type: %s",
             file_size, (upgrade_type == OTA_UPGRADE_SIMCOM) ? "SIMCOM" : "ST");

    if (upgrade_type == OTA_UPGRADE_ST)
    {
        if (!ota_flash_has_space_for_download(file_size, ST_FIRMWARE_FILE_PATH))
        {
            ota_download_cleanup_https(msgq);
            return 0;
        }
    }

    sdk_debug_print("OTA: starting download write type=%d size=%u", upgrade_type, file_size);
    int result = (upgrade_type == OTA_UPGRADE_ST)
                 ? ota_download_write_st(file_size, scratch, scratch_size, out_size)
                 : ota_download_write_simcom(file_size, scratch, scratch_size);

    ota_download_cleanup_https(msgq);

    if (result)
        sdk_log_info("OTA: Firmware download and write completed successfully");
    else
        sdk_log_error("OTA: Firmware download and write failed");

    return result;
}

int ota_download_peripheral_file(const char *url, const char *dest_path,
                                 char *scratch, size_t scratch_size)
{
    if (!url || strlen(url) == 0 || !dest_path || !scratch || scratch_size == 0)
        return 0;

    void *msgq = NULL;
    if (!ota_download_init_https(&msgq))
        return 0;

    sdk_ota_fbf_disable();

    if (sdk_https_download_set_params(url) != SDK_HTTPS_SUCCESS) {
        sdk_log_error("OTA: peripheral set_params failed");
        ota_download_cleanup_https(msgq);
        return 0;
    }

    UINT32 file_size = 0;
    if (!sdk_https_download_get_file_size(&file_size) || file_size == 0) {
        sdk_log_error("OTA: peripheral zero file size");
        ota_download_cleanup_https(msgq);
        return 0;
    }

    if (!ota_flash_has_space_for_download(file_size, dest_path)) {
        sdk_log_error("OTA: insufficient flash for %u byte peripheral image", file_size);
        ota_download_cleanup_https(msgq);
        return 0;
    }

    void *fp = sdk_file_open(dest_path, "wb");
    if (!fp) {
        sdk_log_error("OTA: cannot open %s for write", dest_path);
        ota_download_cleanup_https(msgq);
        return 0;
    }

    sdk_log_info("OTA: downloading peripheral firmware (%u bytes)", file_size);
    int ok = ota_download_stream(file_size, "peripheral", scratch, scratch_size, ota_sink_file, fp);

    sdk_file_close(fp);
    ota_download_cleanup_https(msgq);

    if (!ok) {
        file_system_delete(dest_path);
        return 0;
    }
    sdk_log_info("OTA: peripheral download complete (%u bytes)", file_size);
    return 1;
}
