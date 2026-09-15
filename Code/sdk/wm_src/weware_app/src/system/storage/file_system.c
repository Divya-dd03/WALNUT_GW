/**
 * @file file_system.c
 * @brief High-level file system helpers for weware
 */

#include "system/storage/file_system.h"
#include "system/storage/flash_paths.h"

/* SDK Platform Abstraction Layer */
#include "sdk_platform.h"
#include "functionality/sdk_functionality_file.h"

/* Vendor FS API for directory listing (fs_opendir/fs_readdir/fs_closedir),
 * exported by the kernel via core_stub.o. */
#include "fs_api.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

/*---------------------------------------------------------------
 * Log Configuration
 *--------------------------------------------------------------*/
#define LOG_TAG          "FILESYS"
#define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

/*---------------------------------------------------------------
 * Internal Helpers
 *--------------------------------------------------------------*/

/* Expands inline — safe to pass to LOG_* even when p is NULL. */
#define FS_PATH_STR(p) (((p) != NULL) ? (p) : "(null)")

/*---------------------------------------------------------------
 * Walnut adapters (reference: sdk_file_get_disk_info / sdk_file_list_dir,
 * which the walnut wm_sdk_file.h does not provide)
 *--------------------------------------------------------------*/

/** Matches the reference SdkFileDirEntry layout (== FileInfo). */
typedef FileInfo SdkFileDirEntry;

/* Disk info via wm_sdk_system_get_stats (flash totals are for the internal
 * C:/ user area; other roots are not supported on walnut). */
static wm_SdkResult sdk_file_get_disk_info(const char *root_path,
                                        INT64 *total_size,
                                        INT64 *free_size,
                                        INT64 *used_size)
{
    UINT32 ram_total_kb = 0, ram_free_kb = 0;
    INT64  flash_total_kb = 0, flash_free_kb = 0;
    UINT8  cpu = 0;

    if (!root_path)
        return WM_SDK_RESULT_INVALID_PARAM;
    if (strncasecmp(root_path, "C:/", 3) != 0 && strncasecmp(root_path, "C:", 2) != 0)
        return WM_SDK_RESULT_NOT_SUPPORTED;

    if (wm_sdk_system_get_stats(&ram_total_kb, &ram_free_kb,
                             &flash_total_kb, &flash_free_kb, &cpu) != WM_SDK_RESULT_SUCCESS)
        return WM_SDK_RESULT_ERROR;

    if (total_size) *total_size = flash_total_kb * 1024;
    if (free_size)  *free_size  = flash_free_kb * 1024;
    if (used_size)  *used_size  = (flash_total_kb - flash_free_kb) * 1024;
    return WM_SDK_RESULT_SUCCESS;
}

/* Directory listing over the vendor fs_opendir/fs_readdir/fs_closedir API.
 *
 * Kernel behaviour (from cp.map + disassembly of the vendor readdir wrapper
 * in lib_wmsrc_B.a c_wrap.c.obj):
 *   - fs_opendir wants the path WITHOUT a trailing slash (same kernel rule
 *     as fs_stat/fs_makedir) and returns 0 on failure;
 *   - fs_readdir returns (uint32_t)-1 on failure, and end-of-directory is
 *     signalled by an EMPTY file_name, not by the return code;
 *   - DirFileInfo_t.permissions == 0x200 marks a directory (the vendor
 *     wrapper maps exactly that to DT_DIR).
 */
#define FS_KERNEL_PERM_DIR 0x200U

static wm_SdkResult sdk_file_list_dir(const char *path,
                                   SdkFileDirEntry *entries,
                                   UINT32 max_entries,
                                   UINT32 *out_count)
{
    if (out_count)
        *out_count = 0;
    if (!path || !entries || max_entries == 0U || !out_count)
        return WM_SDK_RESULT_INVALID_PARAM;

    /* Strip a trailing slash (keep the "C:/" root intact). */
    char dir[128];
    size_t len = strlen(path);
    if (len == 0U || len >= sizeof(dir))
        return WM_SDK_RESULT_INVALID_PARAM;
    memcpy(dir, path, len + 1U);
    if (len > 3U && dir[len - 1U] == '/')
        dir[len - 1U] = '\0';

    uint32_t stream = fs_opendir(dir);
    if (stream == 0U) {
        LOG_WARN("list_dir: fs_opendir failed '%s'", dir);
        return WM_SDK_RESULT_ERROR;
    }

    UINT32 count = 0U;
    while (count < max_entries) {
        DirFileInfo_t info;
        memset(&info, 0, sizeof(info));

        uint32_t rc = fs_readdir((int)stream, &info);
        if (rc == (uint32_t)-1 || info.file_name[0] == '\0')
            break;   /* -1 = failure, empty name = end of directory */

        SdkFileDirEntry *e = &entries[count];
        strncpy(e->name, info.file_name, sizeof(e->name) - 1U);
        e->name[sizeof(e->name) - 1U] = '\0';
        e->size = (long)info.size;
        e->type = (info.permissions == FS_KERNEL_PERM_DIR) ? 1 : 0;
        count++;
    }

    (void)fs_closedir((int)stream);
    *out_count = count;
    return WM_SDK_RESULT_SUCCESS;
}

/* Create the parent directory of @p path if it is missing.
 * Returns TRUE if the parent now exists (so the caller should retry open). */
static BOOL fs_ensure_parent_dir(const char *path)
{
    char        dir[64];
    const char *slash = strrchr(path, '/');
    size_t      len;

    if (!slash)
        return FALSE;
    len = (size_t)(slash - path);
    if (len < 3U || len + 1U > sizeof(dir))   /* need at least "C:/x" */
        return FALSE;
    memcpy(dir, path, len);
    dir[len] = '\0';
    if (strcmp(dir, "C:") == 0 || strcmp(dir, "C:/") == 0)
        return FALSE;                           /* root always exists */

    if (wm_sdk_file_exists(dir) == WM_SDK_RESULT_SUCCESS)
        return FALSE;                           /* parent fine, open failed for another reason */

    LOG_WARN("parent dir '%s' missing, creating", dir);
    (void)wm_sdk_file_mkdir(dir);
    return (wm_sdk_file_exists(dir) == WM_SDK_RESULT_SUCCESS);
}

static Result file_system_do_delete(const char *path, const char *op_name)
{
    if (!path) {
        LOG_WARN("%s: invalid parameters (path is null)", op_name);
        return RESULT_INVALID_PARAM;
    }

    LOG_INFO("%s path='%s'", op_name, path);

    wm_SdkResult ret = wm_sdk_file_delete(path);
    if (ret != WM_SDK_RESULT_SUCCESS) {
        LOG_ERROR("%s: failed path='%s' sdk_ret=%d", op_name, path, (int)ret);
        return RESULT_ERROR;
    }

    return RESULT_SUCCESS;
}

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/

Result file_system_init(void)
{
    LOG_INFO("init");
    return RESULT_SUCCESS;
}

Result file_system_write_file(const char *path,
                              const char *data,
                              UINT32 data_len)
{
    if (!path || !data || data_len == 0U) {
        LOG_WARN("write_file: invalid parameters (path=%s, data=%p, len=%u)",
                 FS_PATH_STR(path), (const void *)data, (unsigned)data_len);
        return RESULT_INVALID_PARAM;
    }

    LOG_INFO("write_file path='%s' len=%u", path, (unsigned)data_len);

    void *file = wm_sdk_file_open(path, "wb+");
    if (!file) {
        /* Walnut: fs_open does not create parent directories. If the parent
         * is one of our FLASH_DIR_* folders and is missing (e.g. first boot
         * before/without flash_paths_ensure_directories), create it once and
         * retry, so a single missing folder doesn't wedge persistence. */
        if (fs_ensure_parent_dir(path))
            file = wm_sdk_file_open(path, "wb+");
    }
    if (!file) {
        LOG_ERROR("write_file: open failed path='%s' mode=wb+", path);
        return RESULT_ERROR;
    }

    UINT32    written = 0;
    wm_SdkResult ret     = wm_sdk_file_write(file, data, data_len, &written);
    if (ret != WM_SDK_RESULT_SUCCESS || written != data_len) {
        LOG_ERROR("write_file: write failed path='%s' sdk_ret=%d written=%u expected=%u",
                  path, (int)ret, (unsigned)written, (unsigned)data_len);
        (void)wm_sdk_file_close(file);
        return RESULT_ERROR;
    }

    ret = wm_sdk_file_close(file);
    if (ret != WM_SDK_RESULT_SUCCESS) {
        LOG_WARN("write_file: close failed path='%s' sdk_ret=%d (data may be committed)",
                 path, (int)ret);
        return RESULT_ERROR;
    }

    return RESULT_SUCCESS;
}

Result file_system_read_file(const char *path,
                             char *buffer,
                             UINT32 buffer_size,
                             UINT32 *out_read_len)
{
    if (!path || !buffer || buffer_size == 0U) {
        LOG_WARN("read_file: invalid parameters (path=%s, buffer=%p, buffer_size=%u)",
                 FS_PATH_STR(path), (void *)buffer, (unsigned)buffer_size);
        return RESULT_INVALID_PARAM;
    }

    LOG_INFO("read_file path='%s' buffer_size=%u", path, (unsigned)buffer_size);

    void *file = wm_sdk_file_open(path, "rb");
    if (!file) {
        LOG_WARN("read_file: open failed path='%s' mode=rb", path);
        return RESULT_NOT_FOUND;
    }

    UINT32 file_size = buffer_size;
    if (wm_sdk_file_get_size(file, &file_size) != WM_SDK_RESULT_SUCCESS)
        file_size = buffer_size;
    UINT32 to_read = (file_size < buffer_size) ? file_size : buffer_size;

    memset(buffer, 0, buffer_size);

    UINT32    read_len = 0;
    wm_SdkResult ret      = wm_sdk_file_read(file, buffer, to_read, &read_len);
    if (ret != WM_SDK_RESULT_SUCCESS || (read_len == 0U && to_read > 0U)) {
        LOG_ERROR("read_file: read failed path='%s' sdk_ret=%d read_len=%u to_read=%u",
                  path, (int)ret, (unsigned)read_len, (unsigned)to_read);
        (void)wm_sdk_file_close(file);
        return RESULT_ERROR;
    }

    if (out_read_len)
        *out_read_len = read_len;

    ret = wm_sdk_file_close(file);
    if (ret != WM_SDK_RESULT_SUCCESS) {
        LOG_WARN("read_file: close failed path='%s' sdk_ret=%d (data already in buffer)",
                 path, (int)ret);
    }

    return RESULT_SUCCESS;
}

Result file_system_get_file_size(const char *path, UINT32 *out_size)
{
    if (!path || !out_size) {
        LOG_WARN("get_file_size: invalid parameters (path=%s, out_size=%p)",
                 FS_PATH_STR(path), (void *)out_size);
        return RESULT_INVALID_PARAM;
    }

    LOG_INFO("get_file_size path='%s'", path);

    void *file = wm_sdk_file_open(path, "rb");
    if (!file) {
        LOG_WARN("get_file_size: open failed path='%s'", path);
        return RESULT_NOT_FOUND;
    }

    UINT32    size = 0;
    wm_SdkResult ret  = wm_sdk_file_get_size(file, &size);
    wm_SdkResult cl   = wm_sdk_file_close(file);

    if (cl != WM_SDK_RESULT_SUCCESS) {
        LOG_WARN("get_file_size: close failed path='%s' sdk_ret=%d", path, (int)cl);
    }
    if (ret != WM_SDK_RESULT_SUCCESS) {
        LOG_ERROR("get_file_size: get_size failed path='%s' sdk_ret=%d", path, (int)ret);
        return RESULT_ERROR;
    }

    *out_size = size;
    return RESULT_SUCCESS;
}

Result file_system_read_at_offset(const char *path,
                                  char *buffer,
                                  UINT32 buffer_size,
                                  UINT32 offset,
                                  UINT32 *out_read_len)
{
    if (!path || !buffer || buffer_size == 0U) {
        LOG_WARN("read_at_offset: invalid parameters (path=%s, buffer=%p, buffer_size=%u)",
                 FS_PATH_STR(path), (void *)buffer, (unsigned)buffer_size);
        return RESULT_INVALID_PARAM;
    }

    void *file = wm_sdk_file_open(path, "rb");
    if (!file) {
        LOG_WARN("read_at_offset: open failed path='%s'", path);
        return RESULT_NOT_FOUND;
    }

    if (offset > 0U && wm_sdk_file_seek(file, (INT32)offset, 0) != WM_SDK_RESULT_SUCCESS) {
        LOG_ERROR("read_at_offset: seek failed path='%s' offset=%u", path, (unsigned)offset);
        (void)wm_sdk_file_close(file);
        return RESULT_ERROR;
    }

    UINT32    read_len = 0;
    wm_SdkResult ret      = wm_sdk_file_read(file, buffer, buffer_size, &read_len);
    wm_SdkResult cl       = wm_sdk_file_close(file);

    if (cl != WM_SDK_RESULT_SUCCESS) {
        LOG_WARN("read_at_offset: close failed path='%s' sdk_ret=%d", path, (int)cl);
    }
    if (ret != WM_SDK_RESULT_SUCCESS) {
        LOG_ERROR("read_at_offset: read failed path='%s' offset=%u sdk_ret=%d",
                  path, (unsigned)offset, (int)ret);
        return RESULT_ERROR;
    }

    if (out_read_len)
        *out_read_len = read_len;

    return RESULT_SUCCESS;
}

Result file_system_delete(const char *path)
{
    return file_system_do_delete(path, "delete");
}

Result file_system_rename(const char *old_path, const char *new_path)
{
    if (!old_path || !new_path) {
        LOG_WARN("rename: invalid parameters (old=%s, new=%s)",
                 FS_PATH_STR(old_path), FS_PATH_STR(new_path));
        return RESULT_INVALID_PARAM;
    }

    LOG_INFO("rename '%s' -> '%s'", old_path, new_path);

    wm_SdkResult ret = wm_sdk_file_rename(old_path, new_path);
    if (ret == WM_SDK_RESULT_NOT_SUPPORTED) {
        LOG_WARN("rename: not supported by platform '%s' -> '%s'", old_path, new_path);
        return RESULT_NOT_SUPPORTED;
    }
    if (ret != WM_SDK_RESULT_SUCCESS) {
        LOG_ERROR("rename: failed '%s' -> '%s' sdk_ret=%d", old_path, new_path, (int)ret);
        return RESULT_ERROR;
    }

    return RESULT_SUCCESS;
}

Result file_system_mkdir(const char *path)
{
    if (!path) {
        LOG_WARN("mkdir: invalid parameters (path is null)");
        return RESULT_INVALID_PARAM;
    }

    LOG_INFO("mkdir path='%s'", path);

    wm_SdkResult ret = wm_sdk_file_mkdir(path);
    if (ret == WM_SDK_RESULT_NOT_SUPPORTED) {
        LOG_WARN("mkdir: not supported by platform path='%s'", path);
        return RESULT_NOT_SUPPORTED;
    }
    if (ret != WM_SDK_RESULT_SUCCESS) {
        LOG_ERROR("mkdir: failed path='%s' sdk_ret=%d", path, (int)ret);
        return RESULT_ERROR;
    }

    return RESULT_SUCCESS;
}

Result file_system_remove(const char *path)
{
    /* Alias for delete (matches SIMCom demo semantics) */
    return file_system_do_delete(path, "remove");
}

Result file_system_get_disk_info(const char *root_path,
                                 INT64 *total_size,
                                 INT64 *free_size,
                                 INT64 *used_size)
{
    if (!root_path) {
        LOG_WARN("disk_info: invalid parameters (root_path is null)");
        return RESULT_INVALID_PARAM;
    }

    LOG_INFO("disk_info root='%s'", root_path);

    wm_SdkResult sr = sdk_file_get_disk_info(root_path, total_size, free_size, used_size);
    if (sr == WM_SDK_RESULT_NOT_SUPPORTED) {
        /* Expected on some SDK ports; caller uses return code. */
        return RESULT_NOT_SUPPORTED;
    }
    if (sr != WM_SDK_RESULT_SUCCESS) {
        LOG_ERROR("disk_info: failed root='%s' sdk_ret=%d", root_path, (int)sr);
        return RESULT_ERROR;
    }

    return RESULT_SUCCESS;
}

Result file_system_list_dir(const char *path,
                            FileInfo *entries,
                            UINT32 max_entries,
                            UINT32 *out_count)
{
    if (!path || !entries || max_entries == 0U) {
        LOG_WARN("list_dir: invalid parameters (path=%s, entries=%p, max_entries=%u)",
                 FS_PATH_STR(path), (void *)entries, (unsigned)max_entries);
        return RESULT_INVALID_PARAM;
    }

    LOG_INFO("list_dir path='%s' max_entries=%u", path, (unsigned)max_entries);

    if (out_count) *out_count = 0;

    /* FileInfo must match SdkFileDirEntry (sdk_functionality_file.h). */
    wm_SdkResult sr = sdk_file_list_dir(path, (SdkFileDirEntry *)entries, max_entries, out_count);
    if (sr == WM_SDK_RESULT_NOT_SUPPORTED)
        return RESULT_NOT_SUPPORTED;
    if (sr != WM_SDK_RESULT_SUCCESS) {
        LOG_ERROR("list_dir: failed path='%s' sdk_ret=%d", path, (int)sr);
        return RESULT_ERROR;
    }

    return RESULT_SUCCESS;
}

/*---------------------------------------------------------------
 * Flat directory purge (no recursion)
 *--------------------------------------------------------------*/

#define FS_DIR_PURGE_PATH_MAX  320
#define FS_DIR_PURGE_BATCH     16
#define FS_ENTRY_TYPE_DIR      1

static FileInfo s_fs_dir_purge_batch[FS_DIR_PURGE_BATCH];

static BOOL fs_dir_purge_path_ok(const char *path)
{
    if (!path || path[0] == '\0')
        return FALSE;
    if (strstr(path, "..") != NULL)
        return FALSE;
    return (strncasecmp(path, "C:/", 3) == 0);
}

static void fs_dir_purge_ensure_slash(char *dir, size_t cap)
{
    size_t len = strlen(dir);
    if (len == 0U || len + 2U > cap)
        return;
    if (dir[len - 1U] != '/') {
        dir[len]      = '/';
        dir[len + 1U] = '\0';
    }
}

Result file_system_delete_all_files_in_directory(const char *dir_path,
                                                 UINT32 *out_deleted,
                                                 UINT32 *out_failed)
{
    if (out_deleted) *out_deleted = 0U;
    if (out_failed)  *out_failed  = 0U;

    if (!dir_path) {
        LOG_WARN("dir_purge: null path");
        return RESULT_INVALID_PARAM;
    }

    char folder[FS_DIR_PURGE_PATH_MAX];
    if (snprintf(folder, sizeof(folder), "%s", dir_path) >= (int)sizeof(folder))
        return RESULT_INVALID_PARAM;

    fs_dir_purge_ensure_slash(folder, sizeof(folder));

    if (!fs_dir_purge_path_ok(folder)) {
        LOG_WARN("dir_purge: path not under C:/ '%s'", folder);
        return RESULT_INVALID_PARAM;
    }

    UINT32 total_deleted = 0U;
    UINT32 total_failed  = 0U;

    for (;;) {
        UINT32    count = 0U;
        wm_SdkResult sr    = sdk_file_list_dir(folder, (SdkFileDirEntry *)s_fs_dir_purge_batch,
                                            FS_DIR_PURGE_BATCH, &count);
        if (sr == WM_SDK_RESULT_NOT_SUPPORTED)
            return RESULT_NOT_SUPPORTED;
        if (sr != WM_SDK_RESULT_SUCCESS) {
            LOG_WARN("dir_purge: list_dir failed '%s' sdk_ret=%d", folder, (int)sr);
            return RESULT_ERROR;
        }
        if (count == 0U)
            break;

        UINT32 deleted_this_round = 0U;
        for (UINT32 i = 0U; i < count; i++) {
            const char *ent = s_fs_dir_purge_batch[i].name;
            if (ent[0] == '\0' || strcmp(ent, ".") == 0 || strcmp(ent, "..") == 0)
                continue;
            if (s_fs_dir_purge_batch[i].type == FS_ENTRY_TYPE_DIR)
                continue;

            char   child[FS_DIR_PURGE_PATH_MAX];
            size_t fl = strlen(folder);
            size_t nl = strlen(ent);
            if (fl + nl + 1U > sizeof(child)) {
                total_failed++;
                continue;
            }
            memcpy(child, folder, fl);
            memcpy(child + fl, ent, nl);
            child[fl + nl] = '\0';

            if (!fs_dir_purge_path_ok(child)) {
                total_failed++;
                continue;
            }

            wm_SdkResult dr = wm_sdk_file_delete(child);
            if (dr == WM_SDK_RESULT_SUCCESS) {
                deleted_this_round++;
                total_deleted++;
            } else {
                LOG_WARN("dir_purge: delete failed '%s' sdk_ret=%d", child, (int)dr);
                total_failed++;
            }
        }

        if (deleted_this_round == 0U && count > 0U) {
            LOG_WARN("dir_purge: stalled in '%s'", folder);
            return RESULT_ERROR;
        }
    }

    if (out_deleted) *out_deleted = total_deleted;
    if (out_failed)  *out_failed  = total_failed;

    return RESULT_SUCCESS;
}

Result file_system_wipe_user_flash_c(void)
{
    static const char *const k_dirs[] = {
        FLASH_DIR_CONFIG,
        FLASH_DIR_QUEUE,
        FLASH_DIR_FOTA,
        FLASH_DIR_PREBOOT,
    };

    LOG_WARN("wipe_user_flash_c: clearing config, queue, fota, preboot");

    for (size_t i = 0; i < sizeof(k_dirs) / sizeof(k_dirs[0]); i++) {
        Result r = file_system_delete_all_files_in_directory(k_dirs[i], NULL, NULL);
        if (r == RESULT_NOT_SUPPORTED) {
            LOG_ERROR("wipe_user_flash_c: list_dir not supported");
            return r;
        }
        if (r != RESULT_SUCCESS) {
            LOG_ERROR("wipe_user_flash_c: failed '%s' ret=%d", k_dirs[i], (int)r);
            return r;
        }
    }

    LOG_WARN("wipe_user_flash_c: done");
    return RESULT_SUCCESS;
}

Result file_system_deinit(void)
{
    LOG_INFO("deinit");
    return RESULT_SUCCESS;
}
