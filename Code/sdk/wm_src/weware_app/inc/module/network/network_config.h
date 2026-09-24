/**
 * @file network_config.h
 * @brief Network module configuration - walnut port of the reference
 *        firmware's module/network/network_config.h (types, defaults and the
 *        config get/set/persist API; task/timeout constants stay in network.c).
 */

#ifndef WEWARE_NETWORK_CONFIG_H
#define WEWARE_NETWORK_CONFIG_H

#include <stddef.h>

#include "common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Default APN and credentials (used by network_config_get_defaults) */
#define NETWORK_DEFAULT_APN       "wheelseye.com"
#define NETWORK_VI_TEST_APN         "TESTAPN.WHEELSEYE.COM"
#define NETWORK_AIRTEL_TEST_APN         "test.wheelseye.m2m"
#define NETWORK_JIO_APN         "jionet"
#define NETWORK_DEFAULT_USERNAME  ""
#define NETWORK_DEFAULT_PASSWORD  ""

/** Compile-time APN override (bench testing).
 *
 *  Defined => network_config_apply_forced_apn() pushes this APN through
 *  network_config_set_apn() on every boot, so it wins over BOTH
 *  network_config_get_defaults() and whatever the persisted config file holds.
 *  Needed while SMS/TCP config commands cannot be used to switch the APN.
 *
 *  Comment the three defines out to go back to the persisted/default APN
 *  (the persisted file still holds the last forced value, so also send a
 *  config command - or erase the config - after removing the override).
 */
#define NETWORK_FORCE_APN           NETWORK_DEFAULT_APN
#define NETWORK_FORCE_APN_USERNAME  ""
#define NETWORK_FORCE_APN_PASSWORD  ""

/**
 * @brief Network module configuration structure (reference layout)
 */
typedef struct
{
    char apn[32];                      /**< Access Point Name */
    char username[32];                 /**< APN username */
    char password[32];                 /**< APN password */
    int  cid;                          /**< Context ID */
    BOOL auto_connect;                 /**< Auto-connect on initialization */
} NetworkConfig;

/**
 * @brief Get Network module default configuration
 * @note Compatible with ModuleConfigGetDefaultsFn - takes void* parameter
 */
void network_config_get_defaults(void *config);

/**
 * @brief Validate Network configuration
 * @return RESULT_SUCCESS if valid, RESULT_INVALID_PARAM if invalid
 * @note Compatible with ModuleConfigValidateFn - takes const void* parameter
 */
Result network_config_validate(const void *config);

/**
 * @brief Get network module configuration storage pointer (via module registry)
 */
NetworkConfig *network_config_get_storage(void);

/**
 * @brief Set APN configuration (APN, username, password) and save to file
 * @return RESULT_SUCCESS on success, RESULT_INVALID_PARAM on error
 */
Result network_config_set_apn(const char *apn, const char *username, const char *password);

/**
 * @brief Apply the NETWORK_FORCE_APN compile-time override, if one is defined.
 *        Called from weware_network_init() after the config file has been
 *        loaded, so it overrides both the defaults and the persisted APN.
 *        No-op (and no flash write) when the APN already matches, or when
 *        NETWORK_FORCE_APN is not defined.
 */
void network_config_apply_forced_apn(void);

/**
 * @brief Set network configuration from comma-separated string and save to file
 *        Format: "apn:<value>,user:<value>,pass:<value>,cid:<value>,auto:<value>"
 *        Partial updates allowed; all values validate before any is applied.
 * @return RESULT_SUCCESS if valid and set, RESULT_INVALID_PARAM if invalid
 */
Result network_config_set(const char *config_string);

/**
 * @brief Render the current config as "apn:...,user:...,pass:...,cid:...,auto:..."
 * @return RESULT_SUCCESS, RESULT_INVALID_PARAM, or RESULT_ERROR (buffer too small)
 */
Result network_config_get_string(char *buffer, size_t buffer_size);

/** Network module configuration storage (defined in network.c, reference pattern) */
extern NetworkConfig g_network_config;

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_NETWORK_CONFIG_H */
