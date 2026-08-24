/**
 * @file log_config.h
 * @brief Log module runtime configuration (sink selection) - reference port.
 */

#ifndef WEWARE_LOG_CONFIG_H
#define WEWARE_LOG_CONFIG_H

#include "common/types.h"
#include "module/log/log_manager.h"

typedef struct
{
    LogOutput output; /**< LOG_OUTPUT_DEBUG or LOG_OUTPUT_UART2 */
} LogConfig;

extern LogConfig g_log_config;

void log_config_get_defaults(void *config);
Result log_config_validate(const void *config);

#endif /* WEWARE_LOG_CONFIG_H */
