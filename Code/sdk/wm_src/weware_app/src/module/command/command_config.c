/**
 * @file command_config.c
 * @brief Command module configuration with default values
 */

#include "module/command/command_config.h"
#include "module/module_manager.h"
#include "module/gps/gps_manager.h"
#include "config/config.h"
#include "common/utils.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <stddef.h>

/*---------------------------------------------------------------
 * Log Configuration
 *--------------------------------------------------------------*/
#define LOG_TAG "CMD_CFG"
#define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

#include "wm_sdk_log.h"

static const ConfigKeyMap s_cmd_cfg_keys[] = {
    { "auth-en",       0 },
    { "pass",          1 },
    { "min-csq",       2 },
    { "adoc-net-task", 3 },
};

BOOL command_config_adoc_net_task_blocked(void)
{
    CommandConfig *cfg = command_config_get_storage();

    if (!cfg || !cfg->adoc_net_task_flag)
        return FALSE;
    return gps_manager_is_motion_on();
}

Result command_config_validate(const void *config)
{
    const CommandConfig *cmd_cfg = (const CommandConfig *)config;
    
    if (!cmd_cfg)
    {
        return RESULT_INVALID_PARAM;
    }
    
    if (strlen(cmd_cfg->auth_password) == 0 || strlen(cmd_cfg->auth_password) >= sizeof(cmd_cfg->auth_password))
    {
        LOG_ERROR("Invalid auth_password length");
        return RESULT_INVALID_PARAM;
    }
    
    return RESULT_SUCCESS;
}

CommandConfig *command_config_get_storage(void)
{
    const ModuleConfig *module_config = module_manager_get_config(MODULE_ID_CMD);
    if (!module_config || !module_config->config_ptr)
    {
        LOG_ERROR("Command module config not found");
        return NULL;
    }
    return (CommandConfig *)module_config->config_ptr;
}


Result command_config_set(const char *config_string)
{
    if (!config_string || strlen(config_string) == 0)
    {
        LOG_ERROR("Invalid config_string: NULL or empty");
        return RESULT_INVALID_PARAM;
    }
    
    /* Get Command config from module_manager */
    const ModuleConfig *module_config = module_manager_get_config(MODULE_ID_CMD);
    if (!module_config || !module_config->config_ptr)
    {
        LOG_ERROR("Command module not initialized or config not available");
        return RESULT_INVALID_PARAM;
    }
    
    CommandConfig *config = (CommandConfig *)module_config->config_ptr;
    
    /* Make a copy of the input string for tokenization */
    char *str_copy = utils_strdup_for_tokenization(config_string);
    if (!str_copy)
    {
        LOG_ERROR("Failed to allocate memory for config string");
        return RESULT_ERROR;
    }
    
    /* Temporary variables for two-phase validation */
    BOOL temp_auth_enable = config->auth_enable;
    char temp_auth_password[32];
    utils_strncpy_safe(temp_auth_password, config->auth_password, sizeof(temp_auth_password));
    UINT8 temp_min_csq = config->min_csq_threshold;
    BOOL temp_adoc_net_task_flag = config->adoc_net_task_flag;
    
    char *token;
    char *saveptr = NULL;
    BOOL parsed_any = FALSE;
    Result result = RESULT_SUCCESS;
    
    /* Keyed tokens only; skip bare comma fragments (e.g. commas inside values). */
    token = utils_strtok_r(str_copy, ",", &saveptr);
    while (token != NULL && result == RESULT_SUCCESS)
    {
        int current_index;
        char key_buf[32];

        token = utils_trim_whitespace(token);
        if (!utils_config_parse_key(token, key_buf, sizeof(key_buf))) {
            token = utils_strtok_r(NULL, ",", &saveptr);
            continue;
        }
        current_index = utils_config_key_to_index(key_buf, s_cmd_cfg_keys,
                                                  sizeof(s_cmd_cfg_keys) / sizeof(s_cmd_cfg_keys[0]));
        if (current_index < 0) {
            LOG_ERROR("Unknown Command config key: %s", key_buf);
            result = RESULT_INVALID_PARAM;
            break;
        }

        parsed_any = TRUE;
        const char *value_str = utils_extract_config_value(token);

        switch (current_index)
        {
        case 0: /* auth_enable */
        {
            if (strcasecmp(value_str, "TRUE") == 0 || strcmp(value_str, "1") == 0)
            {
                temp_auth_enable = TRUE;
            }
            else if (strcasecmp(value_str, "FALSE") == 0 || strcmp(value_str, "0") == 0)
            {
                temp_auth_enable = FALSE;
            }
            else
            {
                LOG_ERROR("Invalid auth_enable: %s (must be TRUE/FALSE/1/0)", value_str);
                result = RESULT_INVALID_PARAM;
            }
            break;
        }
        
        case 1: /* auth_password */
        {
            size_t pass_len = strlen(value_str);
            if (pass_len == 0 || pass_len >= sizeof(temp_auth_password))
            {
                LOG_ERROR("Invalid auth_password length: %zu (must be 1-%zu)", 
                         pass_len, sizeof(temp_auth_password) - 1);
                result = RESULT_INVALID_PARAM;
            }
            else
            {
                utils_strncpy_safe(temp_auth_password, value_str, sizeof(temp_auth_password));
            }
            break;
        }

        case 2: /* min_csq_threshold */
        {
            char *end = NULL;
            long v = strtol(value_str, &end, 10);
            if (end == value_str || *end != '\0' || v < 0 || v > 31) {
                LOG_ERROR("Invalid min-csq: %s (must be 0-31, 0=disabled)", value_str);
                result = RESULT_INVALID_PARAM;
            } else {
                temp_min_csq = (UINT8)v;
            }
            break;
        }

        case 3: /* adoc_net_task_flag */
            if (strcasecmp(value_str, "TRUE") == 0 || strcmp(value_str, "1") == 0)
                temp_adoc_net_task_flag = TRUE;
            else if (strcasecmp(value_str, "FALSE") == 0 || strcmp(value_str, "0") == 0)
                temp_adoc_net_task_flag = FALSE;
            else {
                LOG_ERROR("Invalid adoc-net-task: %s (must be TRUE/FALSE/1/0)", value_str);
                result = RESULT_INVALID_PARAM;
            }
            break;
        
        default:
            /* Ignore extra parameters beyond the 4 expected */
            break;
        }

        token = utils_strtok_r(NULL, ",", &saveptr);
    }

    if (result == RESULT_SUCCESS && !parsed_any)
        result = RESULT_INVALID_PARAM;
    
    /* Only update config if all parameters were valid */
    if (result == RESULT_SUCCESS && parsed_any)
    {
        config->auth_enable = temp_auth_enable;
        utils_strncpy_safe(config->auth_password, temp_auth_password, sizeof(config->auth_password));
        config->min_csq_threshold = temp_min_csq;
        config->adoc_net_task_flag = temp_adoc_net_task_flag;
    }
    
    /* If config was successfully updated, save to file */
    if (result == RESULT_SUCCESS && parsed_any)
    {
        config_get_current();
        Result save_result = config_save_to_file(NULL);
        if (save_result == RESULT_SUCCESS)
        {
            LOG_DEBUG("Command configuration saved to file");
        }
        else
        {
            LOG_WARN("Failed to save Command configuration to file");
        }
    }
    
    free(str_copy);
    return result;
}

Result command_config_get_string(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0)
    {
        return RESULT_INVALID_PARAM;
    }
    
    /* Get Command config from module_manager */
    const ModuleConfig *module_config = module_manager_get_config(MODULE_ID_CMD);
    if (!module_config || !module_config->config_ptr)
    {
        LOG_ERROR("Command module not initialized or config not available");
        return RESULT_INVALID_PARAM;
    }
    
    const CommandConfig *config = (const CommandConfig *)module_config->config_ptr;
    
    /* Format config values as comma-separated string with prefixes */
    int len = snprintf(buffer, buffer_size, "auth-en:%s,pass:%s,min-csq:%u,adoc-net-task:%s",
                       config->auth_enable ? "TRUE" : "FALSE",
                       config->auth_password,
                       (unsigned)config->min_csq_threshold,
                       config->adoc_net_task_flag ? "TRUE" : "FALSE");
    
    /* Check if buffer was large enough */
    if (len < 0)
    {
        LOG_ERROR("Failed to format Command config string");
        return RESULT_ERROR;
    }
    
    if ((size_t)len >= buffer_size)
    {
        LOG_ERROR("Buffer too small for Command config string (needed %d, got %zu)", len, buffer_size);
        return RESULT_ERROR;
    }
    
    return RESULT_SUCCESS;
}

/*---------------------------------------------------------------
 * Defaults (bottom-up entry)
 *--------------------------------------------------------------*/

void command_config_get_defaults(void *config)
{
    CommandConfig *cmd_cfg = (CommandConfig *)config;

    if (!cmd_cfg)
        return;

    cmd_cfg->auth_enable = TRUE;
    utils_strncpy_safe(cmd_cfg->auth_password, "WEWARE2024", sizeof(cmd_cfg->auth_password));
    cmd_cfg->digout_on = FALSE;
    cmd_cfg->min_csq_threshold = 20;
    cmd_cfg->adoc_net_task_flag = TRUE;
}

