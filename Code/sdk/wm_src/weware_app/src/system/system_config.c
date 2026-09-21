/**
 * @file system_config.c
 * @brief System configuration parse/validate/persist - walnut port.
 *
 * Walnut adaptations vs the reference:
 * - The reference migrated soft-ignition thresholds from TcpConfig
 *   (ign_det_*) and mirrored updates back into it. The walnut
 *   WewareTcpConfig has no ignition-detection fields, so the migration is a
 *   no-op and the writeback is dropped.
 * - free() of the tokenization copy uses utils_free_tokenization()
 *   (walnut heap goes through wm_sdk_memory_alloc/free).
 */

#include "system/system_config.h"
#include "module/module_manager.h"
#include "config/config.h"
#include "common/utils.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>

#define LOG_TAG "SYS_CFG"
#define LOG_MODULE_LEVEL LOG_LEVEL_DEBUG
#include "module/log/log.h"

SystemConfig g_system_config;

static const ConfigKeyMap s_sys_cfg_keys[] = {
    { "ign-src",     0 }, { "is",          0 },
    { "wire-on-v",   1 }, { "wo",          1 },
    { "wire-off-v",  2 }, { "wf",          2 },
    { "soft-lo-v",   3 }, { "sl",          3 },
    { "soft-hi-v",   4 }, { "sh",          4 },
    { "deb",         5 }, { "d",           5 },
    { "mot-src",     6 }, { "ms",          6 },
    { "mot-method",  7 }, { "mm",          7 },
    { "mot-spd-kmh", 8 }, { "mot-spd",     8 }, { "sp", 8 },
    { "mot-dist-m",  9 }, { "mot-dist",    9 }, { "di", 9 },
    { "accel-g",    10 }, { "ag",         10 },
    { "accel-dur-ms", 11 }, { "ad",       11 },
    { "mot-stop-sec", 12 }, { "st",       12 },
    { "mot-spd-en", 13 }, { "se",         13 },
    { "mot-dist-en", 14 }, { "de",        14 },
    { "accel-en",   15 }, { "ae",         15 },
    { "mot-spd-calc", 16 }, { "sc",       16 },
};

static BOOL parse_bool(const char *s, BOOL *out)
{
    if (strcasecmp(s, "TRUE") == 0 || strcmp(s, "1") == 0) {
        *out = TRUE;
        return TRUE;
    }
    if (strcasecmp(s, "FALSE") == 0 || strcmp(s, "0") == 0) {
        *out = FALSE;
        return TRUE;
    }
    return FALSE;
}

static BOOL parse_enum_digit(const char *s, int max_inclusive, int *out)
{
    char *end = NULL;
    long v;

    if (!s || !out)
        return FALSE;
    v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 0 || v > max_inclusive)
        return FALSE;
    *out = (int)v;
    return TRUE;
}

static BOOL parse_ign_src(const char *s, SystemIgnSource *out)
{
    int v;

    if (parse_enum_digit(s, 1, &v)) {
        *out = (v == 0) ? SYSTEM_IGN_SOURCE_WIRE : SYSTEM_IGN_SOURCE_SOFT;
        return TRUE;
    }
    if (strcasecmp(s, "WIRE") == 0 || strcasecmp(s, "W") == 0) {
        *out = SYSTEM_IGN_SOURCE_WIRE;
        return TRUE;
    }
    if (strcasecmp(s, "SOFT") == 0 || strcasecmp(s, "S") == 0) {
        *out = SYSTEM_IGN_SOURCE_SOFT;
        return TRUE;
    }
    return FALSE;
}

static BOOL parse_mot_src(const char *s, SystemMotionSource *out)
{
    int v;

    if (parse_enum_digit(s, 1, &v)) {
        *out = (v == 0) ? SYSTEM_MOT_SOURCE_IGN : SYSTEM_MOT_SOURCE_SOFT;
        return TRUE;
    }
    if (strcasecmp(s, "IGN") == 0 || strcasecmp(s, "IGNITION") == 0) {
        *out = SYSTEM_MOT_SOURCE_IGN;
        return TRUE;
    }
    if (strcasecmp(s, "SOFT") == 0) {
        *out = SYSTEM_MOT_SOURCE_SOFT;
        return TRUE;
    }
    return FALSE;
}

static BOOL parse_mot_method(const char *s, SystemSoftMotionMethod *out)
{
    int v;

    if (parse_enum_digit(s, 4, &v)) {
        *out = (SystemSoftMotionMethod)v;
        return TRUE;
    }
    if (strcasecmp(s, "GNSS_SPEED") == 0 || strcasecmp(s, "GNSS") == 0) {
        *out = SYSTEM_SOFT_MOT_GNSS_SPEED;
        return TRUE;
    }
    if (strcasecmp(s, "ACCEL") == 0) {
        *out = SYSTEM_SOFT_MOT_ACCEL;
        return TRUE;
    }
    if (strcasecmp(s, "GPS_DIST") == 0 || strcasecmp(s, "DIST") == 0) {
        *out = SYSTEM_SOFT_MOT_GPS_DIST;
        return TRUE;
    }
    if (strcasecmp(s, "TRIP") == 0) {
        *out = SYSTEM_SOFT_MOT_TRIP;
        return TRUE;
    }
    if (strcasecmp(s, "ANY") == 0) {
        *out = SYSTEM_SOFT_MOT_ANY;
        return TRUE;
    }
    return FALSE;
}

void system_config_get_defaults(void *config)
{
    SystemConfig *c = (SystemConfig *)config;
    if (!c)
        return;

    c->ign_source = SYSTEM_IGN_SOURCE_WIRE;
    c->wire_on_v = SYSTEM_DEFAULT_WIRE_ON_V;
    c->wire_off_v = SYSTEM_DEFAULT_WIRE_OFF_V;
    c->soft_lo_v = SYSTEM_DEFAULT_SOFT_LO_V;
    c->soft_hi_v = SYSTEM_DEFAULT_SOFT_HI_V;
    c->debounce_polls = (UINT8)SYSTEM_DEFAULT_DEBOUNCE_POLLS;

    c->motion_source = SYSTEM_MOT_SOURCE_IGN;
    c->soft_motion_method = SYSTEM_SOFT_MOT_ANY;
    c->mot_spd_kmh = SYSTEM_DEFAULT_MOT_SPD_KMH;
    c->mot_dist_m = SYSTEM_DEFAULT_MOT_DIST_M;
    c->accel_g = SYSTEM_DEFAULT_ACCEL_G;
    c->accel_dur_ms = SYSTEM_DEFAULT_ACCEL_DUR_MS;
    c->mot_stop_debounce_sec = SYSTEM_DEFAULT_MOT_STOP_DEBOUNCE_SEC;
    c->mot_spd_calc = FALSE;
    c->mot_spd_en = FALSE;
    c->mot_dist_en = TRUE;
    /* Walnut: no accelerometer driver yet (TODO(accel)); default the accel
     * contribution off so SOFT/ANY motion relies on GNSS speed/distance. */
    c->accel_en = FALSE;
}

void system_config_migrate_from_tcp_if_needed(void)
{
    /* Reference: copy soft-ignition thresholds from TcpConfig.ign_det_* when
     * system config is still at defaults. The walnut WewareTcpConfig has no
     * ignition-detection fields, so there is nothing to migrate. */
}

Result system_config_validate(const void *config)
{
    const SystemConfig *c = (const SystemConfig *)config;

    if (!c)
        return RESULT_INVALID_PARAM;

    if (c->wire_off_v >= c->wire_on_v) {
        LOG_ERROR("Invalid wire thresholds: off=%.2f on=%.2f", c->wire_off_v, c->wire_on_v);
        return RESULT_INVALID_PARAM;
    }
    if (c->soft_lo_v >= c->soft_hi_v) {
        LOG_ERROR("Invalid soft thresholds: lo=%.2f hi=%.2f", c->soft_lo_v, c->soft_hi_v);
        return RESULT_INVALID_PARAM;
    }
    if (c->debounce_polls == 0u) {
        LOG_ERROR("Invalid debounce_polls: 0");
        return RESULT_INVALID_PARAM;
    }
    if (c->mot_spd_kmh < 0.0f || c->mot_dist_m < 0.0f || c->accel_g < 0.0f)
        return RESULT_INVALID_PARAM;
    if (c->accel_dur_ms == 0u || c->mot_stop_debounce_sec == 0u)
        return RESULT_INVALID_PARAM;

    return RESULT_SUCCESS;
}

SystemConfig *system_config_get_storage(void)
{
    const ModuleConfig *mc = module_manager_get_config(MODULE_ID_SYSTEM);
    if (!mc || !mc->config_ptr)
        return &g_system_config;
    return (SystemConfig *)mc->config_ptr;
}

Result system_config_set(const char *config_string)
{
    if (!config_string || config_string[0] == '\0')
        return RESULT_INVALID_PARAM;

    SystemConfig *config = system_config_get_storage();
    char *str_copy = utils_strdup_for_tokenization(config_string);
    if (!str_copy)
        return RESULT_ERROR;

    SystemConfig temp;
    memcpy(&temp, config, sizeof(temp));

    char *token;
    char *saveptr = NULL;
    int param_index = 0;
    Result result = RESULT_SUCCESS;

    token = utils_strtok_r(str_copy, ",", &saveptr);
    while (token != NULL && result == RESULT_SUCCESS) {
        token = utils_trim_whitespace(token);
        const char *value_str = utils_extract_config_value(token);

        int idx = param_index;
        const char *colon = strchr(token, ':');
        if (colon != NULL) {
            size_t key_len = (size_t)(colon - token);
            char key_buf[32];
            if (key_len == 0 || key_len >= sizeof(key_buf)) {
                result = RESULT_INVALID_PARAM;
                break;
            }
            memcpy(key_buf, token, key_len);
            key_buf[key_len] = '\0';
            idx = utils_config_key_to_index(key_buf, s_sys_cfg_keys,
                                            sizeof(s_sys_cfg_keys) / sizeof(s_sys_cfg_keys[0]));
            if (idx < 0) {
                LOG_ERROR("Unknown system config key: %s", key_buf);
                result = RESULT_INVALID_PARAM;
                break;
            }
        }

        switch (idx) {
        case 0: {
            SystemIgnSource src;
            if (!parse_ign_src(value_str, &src)) {
                result = RESULT_INVALID_PARAM;
                break;
            }
            temp.ign_source = src;
            break;
        }
        case 1:
        case 2:
        case 3:
        case 4: {
            char *end = NULL;
            float v = strtof(value_str, &end);
            if (end == value_str || (end && *end != '\0')) {
                result = RESULT_INVALID_PARAM;
                break;
            }
            if (idx == 1) temp.wire_on_v = v;
            else if (idx == 2) temp.wire_off_v = v;
            else if (idx == 3) temp.soft_lo_v = v;
            else temp.soft_hi_v = v;
            break;
        }
        case 5: {
            char *end = NULL;
            long v = strtol(value_str, &end, 10);
            if (end == value_str || *end != '\0' || v <= 0 || v > 255) {
                result = RESULT_INVALID_PARAM;
                break;
            }
            temp.debounce_polls = (UINT8)v;
            break;
        }
        case 6: {
            SystemMotionSource src;
            if (!parse_mot_src(value_str, &src)) {
                result = RESULT_INVALID_PARAM;
                break;
            }
            temp.motion_source = src;
            break;
        }
        case 7: {
            SystemSoftMotionMethod m;
            if (!parse_mot_method(value_str, &m)) {
                result = RESULT_INVALID_PARAM;
                break;
            }
            temp.soft_motion_method = m;
            break;
        }
        case 8:
        case 9: {
            char *end = NULL;
            float v = strtof(value_str, &end);
            if (end == value_str || (end && *end != '\0') || v < 0.0f) {
                result = RESULT_INVALID_PARAM;
                break;
            }
            if (idx == 8) temp.mot_spd_kmh = v;
            else temp.mot_dist_m = v;
            break;
        }
        case 10: {
            char *end = NULL;
            float v = strtof(value_str, &end);
            if (end == value_str || (end && *end != '\0') || v < 0.0f) {
                result = RESULT_INVALID_PARAM;
                break;
            }
            temp.accel_g = v;
            break;
        }
        case 11:
        case 12: {
            char *end = NULL;
            unsigned long v = strtoul(value_str, &end, 10);
            if (end == value_str || *end != '\0' || v == 0ul) {
                result = RESULT_INVALID_PARAM;
                break;
            }
            if (idx == 11) temp.accel_dur_ms = (UINT32)v;
            else temp.mot_stop_debounce_sec = (UINT32)v;
            break;
        }
        case 13:
        case 14:
        case 15:
        case 16: {
            BOOL b;
            if (!parse_bool(value_str, &b)) {
                result = RESULT_INVALID_PARAM;
                break;
            }
            if (idx == 13) temp.mot_spd_en = b;
            else if (idx == 14) temp.mot_dist_en = b;
            else if (idx == 15) temp.accel_en = b;
            else temp.mot_spd_calc = b;
            break;
        }
        default:
            break;
        }

        param_index++;
        token = utils_strtok_r(NULL, ",", &saveptr);
    }

    if (result == RESULT_SUCCESS && param_index > 0) {
        result = system_config_validate(&temp);
        if (result == RESULT_SUCCESS)
            memcpy(config, &temp, sizeof(*config));
    }

    if (result == RESULT_SUCCESS && param_index > 0) {
        /* Reference also mirrored soft thresholds into TcpConfig.ign_det_*;
         * walnut TcpConfig has no such fields (see migrate note above). */
        config_get_current();
        if (config_save_to_file(NULL) != RESULT_SUCCESS)
            LOG_WARN("Failed to save system configuration");
    }

    utils_free_tokenization(str_copy);
    return result;
}

Result system_config_get_string(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0)
        return RESULT_INVALID_PARAM;

    const SystemConfig *c = system_config_get_storage();

    /*
     * Compact CSV for SMS/cmd (fits CMD_HANDLER_MAX_RESPONSE_LENGTH 160).
     * is/ms: 0=WIRE/IGN 1=SOFT; mm: 0=GNSS 1=ACCEL 2=DIST 3=TRIP 4=ANY; *-en/sc: 0/1.
     */
    int len = snprintf(buffer, buffer_size,
                       "is:%u,wo:%.1f,wf:%.1f,sl:%.1f,sh:%.1f,d:%u,"
                       "ms:%u,mm:%u,sp:%.0f,di:%.0f,ag:%.2f,ad:%lu,st:%lu,se:%u,de:%u,ae:%u,sc:%u",
                       (unsigned)c->ign_source,
                       (double)c->wire_on_v, (double)c->wire_off_v,
                       (double)c->soft_lo_v, (double)c->soft_hi_v,
                       (unsigned)c->debounce_polls,
                       (unsigned)c->motion_source,
                       (unsigned)c->soft_motion_method,
                       (double)c->mot_spd_kmh, (double)c->mot_dist_m, (double)c->accel_g,
                       (unsigned long)c->accel_dur_ms,
                       (unsigned long)c->mot_stop_debounce_sec,
                       c->mot_spd_en ? 1u : 0u,
                       c->mot_dist_en ? 1u : 0u,
                       c->accel_en ? 1u : 0u,
                       c->mot_spd_calc ? 1u : 0u);

    if (len < 0 || (size_t)len >= buffer_size) {
        LOG_ERROR("System config string too long (need %d, got %zu)", len, buffer_size);
        return RESULT_ERROR;
    }
    return RESULT_SUCCESS;
}
