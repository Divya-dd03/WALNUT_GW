/**
 * @file log_manager.h
 * @brief Log output sink - walnut port of the reference module/log/log_manager.h.
 *
 * Walnut adaptation: the reference async lock-free ring + logger task is not
 * ported yet; log_printf() formats and forwards synchronously to
 * wm_sdk_debug_print (same console the rest of weware_app logs to).
 * The API is kept identical so the module registry and system_manager port
 * verbatim. TODO(log): port the async ring backend if logging volume grows.
 */

#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include "common/types.h"

typedef enum
{
    LOG_OUTPUT_DEBUG = 0,
    LOG_OUTPUT_UART2 = 1
} LogOutput;

/**
 * @brief Module entry: initialize logger from @ref g_log_config (see log_config.h).
 */
Result log_module_init(void);

/**
 * @brief Initialize runtime log sink.
 * @note Walnut: both outputs currently map to wm_sdk_debug_print (USB VCOM).
 */
Result logger_init(LogOutput output);

/**
 * @brief Total number of log lines dropped since boot.
 * @note Walnut synchronous backend never drops; always 0 until the ring is ported.
 */
UINT32 logger_get_dropped_log_total(void);

/**
 * @brief Disable runtime log output; log_printf() returns early while deinitialized.
 */
void logger_deinit(void);

#endif /* LOG_MANAGER_H */
