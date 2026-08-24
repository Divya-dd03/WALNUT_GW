/**
 * @file file_system.h
 * @brief High-level file system helpers for weware (wrapper around SIMCom FS APIs)
 *
 * This module provides a small, safe API for common file system operations
 * that weware can use or expose via commands/SMS. It is intentionally
 * simpler than the interactive demo in `sc_demo/V1/src/demo_file_system.c`,
 * but it follows the same underlying behaviour.
 */

#ifndef WEWARE_FILE_SYSTEM_H
#define WEWARE_FILE_SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/types.h"

/**
 * @brief File info type for directory listings (must match SDK @c SdkFileDirEntry layout)
 */
typedef struct
{
    char  name[260]; /**< File or directory name */
    long  size;      /**< Size in bytes (for files) */
    int   type;      /**< Type from platform FS (e.g. 0=file, 1=dir) */
} FileInfo;

/**
 * @brief Initialize file system module (reserved for future use)
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result file_system_init(void);

/**
 * @brief Write data to a file (create/overwrite)
 *
 * @param path      Full file path (e.g. "D:/test.txt")
 * @param data      Pointer to data buffer
 * @param data_len  Length of data in bytes
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result file_system_write_file(const char *path, const char *data, UINT32 data_len);

/**
 * @brief Read file contents into provided buffer
 *
 * @param path          Full file path
 * @param buffer        Output buffer
 * @param buffer_size   Buffer size in bytes
 * @param out_read_len  Optional output for actual bytes read (can be NULL)
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result file_system_read_file(const char *path,
                             char *buffer,
                             UINT32 buffer_size,
                             UINT32 *out_read_len);

/**
 * @brief Get file size in bytes (opens, gets size, closes)
 * @param path      Full file path
 * @param out_size  Output: file size in bytes
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result file_system_get_file_size(const char *path, UINT32 *out_size);

/**
 * @brief Read a chunk of file at given offset (for large files; does not load entire file)
 * @param path          Full file path
 * @param buffer        Output buffer
 * @param buffer_size   Buffer size in bytes
 * @param offset        Byte offset from start of file
 * @param out_read_len  Optional output for actual bytes read (can be NULL)
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result file_system_read_at_offset(const char *path,
                                  char *buffer,
                                  UINT32 buffer_size,
                                  UINT32 offset,
                                  UINT32 *out_read_len);

/**
 * @brief Delete a file
 *
 * @param path Full file path
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result file_system_delete(const char *path);

/**
 * @brief Rename/move a file
 *
 * @param old_path Old file path
 * @param new_path New file path
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result file_system_rename(const char *old_path, const char *new_path);

/**
 * @brief Create directory
 *
 * @param path Directory path
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result file_system_mkdir(const char *path);

/**
 * @brief Remove directory or file (thin wrapper over sAPI_remove)
 *
 * @param path Path to remove
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result file_system_remove(const char *path);

/**
 * @brief Get disk size information
 *
 * @param root_path   Root path, e.g. "C:/" or "D:/"
 * @param total_size  Output: total size in bytes
 * @param free_size   Output: free size in bytes
 * @param used_size   Output: used size in bytes
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result file_system_get_disk_info(const char *root_path,
                                 INT64 *total_size,
                                 INT64 *free_size,
                                 INT64 *used_size);

/**
 * @brief List directory contents (simple iterator)
 *
 * This function opens the directory, reads entries into @p entries up to
 * @p max_entries, then closes the directory.
 *
 * @param path         Directory path
 * @param entries      Output array
 * @param max_entries  Size of @p entries array
 * @param out_count    Output: number of valid entries filled
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result file_system_list_dir(const char *path, FileInfo *entries, UINT32 max_entries, UINT32 *out_count);

/**
 * @brief Delete all regular files in one directory (non-recursive; subdirs skipped).
 *
 * @param dir_path      Path with trailing slash (e.g. @c C:/queue/). Must be under @c C:/.
 * @param out_deleted   Optional successful delete count; may be NULL.
 * @param out_failed    Optional failed delete count; may be NULL.
 */
Result file_system_delete_all_files_in_directory(const char *dir_path,
                                                 UINT32 *out_deleted,
                                                 UINT32 *out_failed);

/**
 * @brief Factory reset file cleanup: delete files in @c config, @c queue, @c fota, @c preboot under @c C:/.
 *
 * Does not recurse into subdirectories; does not remove the folders themselves. For use before reboot.
 */
Result file_system_wipe_user_flash_c(void);

/**
 * @brief Deinitialize file system module (reserved for future use)
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result file_system_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_FILE_SYSTEM_H */


