/**
 * @file module_config.h
 * @brief Module registry – list of all application modules and their configuration
 *
 * g_modules[] and g_module_count are defined in module_config.c and must match
 * the ModuleId enum order in module_manager.h.
 */

#ifndef WEWARE_MODULE_CONFIG_H
#define WEWARE_MODULE_CONFIG_H

#include "common/types.h"
#include "module/module_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Module registry (order must match ModuleId enum: LOG, UART, CMD, …) */
extern Module *g_modules[];

/** Number of modules in g_modules[] */
extern const UINT32 g_module_count;

/* Module init and monitoring configuration (used by module_manager) */
#define MODULE_INIT_RETRY_INTERVAL_MS (10000U)  /* 10 seconds between retry rounds */
#define MODULE_INIT_RETRY_COUNT       (6U)
#define MODULE_TASK_TIMEOUT_SEC      (60U)

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_MODULE_CONFIG_H */
