/**
 * @file pre_boot_handler.h
 * @brief Pre-boot on-disk record (@c PRE_BOOT_INFO_FILE_PATH).
 */

#ifndef WEWARE_PRE_BOOT_HANDLER_H
#define WEWARE_PRE_BOOT_HANDLER_H

#include "common/types.h"
#include "system/storage/flash_paths.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RESET_TYPE_NONE = 0,
    RESET_TYPE_CFUN = 1,
    RESET_TYPE_SOFT = 2,
    RESET_TYPE_HARD = 3,
} ResetType;

typedef struct {
    ResetType reset_type;
    UINT32 reset_time;
    UINT32 total_resets;
    char module_name[33];
    BOOL valid;
} ResetHandlerLastSwResetInfo;

/** Human-readable reset type for logs (returns "?" if out of range). */
const char *reset_type_name(ResetType type);

/* Before SoC reset (reset_handler) */
Result pre_boot_handler_save_before_soc_reset(ResetType reset_type, UINT32 reset_time,
                                              const char *module_name);

/* TCP queue resume offset (queue_manager) */
Result pre_boot_handler_save_tcp_send_queue_offset(UINT32 offset);
BOOL pre_boot_handler_take_tcp_send_queue_offset(UINT32 *out_offset);
Result pre_boot_handler_clear_tcp_send_queue_offset(void);

/* At boot: read file, fill @a prior, clear consumed reset slot on disk (post_boot_handler_init) */
Result pre_boot_handler_load_prior_reset(ResetHandlerLastSwResetInfo *prior);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_PRE_BOOT_HANDLER_H */
