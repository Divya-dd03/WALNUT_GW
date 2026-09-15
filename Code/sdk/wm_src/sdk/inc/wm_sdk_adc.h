/**
 ******************************************************************************
 * @file    wm_sdk_adc.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - ADC API.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __WM_SDK_ADC_H__
#define __WM_SDK_ADC_H__

#include "wm_sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Read the voltage on a generic ADC channel.
 * @param  channel     zero-based hardware ADC channel (0 = adc0, 1 = adc1, ...).
 * @param  voltage_mv  [out] calibrated source voltage in millivolts. Scaling
 *                     (reference, resolution, external divider) is set by the
 *                     WM_SDK_ADC_* defines in wm_sdk_adc.c.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_adc_read_voltage(INT32 channel, UINT16 *voltage_mv);

/**
 * @brief  Read the main battery (VBAT) voltage.
 * @param  vbat_mv  [out] VBAT voltage in millivolts.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_adc_read_vbat_voltage(UINT16 *vbat_mv);

#ifdef __cplusplus
}
#endif

#endif /* __WM_SDK_ADC_H__ */
