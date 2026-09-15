/**
 ******************************************************************************
 * @file    wm_sdk_device.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - DEVICE identity API.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __WM_SDK_DEVICE_H__
#define __WM_SDK_DEVICE_H__

#include "wm_sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Read the device IMEI (15-digit modem identity).
 * @param  imei       [out] buffer for the IMEI string.
 * @param  imei_size  size of the buffer in bytes (>=16 recommended).
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_device_get_imei(char *imei, UINT32 imei_size);

#ifdef __cplusplus
}
#endif

#endif /* __WM_SDK_DEVICE_H__ */
