/**
 ******************************************************************************
 * @file    wm_sdk_storage.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - STORAGE (NVM) API.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __WM_SDK_STORAGE_H__
#define __WM_SDK_STORAGE_H__

#include "wm_sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Read raw bytes from an NVM flash partition.
 * @param  partition  partition id.
 * @param  offset     byte offset within the partition.
 * @param  buffer     [out] read buffer.
 * @param  size       bytes to read.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_storage_read_nvm(UINT32 partition, UINT32 offset, void *buffer, UINT32 size);

/**
 * @brief  Write raw bytes to an NVM flash partition.
 * @param  partition  partition id.
 * @param  offset     byte offset within the partition.
 * @param  buffer     data to write.
 * @param  size       number of bytes.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_storage_write_nvm(UINT32 partition, UINT32 offset, const void *buffer, UINT32 size);

/**
 * @brief  Read a stored credential blob (root CA / client certificate / client
 *         key) into a caller buffer. The blob is returned NUL-terminated.
 *
 * @param  cred    which credential to read (wm_SdkStorageCredential).
 * @param  buffer  [out] destination for the NUL-terminated blob.
 * @param  size    size of @p buffer in bytes; must hold the stored blob plus
 *                 its terminating NUL (up to WM_SDK_STORAGE_CRED_MAX_SIZE).
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a bad argument or
 *         when @p buffer is too small for the stored blob; negative on failure.
 */
wm_SdkResult wm_sdk_storage_cred_read(wm_SdkStorageCredential cred, char *buffer, UINT32 size);

/**
 * @brief  Write a credential blob (root CA / client certificate / client key)
 *         to persistent storage.
 *
 * @param  cred  which credential to write (wm_SdkStorageCredential).
 * @param  data  NUL-terminated string to store; its length including the NUL
 *               must not exceed WM_SDK_STORAGE_CRED_MAX_SIZE.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a bad argument.
 */
wm_SdkResult wm_sdk_storage_cred_write(wm_SdkStorageCredential cred, const char *data);

#ifdef __cplusplus
}
#endif

#endif /* __WM_SDK_STORAGE_H__ */
