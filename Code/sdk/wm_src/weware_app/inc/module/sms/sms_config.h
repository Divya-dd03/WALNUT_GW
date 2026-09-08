/**
 * @file sms_config.h
 * @brief SMS module configuration
 *
 * Walnut port of the reference firmware's module/sms/sms_config.h - verbatim
 * (the structure holds no SDK types, so nothing had to be adapted).
 */

#ifndef WEWARE_SMS_CONFIG_H
#define WEWARE_SMS_CONFIG_H

#include "common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SMS module configuration structure
 */
typedef struct
{
    INT32 format_mode;              /**< SMS format mode (0=PDU, 1=TEXT) */
    UINT32 urc_timeout_ms;          /**< Timeout for SMS read operation (ms) */
} SmsConfig;

/**
 * @brief Get SMS module default configuration
 * @param config Output SMS configuration structure (must not be NULL)
 * @note Compatible with ModuleConfigGetDefaultsFn - takes void* parameter
 */
void sms_config_get_defaults(void *config);

/**
 * @brief Validate SMS configuration
 * @param config SMS configuration to validate
 * @return RESULT_SUCCESS if valid, RESULT_INVALID_PARAM if invalid
 * @note Compatible with ModuleConfigValidateFn - takes const void* parameter
 */
Result sms_config_validate(const void *config);

/**
 * @brief Get SMS module configuration storage pointer
 * @return Pointer to SMS configuration storage (for ModuleStatus->config_ptr)
 */
SmsConfig *sms_config_get_storage(void);

/**
 * @brief SMS module configuration storage (defined in sms_manager.c)
 * @note This is the actual storage for SMS configuration
 */
extern SmsConfig g_sms_config;

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_SMS_CONFIG_H */
