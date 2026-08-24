/**
 ******************************************************************************
 * @file    sdk_system.h
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

#ifndef __SDK_SYSTEM_H__
#define __SDK_SYSTEM_H__

#include "sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Perform an immediate system reset. Does not return.
 */
void sdk_system_reset(void);

/**
 * @brief  Reboot the device (graceful). Does not return.
 */
void sdk_system_reboot(void);

/**
 * @brief  Power the device off. Does not return.
 */
void sdk_system_power_off(void);

/**
 * @brief  Get the code for the last reset/power-up reason.
 * @return SW_RESTART_REASON code (ASCII, e.g. 'N','C','R','A','E','D');
 *         0 if not available.
 */
UINT32 sdk_get_reset_reason(void);

/**
 * @brief  Get a human-readable string for a reset-reason code.
 * @param  reset_reason_code  the code from sdk_get_reset_reason().
 * @return static description string; one of:
 *           'N' -> "normal power-on"
 *           'C' -> "power-off charging"
 *           'R' -> "RD production"
 *           'A' -> "RTC alarm"
 *           'E' -> "error / watchdog reset"
 *           'D' -> "download mode"
 *           any other code -> "unknown"
 */
const char *sdk_get_reset_reason_string(UINT32 reset_reason_code);

#ifdef __cplusplus
}
#endif

#endif /* __SDK_SYSTEM_H__ */
