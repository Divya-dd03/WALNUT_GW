/**
 * @file sms_config.c
 * @brief SMS module configuration with default values
 *
 * Walnut port of the reference firmware's module/sms/sms_config.c.
 *
 * Walnut adaptation: the reference logs invalid configuration through
 * LOG_ERRC(ERR_SMS_CONFIG_FAILED, ...) (common/error_codes.h + the error-code
 * log sink). Walnut has no error_codes.h and log.h exports no LOG_ERRC, so the
 * calls become LOG_ERROR with the reference error-code name kept in the text -
 * same as every other module already ported to walnut.
 */

#include "module/sms/sms_config.h"
#include "module/module_manager.h"
#include <string.h>

#include "wm_sdk_log.h"

/*---------------------------------------------------------------
 * Log Configuration
 *--------------------------------------------------------------*/
#define LOG_TAG "SMS_CFG"
#define LOG_MODULE_LEVEL LOG_LEVEL_DEBUG
#include "module/log/log.h"

void sms_config_get_defaults(void *config)
{
    SmsConfig *sms_cfg = (SmsConfig *)config;

    if (!sms_cfg)
    {
        return;
    }

    sms_cfg->format_mode = 1;        /* TEXT mode */
    sms_cfg->urc_timeout_ms = 500;  /* 500ms timeout */
}

Result sms_config_validate(const void *config)
{
    const SmsConfig *sms_cfg = (const SmsConfig *)config;

    if (!sms_cfg)
    {
        LOG_ERROR("ERR_SMS_CONFIG_FAILED: NULL config"); /* ERRC */
        return RESULT_INVALID_PARAM;
    }

    if (sms_cfg->format_mode != 0 && sms_cfg->format_mode != 1)
    {
        LOG_ERROR("ERR_SMS_CONFIG_FAILED: Invalid format_mode: %d (must be 0 or 1)",
                (int)sms_cfg->format_mode); /* ERRC */
        return RESULT_INVALID_PARAM;
    }

    if (sms_cfg->urc_timeout_ms == 0 || sms_cfg->urc_timeout_ms > 10000)
    {
        LOG_ERROR("ERR_SMS_CONFIG_FAILED: Invalid urc_timeout_ms: %u (must be 1-10000)",
                (unsigned)sms_cfg->urc_timeout_ms); /* ERRC */
        return RESULT_INVALID_PARAM;
    }

    return RESULT_SUCCESS;
}

SmsConfig *sms_config_get_storage(void)
{
    const ModuleConfig *module_config = module_manager_get_config(MODULE_ID_SMS);
    if (!module_config || !module_config->config_ptr)
    {
        LOG_ERROR("SMS module config not found");
        return NULL;
    }
    return (SmsConfig *)module_config->config_ptr;
}
