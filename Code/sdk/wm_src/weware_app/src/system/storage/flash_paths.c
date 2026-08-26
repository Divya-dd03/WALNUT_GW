/**
 * @file flash_paths.c
 * @brief Ensure @c C:/config, @c fota, @c preboot, @c queue exist on flash.
 *
 * Declarations: @c system/storage/flash_paths.h
 *
 * Walnut delta vs CG: the kernel FS (fs_stat / fs_makedir, reached through
 * sdk_file_exists / sdk_file_mkdir -> wm_create_folder) does not accept a
 * trailing '/' on a directory path — the vendor libc wrapper strips it
 * (components/libc_wrap/c_wrap.c remove_postfix_if_needed) before mkdir, and
 * the vendor demo / gps_storage create "C:/wegwdir" / "C:/config" without one.
 * FLASH_DIR_* are defined with a trailing slash (so file paths concatenate),
 * so normalize here before exists/mkdir. Without this no subdirectory was
 * created and every open under C:/preboot/, C:/queue/ ... failed (wb+).
 */

#include "system/storage/flash_paths.h"
#include "system/storage/file_system.h"

#include "sdk_platform.h"
#include "functionality/sdk_functionality_file.h"

#include <string.h>

#define LOG_TAG          "FLASHP"
#define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

#define FLASH_PATHS_DIR_MAX 64

/* Copy @p dir into @p out without a trailing '/' (keeps "C:/" root intact). */
BOOL flash_paths_dir_no_slash(const char *dir, char *out, size_t out_size)
{
    size_t len;

    if (!dir || !out || out_size == 0U)
        return FALSE;

    len = strlen(dir);
    if (len == 0U || len + 1U > out_size)
        return FALSE;

    memcpy(out, dir, len + 1U);
    while (len > 3U && out[len - 1U] == '/') {   /* never strip "C:/" itself */
        out[len - 1U] = '\0';
        len--;
    }
    return TRUE;
}

Result flash_paths_ensure_directory(const char *dir)
{
    char path[FLASH_PATHS_DIR_MAX];

    if (!flash_paths_dir_no_slash(dir, path, sizeof(path)))
        return RESULT_INVALID_PARAM;

    if (sdk_file_exists(path) == SDK_RESULT_SUCCESS)
        return RESULT_SUCCESS;

    if (file_system_mkdir(path) != RESULT_SUCCESS) {
        LOG_ERROR("ensure_dir: mkdir failed '%s'", path);
        return RESULT_ERROR;
    }

    /* Verify: wm_create_folder can report success while the kernel refused. */
    if (sdk_file_exists(path) != SDK_RESULT_SUCCESS) {
        LOG_ERROR("ensure_dir: '%s' still missing after mkdir", path);
        return RESULT_ERROR;
    }

    LOG_WARN("ensure_dir: created '%s'", path);
    return RESULT_SUCCESS;
}

Result flash_paths_ensure_directories(void)
{
    static const char *const dirs[] = {
        FLASH_DIR_CONFIG,
        FLASH_DIR_FOTA,
        FLASH_DIR_PREBOOT,
        FLASH_DIR_QUEUE,
    };
    Result overall = RESULT_SUCCESS;

    for (unsigned i = 0; i < (unsigned)(sizeof(dirs) / sizeof(dirs[0])); i++) {
        if (flash_paths_ensure_directory(dirs[i]) != RESULT_SUCCESS)
            overall = RESULT_ERROR;
    }

    return overall;
}
