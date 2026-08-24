/**
 * @file reset_handler.h
 * @brief Deferred soft/hard SoC reset — single application path with pre-boot persistence.
 *
 * Flow:
 * - Something broadcasts @c EVENT_RESET_SOFT or @c EVENT_RESET_HARD (or calls
 *   reset_handler_request_reset(), which broadcasts the same).
 * - The handler schedules a deferred reset; the main loop must call
 *   reset_handler_process_deferred_reset() periodically.
 * - After a short monotonic-ms defer (see implementation), the module persists reset metadata,
 *   last GPS, and reboot-safe module data, waits a stabilize period (soft vs hard),
 *   then @c SDK_SYSTEM_RESET().
 *
 * Timing in the .c file uses @c utils_monotonic_ms_now() / @c utils_monotonic_ms_elapsed()
 * and @c utils_sleep_ms() — not raw RTOS ticks in reset_handler.
 *
 * While a reset is pending or executing, further soft/hard reset events are ignored (first wins).
 *
 * @see post_boot_handler.h — @c post_boot_handler_init() must run earlier the same boot.
 */

#ifndef WEWARE_RESET_HANDLER_H
#define WEWARE_RESET_HANDLER_H

#include "common/types.h"
#include "system/reset/post_boot_handler.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Snapshot of the reset recorded in the pre-boot file before the last reboot.
 * Copy of prior reset snapshot; filled in @c reset_handler_init(). Inspect @c valid before use.
 */
extern ResetHandlerLastSwResetInfo g_reset_handler_last_sw_reset;

/**
 * @brief Load prior-boot snapshot into g_reset_handler_last_sw_reset, log, register for EVENT_RESET_SOFT/HARD.
 * @return RESULT_SUCCESS after registration; error code if event_manager_register fails (see .c).
 * @note Requires post_boot_handler_init() earlier the same boot.
 */
Result reset_handler_init(void);

/**
 * @brief Request a deferred SoC reset by broadcasting the matching event (same path as a direct broadcast).
 * @param reset_type Must be RESET_TYPE_SOFT or RESET_TYPE_HARD (see pre_boot_handler.h).
 * @return Broadcast result; scheduling still happens only when the event handler runs and process_deferred_reset fires.
 */
Result reset_handler_request_reset(int reset_type);

/**
 * @brief If a reset was scheduled and the defer interval has elapsed, run persist → SoC reset.
 * @return FALSE if nothing to do or still waiting; TRUE only if SDK_SYSTEM_RESET() returned (unexpected),
 *         after which the implementation attempted module_manager_init() recovery.
 * @note Call from the main loop (e.g. system_manager); not re-entrant with overlapping reset requests.
 */
BOOL reset_handler_process_deferred_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_RESET_HANDLER_H */
