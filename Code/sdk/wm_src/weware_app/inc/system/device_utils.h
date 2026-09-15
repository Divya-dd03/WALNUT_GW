/**
  ******************************************************************************
  * @file    device_utils.h
  * @author  WheelsEye
  * @brief   Device utility module public interface for the weware application.
  *          Caches the device IMEI, the ST co-processor firmware version and
  *          peripheral device info (ready state + fw/hw versions).
  ******************************************************************************
  */

#ifndef WEWARE_DEVICE_UTILS_H
#define WEWARE_DEVICE_UTILS_H

#include <stdbool.h>

#include "wm_sdk_types.h"

/* IMEI is 15 digits; buffers passed to the IMEI getters must hold at least
 * DEVICE_UTILS_IMEI_BUFFER_SIZE bytes (digits + NUL). */
#define DEVICE_UTILS_IMEI_LENGTH               (15U)
#define DEVICE_UTILS_IMEI_BUFFER_SIZE          (16U)

/* Minimum buffer size (bytes) for device_utils_get_st_firmware_version() */
#define DEVICE_UTILS_ST_FW_VERSION_BUFFER_SIZE (32U)

/* Number of tracked peripheral devices; valid device_id range is
 * 1..PERI_DEVICE_MAX. */
#define PERI_DEVICE_MAX                        (4U)

/* Cached state of one peripheral device */
typedef struct {
    UINT8  device_id;      /* 1..PERI_DEVICE_MAX                            */
    bool   is_ready;       /* device announced itself ready                 */
    bool   version_valid;  /* fw_version/hw_version below are populated     */
    UINT16 fw_version;
    UINT16 hw_version;
} PeriDeviceInfo;

/**
 * @brief  Read the IMEI from the modem (with retries) and cache it.
 * @return WM_SDK_RESULT_SUCCESS (also when already initialized),
 *         WM_SDK_RESULT_NOT_SUPPORTED if the platform lacks the API,
 *         WM_SDK_RESULT_ERROR after all retries fail.
 */
wm_SdkResult device_utils_init(void);

/**
 * @brief  Copy the cached IMEI into @p imei_buffer.
 * @param  imei_buffer  [out] buffer of at least DEVICE_UTILS_IMEI_BUFFER_SIZE
 *                      bytes.
 * @return 1 on success; 0 if not initialized or @p imei_buffer is NULL.
 */
int device_utils_get_imei(char *imei_buffer);

/**
 * @brief  Manually override the cached IMEI (test/provisioning use).
 * @return WM_SDK_RESULT_SUCCESS or WM_SDK_RESULT_INVALID_PARAM.
 */
wm_SdkResult device_utils_set_imei(const char *imei);

/**
 * @brief  @return 1 if the IMEI cache is populated, 0 otherwise.
 */
int device_utils_is_imei_initialized(void);

/**
 * @brief  Drop the cached IMEI and re-read it from the modem.
 * @return same as device_utils_init().
 */
wm_SdkResult device_utils_refresh_imei(void);

/**
 * @brief  Clear all cached state (IMEI + ST firmware version).
 * @return WM_SDK_RESULT_SUCCESS.
 */
wm_SdkResult device_utils_deinit(void);

/**
 * @brief  Copy the cached ST firmware version into @p version_buffer.
 * @param  version_buffer  [out] buffer of at least
 *                         DEVICE_UTILS_ST_FW_VERSION_BUFFER_SIZE bytes.
 * @return 1 on success; 0 if not set or @p version_buffer is NULL.
 */
int device_utils_get_st_firmware_version(char *version_buffer);

/**
 * @brief  Cache the ST firmware version (learned from get-device-info).
 * @return WM_SDK_RESULT_SUCCESS or WM_SDK_RESULT_INVALID_PARAM.
 */
wm_SdkResult device_utils_set_st_firmware_version(const char *version);

/**
 * @brief  @return 1 if the ST firmware version cache is populated, 0 otherwise.
 */
int device_utils_is_st_firmware_version_initialized(void);

/**
 * @brief  Drop the cached ST version so it is re-queried. Used after an STM
 *         OTA: the STM reboots to the new version and a stale cache would
 *         otherwise trigger a repeat OTA.
 */
void device_utils_invalidate_st_firmware_version(void);

/*---------------------------------------------------------------
 * Peripheral Device Info API (device_id range: 1..PERI_DEVICE_MAX)
 *--------------------------------------------------------------*/

/**
 * @brief  Allocate (first call) and clear the peripheral info table.
 *         Must be called before any other peri call.
 */
void device_utils_peri_reset_all(void);

/**
 * @brief  Mark a peripheral as ready.
 * @return WM_SDK_RESULT_SUCCESS or WM_SDK_RESULT_INVALID_PARAM.
 */
wm_SdkResult device_utils_peri_set_ready(UINT8 device_id);

/**
 * @brief  Store a peripheral's firmware/hardware versions.
 * @return WM_SDK_RESULT_SUCCESS or WM_SDK_RESULT_INVALID_PARAM.
 */
wm_SdkResult device_utils_peri_set_version(UINT8 device_id, UINT16 fw_version,
                                        UINT16 hw_version);

/**
 * @brief  Copy a peripheral's cached info into @p out.
 * @return WM_SDK_RESULT_SUCCESS or WM_SDK_RESULT_INVALID_PARAM.
 */
wm_SdkResult device_utils_peri_get_info(UINT8 device_id, PeriDeviceInfo *out);

/**
 * @brief  @return 1 if the peripheral has been marked ready, 0 otherwise.
 */
int device_utils_peri_is_ready(UINT8 device_id);

/**
 * @brief  @return 1 if the peripheral's versions are populated, 0 otherwise.
 */
int device_utils_peri_has_version(UINT8 device_id);

#endif /* WEWARE_DEVICE_UTILS_H */
