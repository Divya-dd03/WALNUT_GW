/**
 ******************************************************************************
 * @file    wm_sdk_i2c.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - I2C API.
 *
 *          NOTE: the I2C bus is handled internally by the SDK library. Bus
 *          init and all read/write transfers happen inside lib_wmsrc.a; the
 *          application does not drive I2C directly. wm_sdk_i2c_hw_init() is a
 *          no-op kept for API compatibility; there is no public read/write
 *          transfer API.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __WM_SDK_I2C_H__
#define __WM_SDK_I2C_H__

#include "wm_sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Initialise a hardware I2C channel.
 * @param  channel  I2C channel id.
 * @param  speed    bus speed (standard 100k / fast 400k).
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_i2c_hw_init(UINT32 channel, wm_SdkI2cSpeed speed);

/**
 * @brief  Deinitialise an I2C channel.
 * @param  channel  I2C channel id.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_i2c_hw_deinit(UINT32 channel);

#ifdef __cplusplus
}
#endif

#endif /* __WM_SDK_I2C_H__ */
