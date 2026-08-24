/**
 ******************************************************************************
 * @file    sdk_adc.h
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

#ifndef __SDK_ADC_H__
#define __SDK_ADC_H__

#include "sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Read the voltage on a generic ADC channel.
 * @param  channel     zero-based hardware ADC channel (0 = adc0, 1 = adc1, ...).
 * @param  voltage_mv  [out] calibrated source voltage in millivolts. Scaling
 *                     (reference, resolution, external divider) is set by the
 *                     SDK_ADC_* defines in sdk_adc.c.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_adc_read_voltage(INT32 channel, UINT16 *voltage_mv);

/**
 * @brief  Read the main battery (VBAT) voltage.
 * @param  vbat_mv  [out] VBAT voltage in millivolts.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_adc_read_vbat_voltage(UINT16 *vbat_mv);

#ifdef __cplusplus
}
#endif

#endif /* __SDK_ADC_H__ */
