/**
 * @file task_stats.h
 * @brief Common task statistics structure and functions for all modules
 */

#ifndef WEWARE_TASK_STATS_H
#define WEWARE_TASK_STATS_H

/*---------------------------------------------------------------
 * Standard Includes
 *--------------------------------------------------------------*/
#include <stdint.h>

/*---------------------------------------------------------------
 * Weware Platform Includes
 *--------------------------------------------------------------*/
#include "sdk_platform.h"
#include "common/types.h"
#include "module/module_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------
 * Type Definitions
 *--------------------------------------------------------------*/

/**
 * @brief Common task statistics structure
 * @note Used by all modules that have tasks
 */
typedef struct
{
    UINT32 task_stack_size;      /**< Total task stack size (bytes) */
    UINT32 task_stack_used;      /**< Current task stack usage (bytes) */
    UINT32 task_stack_peak;      /**< Peak task stack usage (bytes) */
    UINT32 task_stack_free;      /**< Free stack space (bytes) */
    UINT32 memory_allocated;     /**< Total memory allocated by module (bytes) */
    UINT32 last_sample_uptime_sec; /**< Internal rate-limit state for periodic sampling/logging */
} TaskStats;

#define TASK_STATS_PRINT_INTERVAL_SEC 10U

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/

/**
 * @brief Rate-limited update of task statistics; only public entry for modules.
 */
BOOL task_stats_update_periodic(sdk_task_ref_t task_ref,
                                const char *module_name,
                                ModuleId module_id,
                                TaskStats *stats,
                                UINT32 memory_allocated);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_TASK_STATS_H */
