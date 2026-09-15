/**
 ******************************************************************************
 * @file    wm_sdk_led.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - LED API.
 *
 *          Drives the board's three indicator LEDs - red, green and blue. All
 *          three channels are brought up at boot by wm_system_init(), so there
 *          is no init or deinit entry point - the first wm_sdk_led_* call works
 *          straight away.
 *
 *          The LEDs to light are given as a bitmask of channel bits
 *          (WM_SDK_LED_CH_*). Each channel drives its own LED independently at
 *          the platform's standard indicator intensity, so OR-ing bits
 *          together lights several LEDs at once.
 *
 *          Blinking runs on a timer inside the platform library, so the call
 *          returns immediately and the pattern keeps running until its cycle
 *          count elapses or another wm_sdk_led_set_state() call replaces it. Only
 *          the two rates named by wm_SdkLedBlinkRate are achievable in hardware.
 *
 *          NOTE: the call blocks briefly inside the platform library while the
 *          new channel states are applied. Do not call it from an interrupt
 *          handler or from a latency-critical callback.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __WM_SDK_LED_H__
#define __WM_SDK_LED_H__

#include "wm_sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Light the indicator LEDs named by @p channels, optionally blinking
 *         them. Any LED not named is turned off.
 *
 *         Each call fully replaces the previous state, so WM_SDK_LED_BLINK_NONE
 *         cancels a blink already in progress and WM_SDK_LED_CH_NONE turns all
 *         three LEDs off.
 *
 * @param  channels  bitmask of the LEDs to light: any OR-combination of
 *                   WM_SDK_LED_CH_RED / _GREEN / _BLUE, or WM_SDK_LED_CH_NONE to
 *                   turn them all off.
 * @param  rate      blink cadence, or WM_SDK_LED_BLINK_NONE for a steady light.
 *                   Every lit LED blinks together, in phase.
 * @param  count     on/off cycles, 1..WM_SDK_LED_BLINK_COUNT_MAX, or
 *                   WM_SDK_LED_BLINK_FOREVER to blink until the next call.
 *                   Ignored when @p rate is WM_SDK_LED_BLINK_NONE.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a channel bit
 *                     outside WM_SDK_LED_CH_ALL, a rate outside wm_SdkLedBlinkRate,
 *                     or a count above WM_SDK_LED_BLINK_COUNT_MAX;
 *                     WM_SDK_RESULT_NOT_SUPPORTED when the board has no
 *                     indicator LEDs fitted.
 */
wm_SdkResult wm_sdk_led_set_state(UINT32 channels, wm_SdkLedBlinkRate rate, UINT8 count);

#ifdef __cplusplus
}
#endif

#endif /* __WM_SDK_LED_H__ */
