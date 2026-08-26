/**
 * @file command_config.h
 * @brief Command module configuration
 */

#ifndef WEWARE_COMMAND_CONFIG_H
#define WEWARE_COMMAND_CONFIG_H

#include "common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Command module configuration structure
 * @note Contains only authentication settings
 * @note Task and stats intervals are runtime variables
 */
typedef struct
{
    BOOL auth_enable;               /**< Enable authentication for protected commands (TRUE = auth required, FALSE = no auth) */
    char auth_password[32];         /**< Authentication password for protected commands */
    /** Relay digout ON (TRUE = GPIO LOW). Persisted; default FALSE (GPIO HIGH). */
    BOOL digout_on;
    /** Minimum CSQ (0–31) for A-GPS and OTA HTTPS; 0 = gate disabled. Default 20. */
    UINT8 min_csq_threshold;
    /**
     * When TRUE, defer OTA and A-GPS while vehicle motion is on.
     * Set via @c adoc-net-task in SET-COMMAND-CONFIG. Default TRUE.
     */
    BOOL adoc_net_task_flag;
} CommandConfig;

/**
 * @brief Get Command module default configuration
 * @param config Output Command configuration structure (must not be NULL)
 * @note Compatible with ModuleConfigGetDefaultsFn - takes void* parameter
 */
void command_config_get_defaults(void *config);

/**
 * @brief Set Command configuration parameters from comma-separated string
 * @param config_string Comma-separated string with values (with or without prefixes):
 *                      Format with prefixes: auth_en:<value>,pass:<value>
 *                      Format without prefixes: <value>,<value>
 *                      Partial updates allowed - only provided values will be updated
 *                      Examples:
 *                      - "TRUE" or "auth_en:TRUE" - sets auth_enable
 *                      - "TRUE,WEWARE2024" or "auth_en:TRUE,pass:WEWARE2024" - sets both
 * @note Gets the Command config from module_manager automatically
 * @note Supports both prefixed and non-prefixed formats for backward compatibility
 * @return RESULT_SUCCESS if valid and set, RESULT_INVALID_PARAM if invalid or module not initialized
 */
Result command_config_set(const char *config_string);

/**
 * @brief Get Command configuration parameters as comma-separated string
 * @param buffer Output buffer to store the formatted string (must not be NULL)
 * @param buffer_size Size of the output buffer
 * @return RESULT_SUCCESS if successful, RESULT_INVALID_PARAM if buffer is NULL or module not initialized,
 *         RESULT_ERROR if buffer is too small
 * @note Format: auth-en:<value>,pass:<value>,min-csq:<0-31>,adoc-net-task:<TRUE|FALSE>
 * @note Gets the Command config from module_manager automatically
 */
Result command_config_get_string(char *buffer, size_t buffer_size);

/**
 * @brief Validate Command configuration
 * @param config Command configuration to validate
 * @return RESULT_SUCCESS if valid, RESULT_INVALID_PARAM if invalid
 * @note Compatible with ModuleConfigValidateFn - takes const void* parameter
 */
Result command_config_validate(const void *config);

/**
 * @brief Get Command module configuration storage pointer
 * @return Pointer to Command configuration storage (for ModuleStatus->config_ptr)
 */
CommandConfig *command_config_get_storage(void);

/**
 * @brief TRUE when @c adoc_net_task_flag is set and vehicle motion is on (block OTA / A-GPS).
 */
BOOL command_config_adoc_net_task_blocked(void);

/**
 * @brief Command module configuration storage (defined in command_manager.c)
 * @note This is the actual storage for Command configuration
 */
extern CommandConfig g_command_config;

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_COMMAND_CONFIG_H */

