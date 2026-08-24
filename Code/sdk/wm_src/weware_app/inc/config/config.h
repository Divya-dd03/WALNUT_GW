/**
 * @file config.h
 * @brief Configuration management system for Weware platform
 * 
 * This module provides centralized configuration management for module-specific
 * configurations. It supports loading and saving module configurations from/to
 * persistent storage.
 * 
 * @note Module configs are stored via ModuleStatus->config_ptr
 * @note Module configs are managed automatically through ModuleStatus array
 */

#ifndef WEWARE_CONFIG_H
#define WEWARE_CONFIG_H

/*---------------------------------------------------------------
 * Standard Includes
 *--------------------------------------------------------------*/
#include <stdint.h>

/*---------------------------------------------------------------
 * Weware Platform Includes
 *--------------------------------------------------------------*/
#include "common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------
 * Configuration Management Functions
 *--------------------------------------------------------------*/

/**
 * @brief Initialize module configurations with default values
 * @note This function only initializes module configs
 */
void config_init_defaults(void);

/**
 * @brief Load module configurations from file and merge with defaults
 * @param config_file_path Path to configuration file (NULL for default path)
 * @return RESULT_SUCCESS on success (even if file doesn't exist - uses defaults)
 * @note If file doesn't exist, creates a new file with default values
 * @note Automatically handles version mismatches and invalid entries
 */
Result config_load_from_file(const char *config_file_path);

/**
 * @brief Save module configurations to file
 * @param config_file_path Path to configuration file (NULL for default path)
 * @return RESULT_SUCCESS on success, error code on failure
 * @note Only saves modules that have config_stored flag set to TRUE
 * @note Validates all module configs before saving
 */
Result config_save_to_file(const char *config_file_path);

/**
 * @brief Validate module configurations
 * @return RESULT_SUCCESS if valid, RESULT_INVALID_PARAM if invalid
 * @note Validates all module configs
 */
Result config_validate(void);

/**
 * @brief Initialize configuration system (loads from file if needed)
 * @note Automatically loads from file on first access if not already initialized
 * @note This function triggers initialization - no return value needed
 */
void config_get_current(void);

/*---------------------------------------------------------------
 * Module Configuration Access Functions
 *--------------------------------------------------------------*/

/**
 * @brief Get module configuration by module name (read-only)
 * @param module_name Name of the module (e.g., "GPS Manager", "Network Manager")
 * @return Pointer to module configuration, or NULL if not found
 * @note Cast the result to the appropriate config type (e.g., (const GpsConfig *))
 * @note Returns NULL if module doesn't exist or doesn't have config
 */
const void *config_get_module_config(const char *module_name);

/**
 * @brief Get module configuration pointer for modification (mutable)
 * @param module_name Name of the module (e.g., "GPS Manager", "Network Manager")
 * @return Pointer to module configuration, or NULL if not found
 * @note Cast the result to the appropriate config type (e.g., (GpsConfig *))
 * @note Use this function when you need to modify the configuration
 * @note Returns NULL if module doesn't exist or doesn't have config
 */
void *config_get_module_config_mutable(const char *module_name);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_CONFIG_H */
