/**
  ******************************************************************************
  * @file    tcp_config.c
  * @author  WheelsEye
  * @brief   TCP client module configuration for the weware application.
  *          Compile-time defaults plus a "key:value,key:value" config-string
  *          parser (validate-then-apply, so a bad string never half-applies).
  ******************************************************************************
  */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

// sdk
#include "wm_global.h"
#include "wm_sdk_log.h"

// app
#include "tcp/tcp.h"

/*---------------------------------------------------------------
 * Defaults
 *--------------------------------------------------------------*/
#define WEWARE_TCP_DEFAULT_SERVER_IP   "13.126.118.139"   /* Stage server */
// #define WEWARE_TCP_DEFAULT_SERVER_IP      "65.1.190.236"     /* Prod server (CG default) */
#define WEWARE_TCP_DEFAULT_SERVER_PORT    (9616U)
#define WEWARE_TCP_DEFAULT_CONN_TO_MS     (30000U)
#define WEWARE_TCP_DEFAULT_SEND_TO_MS     (5000U)
#define WEWARE_TCP_DEFAULT_RETRY_MS       (10000U)
#define WEWARE_TCP_DEFAULT_LOGIN_WITH_GPS (false)
#define WEWARE_TCP_DEFAULT_LIVE_FIRST     (true)

#define WEWARE_TCP_CONFIG_STRING_MAX      (192U)

/*---------------------------------------------------------------
 * Static State
 *--------------------------------------------------------------*/
/* Exported for the module registry (module_config.c): config_load_from_file
 * fills this directly via config_ptr before weware_tcp_init runs. */
WewareTcpConfig g_tcp_config;
static bool     g_tcp_config_initialized = false;

/*---------------------------------------------------------------
 * Validation helpers
 *--------------------------------------------------------------*/
/* Accepts dotted-decimal IPv4 or a hostname (sdk_tcp_connect_host resolves
 * names, so both are valid endpoints). */
static bool tcp_config_host_is_valid(const char *host)
{
    size_t len;
    size_t i;

    if (!host)
        return false;
    len = strlen(host);
    if (len == 0U || len >= sizeof(g_tcp_config.server_ip))
        return false;

    for (i = 0; i < len; i++) {
        char c = host[i];
        if (!isalnum((unsigned char)c) && c != '.' && c != '-')
            return false;
    }
    return true;
}

static bool tcp_config_port_is_valid(long port)
{
    return (port >= 1) && (port <= 65535);
}

/* Accepts "true"/"false" (any case) and "1"/"0" */
static bool tcp_config_parse_bool(const char *value, bool *out)
{
    if (!value || !out)
        return false;
    if (strcmp(value, "1") == 0 ||
        strcmp(value, "true") == 0 || strcmp(value, "TRUE") == 0 ||
        strcmp(value, "True") == 0) {
        *out = true;
        return true;
    }
    if (strcmp(value, "0") == 0 ||
        strcmp(value, "false") == 0 || strcmp(value, "FALSE") == 0 ||
        strcmp(value, "False") == 0) {
        *out = false;
        return true;
    }
    return false;
}

/* Positive decimal (> 0) into a UINT32 */
static bool tcp_config_parse_timeout(const char *value, UINT32 *out)
{
    char         *end   = NULL;
    unsigned long parsed;

    if (!value || !out || *value == '\0')
        return false;
    parsed = strtoul(value, &end, 10);
    if (end == value || *end != '\0' || parsed == 0UL || parsed > 0xFFFFFFFFUL)
        return false;
    *out = (UINT32)parsed;
    return true;
}

static char *tcp_config_trim(char *str)
{
    char *end;

    while (*str != '\0' && isspace((unsigned char)*str))
        str++;
    end = str + strlen(str);
    while (end > str && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return str;
}

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/
void weware_tcp_config_get_defaults(WewareTcpConfig *config)
{
    if (!config)
        return;

    /* Defaults applied to the live storage count as initialization, so a
     * later weware_tcp_config_get() won't clobber values that
     * config_load_from_file loaded on top of these defaults. */
    if (config == &g_tcp_config)
        g_tcp_config_initialized = true;

    memset(config, 0, sizeof(*config));
    strcpy(config->server_ip, WEWARE_TCP_DEFAULT_SERVER_IP);
    config->server_port           = WEWARE_TCP_DEFAULT_SERVER_PORT;
    config->connection_timeout_ms = WEWARE_TCP_DEFAULT_CONN_TO_MS;
    config->send_timeout_ms       = WEWARE_TCP_DEFAULT_SEND_TO_MS;
    config->retry_interval_ms     = WEWARE_TCP_DEFAULT_RETRY_MS;
    config->login_with_gps        = WEWARE_TCP_DEFAULT_LOGIN_WITH_GPS;
    config->live_first            = WEWARE_TCP_DEFAULT_LIVE_FIRST;
}

WewareTcpConfig *weware_tcp_config_get(void)
{
    if (!g_tcp_config_initialized) {
        weware_tcp_config_get_defaults(&g_tcp_config);
        g_tcp_config_initialized = true;
        wm_sdk_log_info("TCP default config initialized (%s:%u)",
                     g_tcp_config.server_ip, (unsigned)g_tcp_config.server_port);
    }
    return &g_tcp_config;
}

wm_SdkResult weware_tcp_config_validate(const WewareTcpConfig *config)
{
    if (!config)
        return WM_SDK_RESULT_INVALID_PARAM;

    if (!tcp_config_host_is_valid(config->server_ip)) {
        wm_sdk_log_error("TCP invalid server: %s", config->server_ip);
        return WM_SDK_RESULT_INVALID_PARAM;
    }
    if (!tcp_config_port_is_valid((long)config->server_port)) {
        wm_sdk_log_error("TCP invalid port: %u", (unsigned)config->server_port);
        return WM_SDK_RESULT_INVALID_PARAM;
    }
    if (config->connection_timeout_ms == 0U ||
        config->send_timeout_ms == 0U ||
        config->retry_interval_ms == 0U) {
        wm_sdk_log_error("TCP invalid timeouts: conn=%lu send=%lu retry=%lu",
                      (unsigned long)config->connection_timeout_ms,
                      (unsigned long)config->send_timeout_ms,
                      (unsigned long)config->retry_interval_ms);
        return WM_SDK_RESULT_INVALID_PARAM;
    }
    return WM_SDK_RESULT_SUCCESS;
}

wm_SdkResult weware_tcp_config_set_server(const char *server_ip, UINT16 server_port)
{
    WewareTcpConfig *config = weware_tcp_config_get();

    if (!tcp_config_host_is_valid(server_ip)) {
        wm_sdk_log_error("TCP invalid server: %s", server_ip ? server_ip : "NULL");
        return WM_SDK_RESULT_INVALID_PARAM;
    }
    if (!tcp_config_port_is_valid((long)server_port)) {
        wm_sdk_log_error("TCP invalid port: %u", (unsigned)server_port);
        return WM_SDK_RESULT_INVALID_PARAM;
    }

    strcpy(config->server_ip, server_ip);
    config->server_port = server_port;

    wm_sdk_log_info("TCP server updated: %s:%u",
                 config->server_ip, (unsigned)config->server_port);

    weware_tcp_reset_connection();
    return WM_SDK_RESULT_SUCCESS;
}

wm_SdkResult weware_tcp_config_set(const char *config_string)
{
    WewareTcpConfig *config = weware_tcp_config_get();
    WewareTcpConfig  staged;
    char             work[WEWARE_TCP_CONFIG_STRING_MAX];
    char            *token;
    char            *next;

    if (!config_string) {
        wm_sdk_log_error("TCP config string is NULL");
        return WM_SDK_RESULT_INVALID_PARAM;
    }
    if (strlen(config_string) >= sizeof(work)) {
        wm_sdk_log_error("TCP config string too long");
        return WM_SDK_RESULT_INVALID_PARAM;
    }

    /* Stage on a copy: every value validates before any is applied */
    staged = *config;
    strcpy(work, config_string);

    for (token = work; token != NULL; token = next) {
        char *value;
        char *key;

        next = strchr(token, ',');
        if (next != NULL)
            *next++ = '\0';

        key = tcp_config_trim(token);
        if (*key == '\0')
            continue;

        value = strchr(key, ':');
        if (value == NULL) {
            wm_sdk_log_error("TCP config token has no value: %s", key);
            return WM_SDK_RESULT_INVALID_PARAM;
        }
        *value++ = '\0';
        key   = tcp_config_trim(key);
        value = tcp_config_trim(value);

        if (strcmp(key, "ip") == 0) {
            if (!tcp_config_host_is_valid(value)) {
                wm_sdk_log_error("TCP invalid server: %s", value);
                return WM_SDK_RESULT_INVALID_PARAM;
            }
            strcpy(staged.server_ip, value);
        } else if (strcmp(key, "port") == 0) {
            long port = atol(value);
            if (!tcp_config_port_is_valid(port)) {
                wm_sdk_log_error("TCP invalid port: %s (must be 1-65535)", value);
                return WM_SDK_RESULT_INVALID_PARAM;
            }
            staged.server_port = (UINT16)port;
        } else if (strcmp(key, "conn-to") == 0) {
            if (!tcp_config_parse_timeout(value, &staged.connection_timeout_ms)) {
                wm_sdk_log_error("TCP invalid conn-to: %s (must be > 0)", value);
                return WM_SDK_RESULT_INVALID_PARAM;
            }
        } else if (strcmp(key, "send-to") == 0) {
            if (!tcp_config_parse_timeout(value, &staged.send_timeout_ms)) {
                wm_sdk_log_error("TCP invalid send-to: %s (must be > 0)", value);
                return WM_SDK_RESULT_INVALID_PARAM;
            }
        } else if (strcmp(key, "retry") == 0) {
            if (!tcp_config_parse_timeout(value, &staged.retry_interval_ms)) {
                wm_sdk_log_error("TCP invalid retry: %s (must be > 0)", value);
                return WM_SDK_RESULT_INVALID_PARAM;
            }
        } else if (strcmp(key, "loginwithgps") == 0) {
            if (!tcp_config_parse_bool(value, &staged.login_with_gps)) {
                wm_sdk_log_error("TCP invalid loginwithgps: %s (use true or false)", value);
                return WM_SDK_RESULT_INVALID_PARAM;
            }
        } else if (strcmp(key, "live-first") == 0) {
            if (!tcp_config_parse_bool(value, &staged.live_first)) {
                wm_sdk_log_error("TCP invalid live-first: %s (use true or false)", value);
                return WM_SDK_RESULT_INVALID_PARAM;
            }
        } else {
            wm_sdk_log_error("TCP unknown config key: %s", key);
            return WM_SDK_RESULT_INVALID_PARAM;
        }
    }

    if (weware_tcp_config_validate(&staged) != WM_SDK_RESULT_SUCCESS)
        return WM_SDK_RESULT_INVALID_PARAM;

    *config = staged;
    wm_sdk_log_info("TCP configuration updated");

    weware_tcp_reset_connection();
    return WM_SDK_RESULT_SUCCESS;
}

wm_SdkResult weware_tcp_config_get_string(char *buffer, size_t buffer_size)
{
    const WewareTcpConfig *config = weware_tcp_config_get();
    int                    len;

    if (!buffer || buffer_size == 0U)
        return WM_SDK_RESULT_INVALID_PARAM;

    len = snprintf(buffer, buffer_size,
                   "ip:%s,port:%u,conn-to:%lu,send-to:%lu,retry:%lu,"
                   "loginwithgps:%s,live-first:%s",
                   config->server_ip,
                   (unsigned)config->server_port,
                   (unsigned long)config->connection_timeout_ms,
                   (unsigned long)config->send_timeout_ms,
                   (unsigned long)config->retry_interval_ms,
                   config->login_with_gps ? "true" : "false",
                   config->live_first ? "true" : "false");

    if (len < 0 || (size_t)len >= buffer_size) {
        wm_sdk_log_error("TCP config string buffer too small");
        return WM_SDK_RESULT_ERROR;
    }
    return WM_SDK_RESULT_SUCCESS;
}
