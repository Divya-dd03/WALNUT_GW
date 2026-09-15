/**
 ******************************************************************************
 * @file    wm_sdk_gpio.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - GPIO API.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __WM_SDK_GPIO_H__
#define __WM_SDK_GPIO_H__

#include "wm_sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Set a GPIO pin as input or output.
 * @param  pin        pin number.
 * @param  direction  0=input, 1=output.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_gpio_set_direction(UINT32 pin, UINT32 direction);

/**
 * @brief  Read a GPIO pin's configured direction.
 * @param  pin        pin number.
 * @param  direction  [out] 0=input, 1=output.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_gpio_get_direction(UINT32 pin, UINT32 *direction);

/**
 * @brief  Set the output level of a GPIO pin.
 * @param  pin    pin number.
 * @param  level  0=low, 1=high.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_gpio_set_level(UINT32 pin, UINT32 level);

/**
 * @brief  Read the current level of a GPIO pin.
 * @param  pin    pin number.
 * @param  level  [out] 0=low, 1=high.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_gpio_get_level(UINT32 pin, UINT32 *level);

#ifdef __cplusplus
}
#endif

#endif /* __WM_SDK_GPIO_H__ */
