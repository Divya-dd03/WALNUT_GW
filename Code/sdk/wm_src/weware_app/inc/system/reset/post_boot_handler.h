/**
 * @file post_boot_handler.h
 * @brief Post-reboot: prior reset snapshot and SoC boot reason (power vs planned SW vs unplanned).
 *
 * Call @c post_boot_handler_init() once after @c file_system_init().
 */

#ifndef WEWARE_POST_BOOT_HANDLER_H
#define WEWARE_POST_BOOT_HANDLER_H

#include "common/types.h"
#include "system/reset/pre_boot_handler.h"

#ifdef __cplusplus
extern "C" {
#endif

Result post_boot_handler_init(void);

void post_boot_handler_get_prior_reset_snapshot(ResetHandlerLastSwResetInfo *out);

BOOL post_boot_handler_load_reset_state(ResetType *reset_type, UINT32 *reset_time, char *module_name,
                                        UINT32 *total_resets_out);

UINT32 post_boot_handler_get_soc_reset_reason(void);
const char *post_boot_handler_get_soc_reset_reason_string(void);

BOOL post_boot_handler_boot_is_power_on_reset(void);

BOOL post_boot_handler_prior_reset_was_planned_sw(void);
BOOL post_boot_handler_boot_is_unplanned_sw_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_POST_BOOT_HANDLER_H */
