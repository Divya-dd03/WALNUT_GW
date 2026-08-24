/**
 * @file task_stats.c
 * @brief Common task statistics implementation
 */

/*---------------------------------------------------------------
 * Weware Platform Includes
 *--------------------------------------------------------------*/
#include "common/task_stats.h"

/*---------------------------------------------------------------
 * Log (before any #if on LOG_MODULE_LEVEL; log.h defines level constants)
 *--------------------------------------------------------------*/
#define LOG_TAG "TASK_STATS"
#define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

#if LOG_MODULE_LEVEL >= LOG_LEVEL_DEBUG
#include "common/utils.h"
#endif

/*---------------------------------------------------------------
 * SDK Platform Abstraction Layer
 *--------------------------------------------------------------*/
#include "sdk_platform.h"
#if LOG_MODULE_LEVEL >= LOG_LEVEL_DEBUG
#include "functionality/sdk_functionality_os.h"
#endif

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/

#if LOG_MODULE_LEVEL >= LOG_LEVEL_DEBUG
static BOOL task_stats_update(sdk_task_ref_t task_ref,
                       const char *module_name,
                       TaskStats *stats,
                       UINT32 memory_allocated)
{
    UINT32 stack_size = 0;
    UINT32 stack_used = 0;
    UINT32 stack_peak = 0;

    if (!task_ref || !stats) {
        LOG_ERROR("Update failed: invalid parameters");
        return FALSE;
    }

    if (sdk_task_get_stack_info(task_ref, &stack_size, &stack_used, &stack_peak) != SDK_RESULT_SUCCESS) {
        LOG_WARN("Update failed: task_get_stack_info returned error");
        stats->task_stack_size = 0;
        stats->task_stack_used = 0;
        stats->task_stack_peak = 0;
        stats->task_stack_free = 0;
        stats->memory_allocated = memory_allocated;
        return FALSE;
    }

    stats->task_stack_size = stack_size;
    stats->task_stack_used = stack_used;
    stats->task_stack_peak = stack_peak;
    stats->task_stack_free = (stack_size >= stack_peak) ? (stack_size - stack_peak) : 0;
    stats->memory_allocated = memory_allocated;

    if (module_name) {
        LOG_DEBUG("[%s] Stack: %u/%u (peak: %u, free: %u), Memory: %u bytes",
                  module_name,
                  stats->task_stack_used,
                  stats->task_stack_size,
                  stats->task_stack_peak,
                  stats->task_stack_free,
                  stats->memory_allocated);
    } else {
        LOG_DEBUG("Updated: stack_size=%u, stack_used=%u, stack_peak=%u, stack_free=%u",
                  stack_size, stack_used, stack_peak, stats->task_stack_free);
    }

    return TRUE;
}
#endif

BOOL task_stats_update_periodic(sdk_task_ref_t task_ref,
                                const char *module_name,
                                ModuleId module_id,
                                TaskStats *stats,
                                UINT32 memory_allocated)
{
#if LOG_MODULE_LEVEL < LOG_LEVEL_DEBUG
    (void)task_ref;
    (void)module_name;
    (void)module_id;

    if (stats) {
        stats->task_stack_size = 0;
        stats->task_stack_used = 0;
        stats->task_stack_peak = 0;
        stats->task_stack_free = 0;
        stats->memory_allocated = memory_allocated;
        stats->last_sample_uptime_sec = 0;
    }

    return TRUE;
#else
    UINT32 uptime_sec = 0;

    if (!stats) {
        LOG_ERROR("Periodic update failed: invalid stats");
        return FALSE;
    }

    uptime_sec = utils_get_uptime_seconds();
    if ((stats->task_stack_size != 0U) &&
        ((uptime_sec - stats->last_sample_uptime_sec) < TASK_STATS_PRINT_INTERVAL_SEC)) {
        stats->memory_allocated = memory_allocated;
        return TRUE;
    }

    (void)module_id;

    if (!task_stats_update(task_ref, module_name, stats, memory_allocated)) {
        return FALSE;
    }

    stats->last_sample_uptime_sec = uptime_sec;

    return TRUE;
#endif
}
