/**
 * @file post_boot_handler.c
 * @brief Post-reboot snapshot and boot reason classification.
 */

#include "system/reset/post_boot_handler.h"
#include "common/utils.h"
#include "sdk_platform.h"

#include <string.h>

#define LOG_TAG          "POSTBOOT"
#define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

static ResetHandlerLastSwResetInfo s_prior;
static UINT32                      s_soc_reset_reason;
static BOOL                        s_init_done;

static BOOL soc_reason_is_power_key(UINT32 code)
{
    /* Walnut (wm_sdk_system.h): reason codes are ASCII - 'N' = normal power on.
     * Reference SIMCOM code 2 / "POWER-KEY" kept for portability. */
    if (code == (UINT32)'N' || code == 2u)
        return TRUE;
    const char *s = SDK_GET_RESET_REASON_STRING(code);
    return (s != NULL && (strcmp(s, "POWER-KEY") == 0 || strcmp(s, "NORMAL") == 0));
}

static void log_boot_summary(void)
{
    const char *prior_mod = s_prior.module_name[0] ? s_prior.module_name : "?";

    LOG_ERROR("post-boot: SoC reason=%lu (%s)",
              (unsigned long)s_soc_reset_reason,
              post_boot_handler_get_soc_reset_reason_string());

    if (s_prior.valid) {
        LOG_ERROR("post-boot: prior %s reset module=%s total=%lu",
                  reset_type_name(s_prior.reset_type),
                  prior_mod,
                  (unsigned long)s_prior.total_resets);
    } else if (post_boot_handler_boot_is_power_on_reset()) {
        LOG_ERROR("post-boot: power-on reset");
    } else {
        LOG_ERROR("post-boot: unplanned SW reboot");
    }
}

Result post_boot_handler_init(void)
{
    if (s_init_done)
        return RESULT_SUCCESS;

    s_soc_reset_reason = SDK_GET_RESET_REASON();
    memset(&s_prior, 0, sizeof(s_prior));

    if (pre_boot_handler_load_prior_reset(&s_prior) != RESULT_SUCCESS)
        LOG_WARN("post-boot: pre-boot file load failed");

    log_boot_summary();
    s_init_done = TRUE;
    return RESULT_SUCCESS;
}

void post_boot_handler_get_prior_reset_snapshot(ResetHandlerLastSwResetInfo *out)
{
    if (out)
        memcpy(out, &s_prior, sizeof(*out));
}

BOOL post_boot_handler_load_reset_state(ResetType *reset_type, UINT32 *reset_time,
                                        char *module_name, UINT32 *total_resets_out)
{
    if (!reset_type)
        return FALSE;
    *reset_type = s_prior.reset_type;
    if (reset_time)
        *reset_time = s_prior.reset_time;
    if (module_name)
        utils_strncpy_safe(module_name, s_prior.module_name, sizeof(s_prior.module_name));
    if (total_resets_out)
        *total_resets_out = s_prior.total_resets;
    return s_prior.valid;
}

UINT32 post_boot_handler_get_soc_reset_reason(void)
{
    return s_soc_reset_reason;
}

const char *post_boot_handler_get_soc_reset_reason_string(void)
{
    return SDK_GET_RESET_REASON_STRING(s_soc_reset_reason);
}

BOOL post_boot_handler_boot_is_power_on_reset(void)
{
    return soc_reason_is_power_key(s_soc_reset_reason);
}

BOOL post_boot_handler_prior_reset_was_planned_sw(void)
{
    return s_prior.valid
        && (s_prior.reset_type == RESET_TYPE_SOFT || s_prior.reset_type == RESET_TYPE_HARD);
}

BOOL post_boot_handler_boot_is_unplanned_sw_reset(void)
{
    return !post_boot_handler_prior_reset_was_planned_sw()
        && !post_boot_handler_boot_is_power_on_reset();
}
