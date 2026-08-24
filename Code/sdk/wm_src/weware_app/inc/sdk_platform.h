/**
 * @file sdk_platform.h
 * @brief Walnut compat shim for the reference firmware's SDK platform layer.
 *
 * The reference (common-gateway) sources include "sdk_platform.h" for the
 * SDK_* convenience macros and platform types. On walnut those map 1:1 onto
 * the wm_src/sdk API (sdk_os.h, sdk_system.h, sdk_log.h), so this header lets
 * reference code port verbatim. Only what ported code actually uses is
 * defined here - extend as more reference modules come over.
 */

#ifndef WEWARE_WALNUT_SDK_PLATFORM_H
#define WEWARE_WALNUT_SDK_PLATFORM_H

#include "common/types.h"
#include "sdk_os.h"      /* sdk_get_ticks, sdk_task_sleep, sdk_mutex_*, sdk_memory_* */
#include "sdk_system.h"  /* sdk_system_reset, sdk_get_reset_reason(_string) */
#include "sdk_log.h"     /* sdk_debug_print */

#ifdef __cplusplus
extern "C" {
#endif

/* Tick / sleep (walnut sdk_get_ticks() returns milliseconds) */
#ifndef SDK_GET_TICKS
#define SDK_GET_TICKS()        sdk_get_ticks()
#endif
#ifndef SDK_TASK_SLEEP
#define SDK_TASK_SLEEP(ms)     sdk_task_sleep(ms)
#endif

/* Debug console (raw printf-style; no level, no newline appended) */
#ifndef SDK_DEBUG_PRINT
#define SDK_DEBUG_PRINT        sdk_debug_print
#endif

/* Generic success code (CG sdk_platform_platform.h; SdkResult success is 0
 * on walnut too) */
#ifndef SDK_SUCCESS
#define SDK_SUCCESS  0
#endif

/* SoC reset + reset reason */
#ifndef SDK_SYSTEM_RESET
#define SDK_SYSTEM_RESET()     sdk_system_reset()
#endif
#ifndef SDK_GET_RESET_REASON
#define SDK_GET_RESET_REASON() sdk_get_reset_reason()
#endif
#ifndef SDK_GET_RESET_REASON_STRING
#define SDK_GET_RESET_REASON_STRING(code) sdk_get_reset_reason_string(code)
#endif

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_WALNUT_SDK_PLATFORM_H */
