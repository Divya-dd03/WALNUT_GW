/**
 * @file ota_manager.h
 * @brief OTA (Over-The-Air) update manager API
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include "common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Shared HTTPS endpoint for SIMCOM, ST, and peripheral version checks. */
#define OTA_CHECK_VERSION_URL_PROD "https://wheelseye.com/device/check-version-update/v2"
#define OTA_CHECK_VERSION_URL_STAGE "https://trucking-web.stage.wheelseye.in/device/check-version-update/v2"

/* Download/HTTP helpers (ota_flash_has_space_for_download,
 * ota_download_and_write_firmware) now live in ota_file_download.h */

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/

/**
 * @brief Initialize OTA manager and register for network and GPS events
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 * 
 * @note On init, deletes all regular files under @c FLASH_DIR_FOTA (e.g. leftover
 *       @c st_firmware.bin, @c peri_firmware.bin) so flash use does not grow across reboots.
 * @note Registers for EVENT_NETWORK_CONNECTED/DISCONNECTED and EVENT_GPS_CONFIGURED/DISCONNECTED.
 */
Result ota_manager_init(void);

/**
 * @brief Deinitialize OTA manager and unregister from events
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result ota_manager_deinit(void);

/*---------------------------------------------------------------
 * OTA Upgrade Type Enumeration
 *--------------------------------------------------------------*/

typedef enum
{
    OTA_UPGRADE_SIMCOM = 0,  /**< SIMCOM firmware upgrade (write to partition) */
    OTA_UPGRADE_ST = 1       /**< ST firmware upgrade (write to file) */
} OtaUpgradeType;

/*---------------------------------------------------------------
 * OTA Device Routing
 *--------------------------------------------------------------*/

/**
 * @brief Target device for a single OTA operation.
 *
 * Used by the dispatcher (@c ota_manager_run_device) to route to the correct
 * per-device handler. Add new device types here plus a matching @c case in the
 * dispatcher and one @c ota_run_* handler — nothing else needs to change.
 */
typedef enum
{
    OTA_DEVICE_SIMCOM = 0,   /**< This modem's own application firmware */
    OTA_DEVICE_ST,           /**< STM companion MCU firmware            */
    OTA_DEVICE_PERIPHERAL,   /**< BLE peripherals (nRF / ESP32 / fuel)  */
    OTA_DEVICE_COUNT         /**< Keep last — count/iteration sentinel  */
} OtaDevice;

/**
 * @brief Outcome of a single OTA operation.
 */
typedef enum
{
    OTA_RESULT_NO_UPDATE = 0,  /**< Server checked, nothing to do            */
    OTA_RESULT_UPDATED,        /**< Update applied/started (reset may follow)*/
    OTA_RESULT_IN_PROGRESS,    /**< Multi-tick op still running (peripheral) */
    OTA_RESULT_SKIPPED,        /**< Prerequisites not met this call          */
    OTA_RESULT_ERROR           /**< Operation failed                         */
} OtaResult;

/**
 * @brief Run an OTA for a single, explicitly chosen device.
 *
 * Builds the shared context (IMEI fetched once and cached), re-checks
 * prerequisites, then dispatches to exactly one device handler. Unlike the
 * periodic driver this ignores the 12-hour cooldown, so it is safe to wire to
 * an on-demand command (e.g. "update ST only").
 *
 * @param device Which device to update.
 * @return One of @ref OtaResult. For @c OTA_DEVICE_PERIPHERAL a return of
 *         @c OTA_RESULT_IN_PROGRESS means the cycle continues on subsequent
 *         periodic ticks via the normal in-progress machinery.
 */
OtaResult ota_manager_run_device(OtaDevice device);

/*---------------------------------------------------------------
 * OTA Update Functions
 *--------------------------------------------------------------*/

/**
 * @brief Check if update is required and store download URL
 * @param firmware_version Firmware version string (e.g., "0201")
 * @param hardware_version Hardware version string (e.g., "0106")
 * @param imei IMEI string
 * @param upgrade_type Upgrade type: OTA_UPGRADE_SIMCOM or OTA_UPGRADE_ST
 * @return 1 if update required (URL stored), 0 if no update needed, -1 on error
 * 
 * @note This function checks for updates via HTTPS API and stores the download URL
 *       in global state if an update is available.
 * @note Versions are automatically formatted for the OTA API
 *       (e.g., "0201" -> "2.01", "0106" -> "CG_1.06")
 */
int ota_check_update_required(const char *firmware_version, const char *hardware_version,
                               const char *imei, int upgrade_type);

/*---------------------------------------------------------------
 * Main OTA Check Function
 *--------------------------------------------------------------*/

/**
 * @brief Main OTA check function called from main loop
 * @return 1 on success, 0 on failure or no update needed
 * 
 * @note This function performs a complete OTA update cycle:
 *       1. Checks prerequisites (network, GPS, cooldown)
 *       2. Checks for SIMCOM firmware update
 *       3. Checks for ST firmware update
 *       4. Downloads and applies updates as needed
 *       5. For ST updates, initiates file transfer to STM
 * 
 * @note Uses device information from weware_version.h and device_utils
 * @note Respects cooldown period to prevent excessive API calls
 */
int ota_manager_check_and_update(void);

/**
 * @brief Arm a one-shot forced OTA cycle (FORCED-OTA command).
 *
 * Bypasses the prerequisite gate (network/GPS/stable-link/motion) and the 12-hour
 * cooldown for a single cycle. Does NOT block: the next periodic
 * ota_manager_check_and_update() tick runs the cycle (SIMCOM -> ST -> peripheral).
 * The server version check still runs, so an OTA only happens when the server
 * actually has a different image. Normal gating resumes once the cycle completes.
 */
Result ota_manager_force_update(void);

/**
 * @brief TRUE if an OTA is currently running (peripheral cycle in progress, or an ST
 *        firmware transfer to the STM actively sending/awaiting). Used to reject a
 *        FORCED-OTA that would otherwise restart an in-flight session.
 */
BOOL ota_manager_is_busy(void);

/*---------------------------------------------------------------
 * OTA status readback (field debugging via SMS — no logs available)
 *--------------------------------------------------------------*/

/**
 * @brief Coarse reason for the last OTA outcome per device.
 * @note Recorded persistently so an SMS command can report *why* an OTA did not
 *       complete on a field device (where UART logs are unavailable).
 */
typedef enum
{
    OTA_REASON_NONE = 0,        /**< No attempt since boot                          */
    OTA_REASON_OK,              /**< Last cycle succeeded                           */
    OTA_REASON_ST_VER_UNKNOWN,  /**< STM never reported its fw version -> ST skipped */
    OTA_REASON_NO_UPDATE,       /**< Server reports already up to date              */
    OTA_REASON_VERSION_CHECK,   /**< Version-check request/parse failed             */
    OTA_REASON_DOWNLOAD,        /**< Download / flash-space / write failed          */
    OTA_REASON_XFER_TIMEOUT,    /**< STM not responding during UART transfer        */
    OTA_REASON_XFER_REJECT,     /**< STM rejected a chunk / CRC (mismatch)          */
    OTA_REASON_XFER_LOCAL       /**< Local flash-read / UART-send / init error      */
} OtaReason;

/**
 * @brief Build a short human-readable OTA status line for SMS/TCP (GET-OTA-STATUS).
 * @param out  Output buffer.
 * @param size Buffer size.
 * @return Number of characters written (>=0), or 0 on bad args.
 * @note Reports STM + SIMCOM last outcome and the learned ST firmware version.
 *       Kept well under the ~160-byte SMS limit.
 */
int ota_manager_get_status_string(char *out, int size);

#ifdef __cplusplus
}
#endif

#endif /* OTA_MANAGER_H */
