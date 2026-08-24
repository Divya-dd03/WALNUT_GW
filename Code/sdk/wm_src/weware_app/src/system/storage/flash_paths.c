/**
 * @file flash_paths.c
 * @brief Ensure @c C:/config, @c fota, @c preboot, @c queue exist on flash.
 *
 * Declarations: @c system/storage/flash_paths.h
 */

#include "system/storage/flash_paths.h"
#include "system/storage/file_system.h"

#include "sdk_platform.h"
#include "functionality/sdk_functionality_file.h"

Result flash_paths_ensure_directories(void)
{
    static const char *const dirs[] = {
        FLASH_DIR_CONFIG,
        FLASH_DIR_FOTA,
        FLASH_DIR_PREBOOT,
        FLASH_DIR_QUEUE,
    };

    for (unsigned i = 0; i < (unsigned)(sizeof(dirs) / sizeof(dirs[0])); i++) {
        if (sdk_file_exists(dirs[i]) == SDK_RESULT_SUCCESS)
            continue;
        (void)file_system_mkdir(dirs[i]);
    }

    return RESULT_SUCCESS;
}
