/**
 ******************************************************************************
 * @file    wm_sdk_file.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - FILE SYSTEM API.
 *
 *          Paths are rooted at "C:/" (internal) and "D:/" (external) storage.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __WM_SDK_FILE_H__
#define __WM_SDK_FILE_H__

#include "wm_sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Open a file.
 * @param  path  file path.
 * @param  mode  open mode ("r","w","a","rb", ...).
 * @return file handle on success; NULL on failure.
 */
void *wm_sdk_file_open(const char *path, const char *mode);

/**
 * @brief  Close an open file handle.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_file_close(void *file);

/**
 * @brief  Read bytes from a file.
 * @param  file        file handle.
 * @param  buffer      [out] read buffer.
 * @param  size        bytes to read.
 * @param  bytes_read  [out] bytes actually read.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_file_read(void *file, void *buffer, UINT32 size, UINT32 *bytes_read);

/**
 * @brief  Write bytes to a file.
 * @param  file           file handle.
 * @param  buffer         data to write.
 * @param  size           number of bytes.
 * @param  bytes_written  [out] bytes actually written.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_file_write(void *file, const void *buffer, UINT32 size, UINT32 *bytes_written);

/**
 * @brief  Move the file read/write position.
 * @param  file    file handle.
 * @param  offset  offset value.
 * @param  whence  origin (SEEK_SET/CUR/END equivalent).
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_file_seek(void *file, INT32 offset, UINT32 whence);

/**
 * @brief  Flush buffered file data to persistent storage.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_file_sync(void *file);

/**
 * @brief  Get the size of an open file.
 * @param  file  file handle.
 * @param  size  [out] size in bytes.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_file_get_size(void *file, UINT32 *size);

/**
 * @brief  Check whether a path exists.
 * @return wm_SdkResult - 0 = exists; negative = not found/error.
 */
wm_SdkResult wm_sdk_file_exists(const char *path);

/**
 * @brief  Delete a file.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_file_delete(const char *path);

/**
 * @brief  Rename or move a file.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_file_rename(const char *old_path, const char *new_path);

/**
 * @brief  Create a directory.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_file_mkdir(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* __WM_SDK_FILE_H__ */
