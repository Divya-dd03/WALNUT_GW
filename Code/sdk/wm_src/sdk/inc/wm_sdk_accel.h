/**
 ******************************************************************************
 * @file    wm_sdk_accel.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - Accelerometer API.
 *
 *          The accelerometer is initialised at boot (100 Hz, X/Y/Z enabled,
 *          +/-2 g), so it can be read straight away.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __WM_SDK_ACCEL_H__
#define __WM_SDK_ACCEL_H__

#include "wm_sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Re-initialise the accelerometer to its boot defaults.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_NOT_SUPPORTED when the board
 *                     has no accelerometer; negative if the sensor does not
 *                     respond.
 */
wm_SdkResult wm_sdk_accel_init(void);

/**
 * @brief  Read the current acceleration on all three axes.
 * @param  data  [out] acceleration per axis in mg. Scaled to the configured
 *               full-scale range.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_NOT_SUPPORTED when the board
 *                     has no accelerometer; negative on failure.
 */
wm_SdkResult wm_sdk_accel_read_xyz(wm_SdkAccelData *data);

/**
 * @brief  Read one accelerometer register.
 * @param  reg  register address.
 * @param  val  [out] register value.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_accel_read_reg(UINT8 reg, UINT8 *val);

/**
 * @brief  Write one accelerometer register (e.g. range, data rate, interrupts).
 * @param  reg  register address.
 * @param  val  value to write.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_accel_write_reg(UINT8 reg, UINT8 val);

/**
 * @brief  Interrupt callback.
 *
 *         Runs on a platform task, not in interrupt context, so it may call
 *         wm_sdk_accel_* functions. Keep it short and do not block.
 *
 * @param  line  pin that fired.
 * @param  axes  WM_SDK_ACCEL_AXIS_* bits of the axes that triggered it.
 */
typedef void (*wm_sdk_accel_int_cb_t)(wm_SdkAccelIntLine line, UINT8 axes);

/**
 * @brief  Register the interrupt callback, shared by both pins.
 * @param  cb  callback, or NULL to stop delivering events.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_NOT_SUPPORTED when the board
 *                     has no accelerometer.
 */
wm_SdkResult wm_sdk_accel_set_int_callback(wm_sdk_accel_int_cb_t cb);

/**
 * @brief  Raise an interrupt on @p line when the board moves.
 *
 *         Gravity is filtered out, so the board's orientation does not trigger
 *         it. Each pin is configured independently; calling again replaces the
 *         pin's settings. Thresholds and durations follow the range and data
 *         rate in effect when this is called.
 *
 * @param  line  pin to drive.
 * @param  cfg   threshold, duration and axes to watch.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a bad pin,
 *                     no axes, or a threshold/duration out of the sensor's
 *                     range; WM_SDK_RESULT_NOT_SUPPORTED when the board has no
 *                     accelerometer; negative on failure.
 */
wm_SdkResult wm_sdk_accel_motion_int_enable(wm_SdkAccelIntLine line,
                                            const wm_SdkAccelMotionConfig *cfg);

/**
 * @brief  Stop raising interrupts on @p line.
 * @param  line  pin to release.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_accel_motion_int_disable(wm_SdkAccelIntLine line);

#ifdef __cplusplus
}
#endif

#endif /* __WM_SDK_ACCEL_H__ */
