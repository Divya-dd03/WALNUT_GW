/**
 * @file flash_paths.h
 * @brief Central layout for modem flash files under @c C:/ (SIMCOM user area).
 *
 * Convention: one subdirectory per purpose. Call @c flash_paths_ensure_directories() after
 * @c file_system_init() so paths exist before first open (see @c system_manager_init).
 * Implementation: @c system/storage/flash_paths.c.
 *
 * | Folder   | Role |
 * |----------|------|
 * | @c C:/config/   | Config + @c GPS_LAST_VALID_FILE_PATH (GPS module) |
 * | @c C:/fota/     | OTA / ST firmware staging |
 * | @c C:/preboot/  | Pre-boot record (reset metadata + TCP queue resume) |
 * | @c C:/queue/    | Queue persistence snapshots |
 */

#ifndef WEWARE_FLASH_PATHS_H
#define WEWARE_FLASH_PATHS_H

#include "common/types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Modem user-area root (SIMCOM). */
#define FLASH_ROOT "C:/"

#define FLASH_DIR_CONFIG   FLASH_ROOT "config/"
#define FLASH_DIR_FOTA     FLASH_ROOT "fota/"
#define FLASH_DIR_PREBOOT  FLASH_ROOT "preboot/"
#define FLASH_DIR_QUEUE    FLASH_ROOT "queue/"

/** Default module configuration blob. */
#define CONFIG_FILE_PATH            FLASH_DIR_CONFIG "system_config.dat"

/** ST firmware image received via OTA before transfer to STM. */
#define ST_FIRMWARE_FILE_PATH       FLASH_DIR_FOTA "st_firmware.bin"

/** Pre-boot binary (reset metadata); see @c pre_boot_handler.h / @c post_boot_handler.h. */
#define PRE_BOOT_INFO_FILE_PATH     FLASH_DIR_PREBOOT "weware_preboot.bin"

/** Last valid GPS packet; owned by @c gps_storage.c. */
#define GPS_LAST_VALID_FILE_PATH    FLASH_DIR_CONFIG "gps_last_valid.bin"

/** Peripheral firmware image staging file (nRF or ESP32); deleted after each DFU attempt. */
#define PERI_FIRMWARE_FILE_PATH     FLASH_DIR_FOTA "peri_firmware.bin"

/* Maximum number of peripheral BLE device slots: the reference defines
 * PERI_DEVICE_MAX 5 here; on walnut the value (4U) is owned by
 * system/device_utils.h - include that instead of defining it twice. */

/**
 * @brief Create standard subdirectories if they do not exist.
 * @note Walnut: directory names are passed to the kernel FS without the trailing '/'
 *       (fs_makedir rejects it). Returns RESULT_ERROR if any directory is still missing.
 */
Result flash_paths_ensure_directories(void);

/**
 * @brief Ensure a single directory exists (exists check -> mkdir -> verify).
 * @param dir  Directory path, with or without trailing '/', e.g. @c FLASH_DIR_PREBOOT.
 */
Result flash_paths_ensure_directory(const char *dir);

/**
 * @brief Copy @p dir into @p out with trailing '/' removed (root "C:/" is kept).
 * @return TRUE on success, FALSE on bad args / buffer too small.
 */
BOOL flash_paths_dir_no_slash(const char *dir, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_FLASH_PATHS_H */
