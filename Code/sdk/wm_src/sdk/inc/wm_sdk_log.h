/**
 ******************************************************************************
 * @file    wm_sdk_log.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - LOG / debug-print API.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __WM_SDK_LOG_H__
#define __WM_SDK_LOG_H__

#include "wm_sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Initialise the debug UART used for log output.
 * @param  port    UART port id.
 * @param  config  UART configuration (may be NULL to use platform defaults).
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_log_init_uart(UINT32 port, const wm_SdkUartConfig *config);

/**
 * @brief  Raw debug print (printf-style), no level/tag.
 */
void wm_sdk_debug_print(const char *format, ...);

/**
 * @brief  Emit an INFO-level log line.
 */
void wm_sdk_log_info(const char *format, ...);

/**
 * @brief  Emit a WARNING-level log line.
 */
void wm_sdk_log_warning(const char *format, ...);

/**
 * @brief  Emit an ERROR-level log line.
 */
void wm_sdk_log_error(const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif /* __WM_SDK_LOG_H__ */
