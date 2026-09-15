/**
 * @file log_config.c
 * @brief Default log module configuration - reference port.
 *
 * Walnut: LOG_OUTPUT_DEBUG (wm_sdk_debug_print / USB VCOM) is the default sink;
 * the reference default LOG_OUTPUT_UART2 has no walnut backend yet.
 */

#include "module/log/log_config.h"

LogConfig g_log_config = {
    .output = LOG_OUTPUT_DEBUG,
};

void log_config_get_defaults(void *config)
{
    LogConfig *c = (LogConfig *)config;
    if (!c) {
        return;
    }
    c->output = LOG_OUTPUT_DEBUG;
}

Result log_config_validate(const void *config)
{
    const LogConfig *c = (const LogConfig *)config;
    if (!c) {
        return RESULT_INVALID_PARAM;
    }
    if (c->output > LOG_OUTPUT_UART2) {
        return RESULT_INVALID_PARAM;
    }
    return RESULT_SUCCESS;
}
