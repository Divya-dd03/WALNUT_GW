/**
 ******************************************************************************
 * @file    wm_sdk_system.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - SYSTEM control API (reset/reboot/power-off).
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __WM_SDK_SYSTEM_H__
#define __WM_SDK_SYSTEM_H__

#include "wm_sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Perform an immediate system reset. Does not return.
 */
void wm_sdk_system_reset(void);

/**
 * @brief  Reboot the device (graceful). Does not return.
 */
void wm_sdk_system_reboot(void);

/**
 * @brief  Power the device off. Does not return.
 */
void wm_sdk_system_power_off(void);

/**
 * @brief  Get the code for the last reset/power-up reason.
 * @return SW_RESTART_REASON code (ASCII, e.g. 'N','C','R','A','E','D');
 *         0 if not available.
 */
UINT32 wm_sdk_get_reset_reason(void);

/**
 * @brief  Get a human-readable string for a reset-reason code.
 * @param  reset_reason_code  the code from wm_sdk_get_reset_reason().
 * @return static description string; one of:
 *           'N' -> "normal power-on"
 *           'C' -> "power-off charging"
 *           'R' -> "RD production"
 *           'A' -> "RTC alarm"
 *           'E' -> "error / watchdog reset"
 *           'D' -> "download mode"
 *           any other code -> "unknown"
 */
const char *wm_sdk_get_reset_reason_string(UINT32 reset_reason_code);

#ifdef __cplusplus
}
#endif

#endif /* __WM_SDK_SYSTEM_H__ */
