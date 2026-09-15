/**
 * @file network_config.c
 * @brief Network module configuration with default values - walnut port of
 *        the reference firmware's module/network/network_config.c (logic
 *        verbatim; reference LOG macros mapped onto wm_sdk_log_*).
 */

/*===============================================================
 * Includes
 *==============================================================*/
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

#include "wm_sdk_log.h"

#include "module/network/network_config.h"
#include "module/module_manager.h"
#include "common/utils.h"
#include "config/config.h"

static const ConfigKeyMap s_net_cfg_keys[] = {
    { "apn",  0 },
    { "user", 1 },
    { "pass", 2 },
    { "cid",  3 },
    { "auto", 4 },
};

/*===============================================================
 * Configuration Functions
 *==============================================================*/

void network_config_get_defaults(void *config)
{
    NetworkConfig *net_config = (NetworkConfig *)config;

    if (!net_config)
        return;

    /* APN Settings */
    utils_strncpy_safe(net_config->apn, NETWORK_DEFAULT_APN, sizeof(net_config->apn));
    utils_strncpy_safe(net_config->username, NETWORK_DEFAULT_USERNAME, sizeof(net_config->username));
    utils_strncpy_safe(net_config->password, NETWORK_DEFAULT_PASSWORD, sizeof(net_config->password));
    net_config->cid = 1;
    net_config->auto_connect = TRUE;

    wm_sdk_debug_print("Network default config initialized\r\n");
}

Result network_config_validate(const void *config)
{
    const NetworkConfig *net_config = (const NetworkConfig *)config;

    if (!net_config) {
        wm_sdk_log_error("NET CFG invalid: NULL config");
        return RESULT_INVALID_PARAM;
    }

    /* Validate APN */
    if (strlen(net_config->apn) == 0) {
        wm_sdk_log_error("NET CFG invalid APN: empty string");
        return RESULT_INVALID_PARAM;
    }

    return RESULT_SUCCESS;
}

NetworkConfig *network_config_get_storage(void)
{
    const ModuleConfig *module_config = module_manager_get_config(MODULE_ID_NETWORK);
    if (!module_config || !module_config->config_ptr) {
        wm_sdk_log_error("NET CFG: network module config not found");
        return NULL;
    }
    return (NetworkConfig *)module_config->config_ptr;
}

Result network_config_set_apn(const char *apn, const char *username, const char *password)
{
    NetworkConfig *net_config = network_config_get_storage();
    if (!net_config) {
        wm_sdk_log_error("NET CFG storage not available");
        return RESULT_ERROR;
    }

    if (!apn || strlen(apn) == 0 || strlen(apn) >= sizeof(net_config->apn)) {
        wm_sdk_log_error("NET CFG invalid APN: %s", apn ? apn : "NULL");
        return RESULT_INVALID_PARAM;
    }

    utils_strncpy_safe(net_config->apn, apn, sizeof(net_config->apn));

    if (username) {
        if (strlen(username) >= sizeof(net_config->username)) {
            wm_sdk_log_error("NET CFG username too long: %s", username);
            return RESULT_INVALID_PARAM;
        }
        utils_strncpy_safe(net_config->username, username, sizeof(net_config->username));
    } else {
        net_config->username[0] = '\0';
    }

    if (password) {
        if (strlen(password) >= sizeof(net_config->password)) {
            wm_sdk_log_error("NET CFG password too long");
            return RESULT_INVALID_PARAM;
        }
        utils_strncpy_safe(net_config->password, password, sizeof(net_config->password));
    } else {
        net_config->password[0] = '\0';
    }

    wm_sdk_log_info("NET CFG APN updated: APN=%s, Username=%s",
                 net_config->apn,
                 net_config->username[0] ? net_config->username : "(empty)");

    /* Save configuration to file via config system (reference pattern) */
    config_get_current();
    if (config_save_to_file(NULL) == RESULT_SUCCESS)
        wm_sdk_debug_print("NET CFG saved to file\r\n");
    else
        wm_sdk_log_error("NET CFG save to file failed");

    return RESULT_SUCCESS;
}

Result network_config_set(const char *config_string)
{
    if (!config_string) {
        wm_sdk_log_error("NET CFG string is NULL");
        return RESULT_INVALID_PARAM;
    }

    NetworkConfig *config = network_config_get_storage();
    if (!config) {
        wm_sdk_log_error("NET CFG storage not available");
        return RESULT_ERROR;
    }

    /* Make a copy of the string for tokenization */
    char *str_copy = utils_strdup_for_tokenization(config_string);
    if (!str_copy) {
        wm_sdk_log_error("NET CFG alloc failed for config string");
        return RESULT_INVALID_PARAM;
    }

    /* Temporary storage for validated values */
    char temp_apn[32];
    char temp_username[32];
    char temp_password[32];
    int  temp_cid          = config->cid;
    BOOL temp_auto_connect = config->auto_connect;

    utils_strncpy_safe(temp_apn, config->apn, sizeof(temp_apn));
    utils_strncpy_safe(temp_username, config->username, sizeof(temp_username));
    utils_strncpy_safe(temp_password, config->password, sizeof(temp_password));

    char  *token;
    char  *saveptr    = NULL;
    BOOL   parsed_any = FALSE;
    Result result     = RESULT_SUCCESS;

    /* Keyed tokens only; skip bare comma fragments. */
    token = utils_strtok_r(str_copy, ",", &saveptr);
    while (token != NULL && result == RESULT_SUCCESS) {
        int  current_index;
        char key_buf[32];

        token = utils_trim_whitespace(token);
        if (!utils_config_parse_key(token, key_buf, sizeof(key_buf))) {
            token = utils_strtok_r(NULL, ",", &saveptr);
            continue;
        }
        current_index = utils_config_key_to_index(key_buf, s_net_cfg_keys,
                                                  sizeof(s_net_cfg_keys) / sizeof(s_net_cfg_keys[0]));
        if (current_index < 0) {
            wm_sdk_log_error("NET CFG unknown key: %s", key_buf);
            result = RESULT_INVALID_PARAM;
            break;
        }

        parsed_any = TRUE;
        const char *value_str = utils_extract_config_value(token);

        switch (current_index) {
        case 0: /* apn */
        {
            size_t len = strlen(value_str);
            if (len == 0 || len >= sizeof(temp_apn)) {
                wm_sdk_log_error("NET CFG invalid APN: %s (length must be 1-%u)",
                              value_str, (unsigned)(sizeof(temp_apn) - 1));
                result = RESULT_INVALID_PARAM;
                break;
            }
            utils_strncpy_safe(temp_apn, value_str, sizeof(temp_apn));
            break;
        }

        case 1: /* username */
        {
            size_t len = strlen(value_str);
            if (len >= sizeof(temp_username)) {
                wm_sdk_log_error("NET CFG invalid username length: %u (max %u)",
                              (unsigned)len, (unsigned)(sizeof(temp_username) - 1));
                result = RESULT_INVALID_PARAM;
                break;
            }
            utils_strncpy_safe(temp_username, value_str, sizeof(temp_username));
            break;
        }

        case 2: /* password */
        {
            size_t len = strlen(value_str);
            if (len >= sizeof(temp_password)) {
                wm_sdk_log_error("NET CFG invalid password length: %u (max %u)",
                              (unsigned)len, (unsigned)(sizeof(temp_password) - 1));
                result = RESULT_INVALID_PARAM;
                break;
            }
            utils_strncpy_safe(temp_password, value_str, sizeof(temp_password));
            break;
        }

        case 3: /* cid */
        {
            int value = atoi(value_str);
            if (value < 1 || value > 16) {
                wm_sdk_log_error("NET CFG invalid CID: %s (must be 1-16)", value_str);
                result = RESULT_INVALID_PARAM;
                break;
            }
            temp_cid = value;
            break;
        }

        case 4: /* auto_connect */
        {
            if (strcasecmp(value_str, "TRUE") == 0 || strcmp(value_str, "1") == 0) {
                temp_auto_connect = TRUE;
            } else if (strcasecmp(value_str, "FALSE") == 0 || strcmp(value_str, "0") == 0) {
                temp_auto_connect = FALSE;
            } else {
                wm_sdk_log_error("NET CFG invalid auto_connect: %s (must be TRUE/FALSE/1/0)", value_str);
                result = RESULT_INVALID_PARAM;
            }
            break;
        }

        default:
            /* Ignore extra parameters */
            break;
        }

        token = utils_strtok_r(NULL, ",", &saveptr);
    }

    free(str_copy);

    if (result != RESULT_SUCCESS)
        return result;

    if (!parsed_any) {
        wm_sdk_log_error("NET CFG no recognized keys in config string");
        return RESULT_INVALID_PARAM;
    }

    /* Second pass: Update config with validated values */
    utils_strncpy_safe(config->apn, temp_apn, sizeof(config->apn));
    utils_strncpy_safe(config->username, temp_username, sizeof(config->username));
    utils_strncpy_safe(config->password, temp_password, sizeof(config->password));
    config->cid          = temp_cid;
    config->auto_connect = temp_auto_connect;

    /* Save configuration to file */
    config_get_current();
    if (config_save_to_file(NULL) == RESULT_SUCCESS)
        wm_sdk_debug_print("NET CFG saved to file\r\n");
    else
        wm_sdk_log_error("NET CFG save to file failed");

    wm_sdk_log_info("NET CFG updated");
    return RESULT_SUCCESS;
}

Result network_config_get_string(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0)
        return RESULT_INVALID_PARAM;

    const ModuleConfig *module_config = module_manager_get_config(MODULE_ID_NETWORK);
    if (!module_config || !module_config->config_ptr) {
        wm_sdk_log_error("NET CFG: network module config not found");
        return RESULT_ERROR;
    }

    const NetworkConfig *config = (const NetworkConfig *)module_config->config_ptr;

    /* Format config values as comma-separated string with prefixes */
    int len = snprintf(buffer, buffer_size, "apn:%s,user:%s,pass:%s,cid:%d,auto:%s",
                       config->apn,
                       config->username,
                       config->password,
                       config->cid,
                       config->auto_connect ? "TRUE" : "FALSE");

    if (len < 0 || (size_t)len >= buffer_size) {
        wm_sdk_log_error("NET CFG buffer too small for config string");
        return RESULT_ERROR;
    }

    return RESULT_SUCCESS;
}
