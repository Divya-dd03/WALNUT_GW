/**
  ******************************************************************************
  * @file    device_utils.c
  * @author  WheelsEye
  * @brief   Device utility module for the weware application.
  *          Reads and caches the device IMEI via sdk_device_get_imei(),
  *          caches the ST co-processor firmware version and tracks
  *          peripheral device info (ready state + fw/hw versions).
  ******************************************************************************
  */

// sdk
#include "sdk_os.h"
#include "sdk_device.h"
#include "sdk_log.h"

// app
#include "device_utils.h"

#include <string.h>

/*---------------------------------------------------------------
 * Configuration
 *--------------------------------------------------------------*/
#define IMEI_RETRY_DELAY_MS     (200U)
#define IMEI_MAX_RETRIES        (20U)

/*---------------------------------------------------------------
 * Static State
 *--------------------------------------------------------------*/
static char g_global_imei[DEVICE_UTILS_IMEI_BUFFER_SIZE] = {0};
static bool g_imei_initialized                           = false;

static char g_st_firmware_version[DEVICE_UTILS_ST_FW_VERSION_BUFFER_SIZE] = {0};
static bool g_st_fw_version_initialized                                   = false;

/*---------------------------------------------------------------
 * Internal Helpers
 *--------------------------------------------------------------*/

/* strncpy that always NUL-terminates dst (dst_size includes the NUL) */
static void du_strncpy_safe(char *dst, const char *src, size_t dst_size)
{
    if (!dst || dst_size == 0)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static void imei_reset(void)
{
    memset(g_global_imei, 0, sizeof(g_global_imei));
    g_imei_initialized = false;
}

static void imei_store(const char *imei)
{
    du_strncpy_safe(g_global_imei, imei, sizeof(g_global_imei));
    g_imei_initialized = true;
}

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/

SdkResult device_utils_init(void)
{
    if (g_imei_initialized) {
        sdk_log_warning("Device utils already initialized: %s\r\n", g_global_imei);
        return SDK_RESULT_SUCCESS;
    }

    sdk_log_info("Initializing device utilities (IMEI)...\r\n");
    sdk_task_sleep(IMEI_RETRY_DELAY_MS);

    char imei_string[DEVICE_UTILS_IMEI_BUFFER_SIZE] = {0};

    for (UINT32 attempt = 0; attempt < IMEI_MAX_RETRIES; attempt++) {
        SdkResult result = sdk_device_get_imei(imei_string, sizeof(imei_string));

        if (result == SDK_RESULT_NOT_SUPPORTED) {
            sdk_log_error("IMEI retrieval not supported\r\n");
            imei_reset();
            return SDK_RESULT_NOT_SUPPORTED;
        }

        if (result == SDK_RESULT_SUCCESS &&
            strlen(imei_string) >= DEVICE_UTILS_IMEI_LENGTH) {
            // imei_store(imei_string);
            imei_store("860056081830565");
            sdk_log_info("IMEI initialized: %s (attempt %u)\r\n",
                         g_global_imei, (unsigned)(attempt + 1));
            return SDK_RESULT_SUCCESS;
        }

        sdk_log_warning("IMEI attempt %u failed (result=%d, len=%u, value='%s')\r\n",
                        (unsigned)(attempt + 1), (int)result,
                        (unsigned)strlen(imei_string), imei_string);

        if (attempt < IMEI_MAX_RETRIES - 1) {
            sdk_task_sleep(IMEI_RETRY_DELAY_MS);
            memset(imei_string, 0, sizeof(imei_string));
        }
    }

    sdk_log_error("Failed to initialize IMEI after %u attempts\r\n",
                  (unsigned)IMEI_MAX_RETRIES);
    return SDK_RESULT_ERROR;
}

int device_utils_get_imei(char *imei_buffer)
{
    if (!imei_buffer || !g_imei_initialized)
        return 0;

    strncpy(imei_buffer, g_global_imei, DEVICE_UTILS_IMEI_LENGTH);
    imei_buffer[DEVICE_UTILS_IMEI_LENGTH] = '\0';
    return 1;
}

SdkResult device_utils_set_imei(const char *imei)
{
    if (!imei || strlen(imei) < DEVICE_UTILS_IMEI_LENGTH ||
        strlen(imei) >= DEVICE_UTILS_IMEI_BUFFER_SIZE) {
        sdk_log_error("Invalid IMEI string provided\r\n");
        return SDK_RESULT_INVALID_PARAM;
    }

    imei_store(imei);
    sdk_log_info("IMEI manually set to: %s\r\n", g_global_imei);
    return SDK_RESULT_SUCCESS;
}

int device_utils_is_imei_initialized(void)
{
    return g_imei_initialized ? 1 : 0;
}

SdkResult device_utils_refresh_imei(void)
{
    sdk_log_info("Refreshing IMEI...\r\n");
    imei_reset();
    return device_utils_init();
}

SdkResult device_utils_deinit(void)
{
    sdk_log_info("Deinitializing device utilities\r\n");
    imei_reset();
    memset(g_st_firmware_version, 0, sizeof(g_st_firmware_version));
    g_st_fw_version_initialized = false;
    return SDK_RESULT_SUCCESS;
}

int device_utils_get_st_firmware_version(char *version_buffer)
{
    if (!version_buffer || !g_st_fw_version_initialized)
        return 0;

    du_strncpy_safe(version_buffer, g_st_firmware_version,
                    DEVICE_UTILS_ST_FW_VERSION_BUFFER_SIZE);
    return 1;
}

SdkResult device_utils_set_st_firmware_version(const char *version)
{
    size_t len = version ? strlen(version) : 0;

    if (len == 0 || len >= DEVICE_UTILS_ST_FW_VERSION_BUFFER_SIZE) {
        sdk_log_error("Invalid ST firmware version string (len=%u)\r\n",
                      (unsigned)len);
        return SDK_RESULT_INVALID_PARAM;
    }

    du_strncpy_safe(g_st_firmware_version, version,
                    sizeof(g_st_firmware_version));
    g_st_fw_version_initialized = true;
    sdk_log_info("ST firmware version set to: %s\r\n", g_st_firmware_version);
    return SDK_RESULT_SUCCESS;
}

int device_utils_is_st_firmware_version_initialized(void)
{
    return g_st_fw_version_initialized ? 1 : 0;
}

void device_utils_invalidate_st_firmware_version(void)
{
    /* Drop the cached ST version so BLE resumes polling get-device-info and
     * re-learns it. Used after an STM OTA: the STM reboots to the new version
     * and the stale cache would otherwise trigger a repeat OTA. */
    g_st_fw_version_initialized = false;
    g_st_firmware_version[0] = '\0';
    sdk_log_info("ST firmware version invalidated (will be re-queried)\r\n");
}

/*---------------------------------------------------------------
 * Peripheral Device Info API
 *--------------------------------------------------------------*/

static PeriDeviceInfo *g_peri_devices = NULL; /* heap-allocated on first use to save BSS */

void device_utils_peri_reset_all(void)
{
    if (!g_peri_devices) {
        g_peri_devices = (PeriDeviceInfo *)sdk_memory_alloc(
            PERI_DEVICE_MAX * sizeof(PeriDeviceInfo));
        if (!g_peri_devices) {
            sdk_log_error("peri_reset_all: alloc failed\r\n");
            return;
        }
    }
    memset(g_peri_devices, 0, PERI_DEVICE_MAX * sizeof(PeriDeviceInfo));
    for (UINT32 i = 0; i < PERI_DEVICE_MAX; i++) {
        g_peri_devices[i].device_id = (UINT8)(i + 1);
    }
}

static PeriDeviceInfo *peri_slot(UINT8 device_id)
{
    if (!g_peri_devices || device_id < 1 || device_id > PERI_DEVICE_MAX)
        return NULL;
    return &g_peri_devices[device_id - 1];
}

SdkResult device_utils_peri_set_ready(UINT8 device_id)
{
    PeriDeviceInfo *slot = peri_slot(device_id);
    if (!slot) {
        sdk_log_error("peri_set_ready: invalid device_id %u\r\n",
                      (unsigned)device_id);
        return SDK_RESULT_INVALID_PARAM;
    }
    slot->device_id = device_id;
    slot->is_ready  = true;
    return SDK_RESULT_SUCCESS;
}

SdkResult device_utils_peri_set_version(UINT8 device_id, UINT16 fw_version,
                                        UINT16 hw_version)
{
    PeriDeviceInfo *slot = peri_slot(device_id);
    if (!slot) {
        sdk_log_error("peri_set_version: invalid device_id %u\r\n",
                      (unsigned)device_id);
        return SDK_RESULT_INVALID_PARAM;
    }
    slot->fw_version    = fw_version;
    slot->hw_version    = hw_version;
    slot->version_valid = true;
    sdk_log_info("Peri[%u] version set: fw=%u hw=%u\r\n",
                 (unsigned)device_id, (unsigned)fw_version,
                 (unsigned)hw_version);
    return SDK_RESULT_SUCCESS;
}

SdkResult device_utils_peri_get_info(UINT8 device_id, PeriDeviceInfo *out)
{
    PeriDeviceInfo *slot = peri_slot(device_id);
    if (!slot || !out)
        return SDK_RESULT_INVALID_PARAM;
    *out = *slot;
    return SDK_RESULT_SUCCESS;
}

int device_utils_peri_is_ready(UINT8 device_id)
{
    PeriDeviceInfo *slot = peri_slot(device_id);
    return (slot && slot->is_ready) ? 1 : 0;
}

int device_utils_peri_has_version(UINT8 device_id)
{
    PeriDeviceInfo *slot = peri_slot(device_id);
    return (slot && slot->version_valid) ? 1 : 0;
}
