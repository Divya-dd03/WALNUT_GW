/**
  ******************************************************************************
  * @file    gps_config.c
  * @author  WheelsEye
  * @brief   GPS module configuration with default values - walnut port of the
  *          reference firmware's module/gps/gps_config.c. Parsing and
  *          validate-then-apply logic is verbatim.
  *
  *          Walnut adaptations:
  *          - config storage is the file-scope @c g_gps_config (reference
  *            resolves it through module_manager_get_config).
  *          - config file persistence after a successful set is not ported
  *            yet (reference: config_save_to_file). TODO(gps)
  ******************************************************************************
  */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>    /* strcasecmp */

// sdk
#include "wm_global.h"
#include "wm_sdk_log.h"

// app
#include "module/gps/gps_config.h"
#include "module/gps/gps_manager.h"
#include "common/utils.h"

#define LOG_TAG "GPS_CFG"
#define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

/* ============================================================================
 * File scope
 * ============================================================================ */

/* GPS runtime state (gps_manager.c) - start_mode changes reset its flag */
extern gps_manager_runtime_t g_gps;

/** Global GPS module configuration (declared in gps_config.h) */
GpsConfig g_gps_config = {0};

static const ConfigKeyMap s_gps_cfg_keys[] = {
    { "i-on",       0 },
    { "i-off",      1 },
    { "ang",        2 },
    { "dist",       3 },
    { "ang-tr",     4 },
    { "dist-tr",    5 },
    { "agps",       6 },
    { "start",      7 },
    { "lv-boot",    8 },
    { "lv-ign-off", 9 },
    { "lv-no-fix",  10 },
    { "time-src",   11 },
    { "agps-ref",   12 },
    { "agps_ref",   12 },
    { "spd-filt",   13 },
    { "speed-filter", 13 },
    { "sf",         13 },
};

static BOOL gps_config_parse_time_source(const char *value_str, GpsTimeSource *out)
{
    if (!value_str || !out)
        return FALSE;

    if (strcasecmp(value_str, "AUTO") == 0 || strcmp(value_str, "0") == 0) {
        *out = GPS_TIME_SOURCE_AUTO;
        return TRUE;
    }
    if (strcasecmp(value_str, "GPS") == 0 || strcmp(value_str, "1") == 0) {
        *out = GPS_TIME_SOURCE_GPS;
        return TRUE;
    }
    if (strcasecmp(value_str, "GSM") == 0 || strcmp(value_str, "2") == 0) {
        *out = GPS_TIME_SOURCE_GSM;
        return TRUE;
    }
    return FALSE;
}

static unsigned gps_config_start_mode_to_index(UINT32 mode)
{
    if (mode == SDK_GNSS_START_WARM)
        return 1u;
    if (mode == SDK_GNSS_START_COLD)
        return 2u;
    return 0u;
}

/* ============================================================================
 * Defaults
 * ============================================================================ */

/* Set once defaults (or a config-file load via the module framework, which
 * calls this first) have been applied to the global storage; lets
 * gps_manager_init avoid clobbering config loaded by config_load_from_file. */
static BOOL g_gps_config_defaults_applied = FALSE;

BOOL gps_config_defaults_applied(void)
{
    return g_gps_config_defaults_applied;
}

void gps_config_get_defaults(GpsConfig *config)
{
    if (!config)
    {
        return;
    }

    if (config == &g_gps_config)
    {
        g_gps_config_defaults_applied = TRUE;
    }

    /* GPS Trigger Conditions */
    config->ign_on_interval_sec = GPS_DEFAULT_IGN_ON_INTERVAL_SEC;
    config->ign_off_interval_sec = GPS_DEFAULT_IGN_OFF_INTERVAL_SEC;
    config->angle_threshold_deg = GPS_DEFAULT_ANGLE_THRESHOLD_DEG;
    config->distance_threshold_m = GPS_DEFAULT_DISTANCE_THRESHOLD_M;
    config->enable_angle_trigger = GPS_DEFAULT_ENABLE_ANGLE_TRIGGER;
    config->enable_distance_trigger = GPS_DEFAULT_ENABLE_DISTANCE_TRIGGER;
    config->enable_agps = GPS_DEFAULT_ENABLE_AGPS;
    config->agps_ref = GPS_DEFAULT_AGPS_REF;
    config->start_mode = GPS_DEFAULT_START_MODE;

    /* Data send policy: last valid when to send (each configurable) */
    config->send_last_valid_on_boot_no_fix = GPS_DEFAULT_SEND_LAST_VALID_ON_BOOT_NO_FIX;
    config->send_last_valid_on_ign_off_no_movement = GPS_DEFAULT_SEND_LAST_VALID_ON_IGN_OFF_NO_MOVEMENT;
    config->send_last_valid_on_no_fix = GPS_DEFAULT_SEND_LAST_VALID_ON_NO_FIX;
    config->time_source = GPS_DEFAULT_TIME_SOURCE;
    config->speed_filter_kmh = GPS_DEFAULT_SPEED_FILTER_KMH;
}

/* ============================================================================
 * Set config (parse string and update storage)
 * ============================================================================ */

wm_SdkResult gps_config_set(const char *config_string)
{
    if (!config_string)
    {
        return WM_SDK_RESULT_INVALID_PARAM;
    }

    GpsConfig *config = gps_config_get_storage();

    /* Make a copy of the string for tokenization (strtok modifies the string) */
    char *str_copy = utils_strdup_for_tokenization(config_string);
    if (!str_copy)
    {
        LOG_ERROR("GPS config: failed to allocate memory for config string");
        return WM_SDK_RESULT_INVALID_PARAM;
    }

    /* Temporary storage for validated values - only update config if all are valid */
    int temp_ign_on_interval_sec = config->ign_on_interval_sec;
    int temp_ign_off_interval_sec = config->ign_off_interval_sec;
    float temp_angle_threshold_deg = config->angle_threshold_deg;
    float temp_distance_threshold_m = config->distance_threshold_m;
    BOOL temp_enable_angle_trigger = config->enable_angle_trigger;
    BOOL temp_enable_distance_trigger = config->enable_distance_trigger;
    BOOL temp_enable_agps = config->enable_agps;
    BOOL temp_agps_ref = config->agps_ref;
    UINT32 temp_start_mode = config->start_mode;
    BOOL temp_send_last_valid_on_boot_no_fix = config->send_last_valid_on_boot_no_fix;
    BOOL temp_send_last_valid_on_ign_off_no_movement = config->send_last_valid_on_ign_off_no_movement;
    BOOL temp_send_last_valid_on_no_fix = config->send_last_valid_on_no_fix;
    GpsTimeSource temp_time_source = config->time_source;
    float temp_speed_filter_kmh = config->speed_filter_kmh;

    char *token;
    char *saveptr = NULL;
    BOOL provided_params[14] = {FALSE};
    BOOL any_provided = FALSE;
    wm_SdkResult result = WM_SDK_RESULT_SUCCESS;

    /* Keyed tokens only; skip bare comma fragments. */
    token = utils_strtok_r(str_copy, ",", &saveptr);
    while (token != NULL && result == WM_SDK_RESULT_SUCCESS)
    {
        int current_index;
        char key_buf[32];

        token = utils_trim_whitespace(token);
        if (!utils_config_parse_key(token, key_buf, sizeof(key_buf))) {
            token = utils_strtok_r(NULL, ",", &saveptr);
            continue;
        }
        current_index = utils_config_key_to_index(key_buf, s_gps_cfg_keys,
                                                  sizeof(s_gps_cfg_keys) / sizeof(s_gps_cfg_keys[0]));
        if (current_index < 0) {
            LOG_ERROR("Unknown GPS config key: %s", key_buf);
            result = WM_SDK_RESULT_INVALID_PARAM;
            break;
        }

        if (current_index < 14)
            provided_params[current_index] = TRUE;

        const char *value_str = utils_extract_config_value(token);

        switch (current_index)
        {
        case 0: /* ign_on_interval_sec */
        {
            int value = atoi(value_str);
            if (value <= 0)
            {
                LOG_ERROR("Invalid ign_on_interval_sec: %s (must be > 0)", value_str);
                result = WM_SDK_RESULT_INVALID_PARAM;
                break;
            }
            temp_ign_on_interval_sec = value;
            break;
        }

        case 1: /* ign_off_interval_sec */
        {
            int value = atoi(value_str);
            if (value <= 0)
            {
                LOG_ERROR("Invalid ign_off_interval_sec: %s (must be > 0)", value_str);
                result = WM_SDK_RESULT_INVALID_PARAM;
                break;
            }
            temp_ign_off_interval_sec = value;
            break;
        }

        case 2: /* angle_threshold_deg */
        {
            float value = (float)atof(value_str);
            if (value < 0.0f || value > 180.0f)
            {
                LOG_ERROR("Invalid angle_threshold_deg: %s (must be 0-180)", value_str);
                result = WM_SDK_RESULT_INVALID_PARAM;
                break;
            }
            temp_angle_threshold_deg = value;
            break;
        }

        case 3: /* distance_threshold_m */
        {
            float value = (float)atof(value_str);
            if (value < 0.0f)
            {
                LOG_ERROR("Invalid distance_threshold_m: %s (must be >= 0)", value_str);
                result = WM_SDK_RESULT_INVALID_PARAM;
                break;
            }
            temp_distance_threshold_m = value;
            break;
        }

        case 4: /* enable_angle_trigger */
        {
            if (strcasecmp(value_str, "TRUE") == 0 || strcmp(value_str, "1") == 0)
            {
                temp_enable_angle_trigger = TRUE;
            }
            else if (strcasecmp(value_str, "FALSE") == 0 || strcmp(value_str, "0") == 0)
            {
                temp_enable_angle_trigger = FALSE;
            }
            else
            {
                LOG_ERROR("Invalid enable_angle_trigger: %s (must be TRUE/FALSE/1/0)", value_str);
                result = WM_SDK_RESULT_INVALID_PARAM;
            }
            break;
        }

        case 5: /* enable_distance_trigger */
        {
            if (strcasecmp(value_str, "TRUE") == 0 || strcmp(value_str, "1") == 0)
            {
                temp_enable_distance_trigger = TRUE;
            }
            else if (strcasecmp(value_str, "FALSE") == 0 || strcmp(value_str, "0") == 0)
            {
                temp_enable_distance_trigger = FALSE;
            }
            else
            {
                LOG_ERROR("Invalid enable_distance_trigger: %s (must be TRUE/FALSE/1/0)", value_str);
                result = WM_SDK_RESULT_INVALID_PARAM;
            }
            break;
        }

        case 6: /* enable_agps */
        {
            if (strcasecmp(value_str, "TRUE") == 0 || strcmp(value_str, "1") == 0)
            {
                temp_enable_agps = TRUE;
            }
            else if (strcasecmp(value_str, "FALSE") == 0 || strcmp(value_str, "0") == 0)
            {
                temp_enable_agps = FALSE;
            }
            else
            {
                LOG_ERROR("Invalid enable_agps: %s (must be TRUE/FALSE/1/0)", value_str);
                result = WM_SDK_RESULT_INVALID_PARAM;
            }
            break;
        }

        case 7: /* start_mode */
        {
            UINT32 mode_value = 0;
            BOOL valid_mode = FALSE;

            /* Check for string values: HOT, WARM, COLD */
            if (strcasecmp(value_str, "HOT") == 0)
            {
                mode_value = SDK_GNSS_START_HOT;
                valid_mode = TRUE;
            }
            else if (strcasecmp(value_str, "WARM") == 0)
            {
                mode_value = SDK_GNSS_START_WARM;
                valid_mode = TRUE;
            }
            else if (strcasecmp(value_str, "COLD") == 0)
            {
                mode_value = SDK_GNSS_START_COLD;
                valid_mode = TRUE;
            }
            else
            {
                /* Try numeric value: 0=HOT, 1=WARM, 2=COLD */
                int num_value = atoi(value_str);
                if (num_value == 0)
                {
                    mode_value = SDK_GNSS_START_HOT;
                    valid_mode = TRUE;
                }
                else if (num_value == 1)
                {
                    mode_value = SDK_GNSS_START_WARM;
                    valid_mode = TRUE;
                }
                else if (num_value == 2)
                {
                    mode_value = SDK_GNSS_START_COLD;
                    valid_mode = TRUE;
                }
            }

            if (!valid_mode)
            {
                LOG_ERROR("Invalid start_mode: %s (must be 0/HOT, 1/WARM, or 2/COLD)", value_str);
                result = WM_SDK_RESULT_INVALID_PARAM;
            }
            else
            {
                temp_start_mode = mode_value;
            }
            break;
        }

        case 8: /* send_last_valid_on_boot_no_fix */
        {
            if (strcasecmp(value_str, "TRUE") == 0 || strcmp(value_str, "1") == 0)
                temp_send_last_valid_on_boot_no_fix = TRUE;
            else if (strcasecmp(value_str, "FALSE") == 0 || strcmp(value_str, "0") == 0)
                temp_send_last_valid_on_boot_no_fix = FALSE;
            else
            {
                LOG_ERROR("Invalid send_last_valid_on_boot_no_fix: %s (TRUE/FALSE/1/0)", value_str);
                result = WM_SDK_RESULT_INVALID_PARAM;
            }
            break;
        }

        case 9: /* send_last_valid_on_ign_off_no_movement */
        {
            if (strcasecmp(value_str, "TRUE") == 0 || strcmp(value_str, "1") == 0)
                temp_send_last_valid_on_ign_off_no_movement = TRUE;
            else if (strcasecmp(value_str, "FALSE") == 0 || strcmp(value_str, "0") == 0)
                temp_send_last_valid_on_ign_off_no_movement = FALSE;
            else
            {
                LOG_ERROR("Invalid send_last_valid_on_ign_off_no_movement: %s (TRUE/FALSE/1/0)", value_str);
                result = WM_SDK_RESULT_INVALID_PARAM;
            }
            break;
        }

        case 10: /* send_last_valid_on_no_fix */
        {
            if (strcasecmp(value_str, "TRUE") == 0 || strcmp(value_str, "1") == 0)
                temp_send_last_valid_on_no_fix = TRUE;
            else if (strcasecmp(value_str, "FALSE") == 0 || strcmp(value_str, "0") == 0)
                temp_send_last_valid_on_no_fix = FALSE;
            else
            {
                LOG_ERROR("Invalid send_last_valid_on_no_fix: %s (TRUE/FALSE/1/0)", value_str);
                result = WM_SDK_RESULT_INVALID_PARAM;
            }
            break;
        }

        case 11: /* time_source */
        {
            if (!gps_config_parse_time_source(value_str, &temp_time_source))
            {
                LOG_ERROR("Invalid time_source: %s (must be AUTO/GPS/GSM or 0/1/2)", value_str);
                result = WM_SDK_RESULT_INVALID_PARAM;
            }
            break;
        }

        case 12: /* agps_ref */
        {
            if (strcasecmp(value_str, "TRUE") == 0 || strcmp(value_str, "1") == 0)
                temp_agps_ref = TRUE;
            else if (strcasecmp(value_str, "FALSE") == 0 || strcmp(value_str, "0") == 0)
                temp_agps_ref = FALSE;
            else
            {
                LOG_ERROR("Invalid agps_ref: %s (must be TRUE/FALSE/1/0)", value_str);
                result = WM_SDK_RESULT_INVALID_PARAM;
            }
            break;
        }

        case 13: /* speed_filter_kmh */
        {
            char *end = NULL;
            float value = strtof(value_str, &end);
            if (end == value_str || (end && *end != '\0') || value < 0.0f)
            {
                LOG_ERROR("Invalid speed_filter_kmh: %s (must be >= 0)", value_str);
                result = WM_SDK_RESULT_INVALID_PARAM;
            }
            else
                temp_speed_filter_kmh = value;
            break;
        }

        default:
            /* Ignore extra parameters beyond expected set. */
            break;
        }

        token = utils_strtok_r(NULL, ",", &saveptr);
    }

    {
        int i;
        for (i = 0; i < 14; i++) {
            if (provided_params[i]) {
                any_provided = TRUE;
                break;
            }
        }
        if (result == WM_SDK_RESULT_SUCCESS && !any_provided)
            result = WM_SDK_RESULT_INVALID_PARAM;
    }

    if (result == WM_SDK_RESULT_SUCCESS && any_provided)
    {
        /* Check if start_mode changed before updating */
        BOOL start_mode_changed = FALSE;
        if (provided_params[7] && config->start_mode != temp_start_mode)
        {
            start_mode_changed = TRUE;
        }

        config->ign_on_interval_sec = temp_ign_on_interval_sec;
        config->ign_off_interval_sec = temp_ign_off_interval_sec;
        config->angle_threshold_deg = temp_angle_threshold_deg;
        config->distance_threshold_m = temp_distance_threshold_m;
        config->enable_angle_trigger = temp_enable_angle_trigger;
        config->enable_distance_trigger = temp_enable_distance_trigger;
        config->enable_agps = temp_enable_agps;
        if (provided_params[12])
            config->agps_ref = temp_agps_ref;

        /* Update start_mode if it was provided */
        if (provided_params[7])
        {
            config->start_mode = temp_start_mode;
        }
        if (provided_params[8])
        {
            config->send_last_valid_on_boot_no_fix = temp_send_last_valid_on_boot_no_fix;
        }
        if (provided_params[9])
        {
            config->send_last_valid_on_ign_off_no_movement = temp_send_last_valid_on_ign_off_no_movement;
        }
        if (provided_params[10])
        {
            config->send_last_valid_on_no_fix = temp_send_last_valid_on_no_fix;
        }
        if (provided_params[11])
        {
            config->time_source = temp_time_source;
        }
        if (provided_params[13])
        {
            config->speed_filter_kmh = temp_speed_filter_kmh;
        }

        /* If start_mode changed, reset the flag so it gets reapplied */
        if (start_mode_changed)
        {
            g_gps.gnss_start_mode_set = FALSE;
            LOG_INFO("GPS start_mode changed, will be reapplied on next configuration cycle");
        }
    }

    if (result == WM_SDK_RESULT_SUCCESS && any_provided)
    {
        /* TODO(gps): reference saves the full config file here
         * (config_get_current + config_save_to_file); config persistence
         * is not ported yet, so changes are RAM-only. */
        LOG_INFO("GPS config applied (RAM only; file persistence TODO)");
    }

    if (result != WM_SDK_RESULT_SUCCESS)
        LOG_ERROR("GPS config rejected (invalid parameter)");

    utils_free_tokenization(str_copy);
    return result;
}

/* ============================================================================
 * Validate
 * ============================================================================ */

wm_SdkResult gps_config_validate(const GpsConfig *config)
{
    if (!config)
    {
        return WM_SDK_RESULT_INVALID_PARAM;
    }

    /* Validate IGN intervals */
    if (config->ign_on_interval_sec <= 0 || config->ign_off_interval_sec <= 0)
    {
        LOG_ERROR("Invalid IGN intervals: ign_on=%d, ign_off=%d (must be > 0)",
                      config->ign_on_interval_sec, config->ign_off_interval_sec);
        return WM_SDK_RESULT_INVALID_PARAM;
    }

    /* Validate thresholds */
    if (config->angle_threshold_deg < 0.0f || config->angle_threshold_deg > 180.0f)
    {
        LOG_ERROR("Invalid angle_threshold_deg: %d (must be 0-180)",
                      (int)config->angle_threshold_deg);
        return WM_SDK_RESULT_INVALID_PARAM;
    }

    if (config->distance_threshold_m < 0.0f)
    {
        LOG_ERROR("Invalid distance_threshold_m: %d (must be >= 0)",
                      (int)config->distance_threshold_m);
        return WM_SDK_RESULT_INVALID_PARAM;
    }

    /* Validate start_mode */
    if (config->start_mode != SDK_GNSS_START_HOT &&
        config->start_mode != SDK_GNSS_START_WARM &&
        config->start_mode != SDK_GNSS_START_COLD)
    {
        LOG_ERROR("Invalid start_mode: %u (must be HOT/WARM/COLD)",
                      (unsigned)config->start_mode);
        return WM_SDK_RESULT_INVALID_PARAM;
    }

    if (config->time_source != GPS_TIME_SOURCE_AUTO &&
        config->time_source != GPS_TIME_SOURCE_GPS &&
        config->time_source != GPS_TIME_SOURCE_GSM)
    {
        LOG_ERROR("Invalid time_source: %d (must be AUTO/GPS/GSM)", (int)config->time_source);
        return WM_SDK_RESULT_INVALID_PARAM;
    }

    if (config->speed_filter_kmh < 0.0f)
    {
        LOG_ERROR("Invalid speed_filter_kmh: %d (must be >= 0)", (int)config->speed_filter_kmh);
        return WM_SDK_RESULT_INVALID_PARAM;
    }

    return WM_SDK_RESULT_SUCCESS;
}

/* ============================================================================
 * Storage and get string
 * ============================================================================ */

GpsConfig *gps_config_get_storage(void)
{
    return &g_gps_config;
}

wm_SdkResult gps_config_get_string(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0)
    {
        return WM_SDK_RESULT_INVALID_PARAM;
    }

    const GpsConfig *config = gps_config_get_storage();

    /*
     * Compact CSV (fits an SMS: 160 chars). Booleans as 0/1;
     * start 0=HOT 1=WARM 2=COLD; time-src 0=AUTO 1=GPS 2=GSM.
     * Float fields printed as whole numbers (reference uses %.0f; newlib-nano
     * printf may lack float support, so integers are used here).
     */
    int len = snprintf(buffer, buffer_size,
                       "i-on:%d,i-off:%d,ang:%d,dist:%d,ang-tr:%u,dist-tr:%u,agps:%u,start:%u,"
                       "lv-boot:%u,lv-ign-off:%u,lv-no-fix:%u,time-src:%u,agps-ref:%u,spd-filt:%d",
                       config->ign_on_interval_sec,
                       config->ign_off_interval_sec,
                       (int)config->angle_threshold_deg,
                       (int)config->distance_threshold_m,
                       config->enable_angle_trigger ? 1u : 0u,
                       config->enable_distance_trigger ? 1u : 0u,
                       config->enable_agps ? 1u : 0u,
                       gps_config_start_mode_to_index(config->start_mode),
                       config->send_last_valid_on_boot_no_fix ? 1u : 0u,
                       config->send_last_valid_on_ign_off_no_movement ? 1u : 0u,
                       config->send_last_valid_on_no_fix ? 1u : 0u,
                       (unsigned)config->time_source,
                       config->agps_ref ? 1u : 0u,
                       (int)config->speed_filter_kmh);

    /* Check if buffer was large enough */
    if (len < 0)
    {
        LOG_ERROR("Failed to format GPS config string");
        return WM_SDK_RESULT_ERROR;
    }

    if ((size_t)len >= buffer_size)
    {
        LOG_ERROR("Buffer too small for GPS config string (needed %d, got %u)",
                      len, (unsigned)buffer_size);
        return WM_SDK_RESULT_ERROR;
    }

    return WM_SDK_RESULT_SUCCESS;
}
