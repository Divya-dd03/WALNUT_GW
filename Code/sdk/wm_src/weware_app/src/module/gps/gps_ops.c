/**
  ******************************************************************************
  * @file    gps_ops.c
  * @author  WheelsEye
  * @brief   GPS operations: GNSS config, query/parse, fix state, packet build,
  *          validation - walnut port of the reference firmware's
  *          module/gps/gps_ops.c. Send triggers: gps_triggers.c.
  *
  *          Walnut adaptations (business logic unchanged):
  *          - nav data source mirrors the reference URC build exactly: paired
  *            RMC+GGA records from the kernel NMEA callback (gps_manager.c
  *            feed) are parsed for position + HDOP + RMC/GGA 200 m
  *            cross-check; an empty NMEA queue counts as a parse failure and
  *            feeds the reference's fail-streak escalation. The pre-NMEA
  *            wm_sdk_gps_get_navdata poll (validated on-target, no HDOP) is kept
  *            as a compiled-out backup - build with -DGPS_QUERY_NAVDATA_POLL
  *            to revert (mirrors the reference's #if SDK_URC_GNSS_MASK split).
  *          - GSM time source: wm_sdk_network_rtc_get_utc_time (NITZ-synced RTC).
  *          - A-GPS / info-period are NOT_SUPPORTED stubs on this receiver.
  *          - EVENT_* broadcasts are logged only (event manager not ported);
  *            EVENT_RESET_SOFT maps to wm_sdk_system_reboot().
  *
  * Last valid position persistence: gps_storage.c / GPS_LAST_VALID_FILE_PATH.
  ******************************************************************************
  */

#include <string.h>
#include <stdlib.h>

// sdk
#include "wm_global.h"
#include "wm_sdk_os.h"
#include "wm_sdk_log.h"
#include "wm_sdk_gps.h"
#include "wm_sdk_network.h"
#include "wm_sdk_system.h"

// app
#include "module/gps/gps_ops.h"
#include "module/gps/gps_manager.h"
#include "common/event_manager.h"
#include "module/gps/gps_config.h"
#include "module/gps/gps_packet.h"
#include "module/gps/gps_storage.h"
#include "module/gps/gps_urc_queue_types.h"
#include "module/network/network.h"
#include "module/tcp/tcp.h"
#include "module/module_manager.h"
#include "common/queue_manager.h"
#include "common/utils.h"

/* ============================================================================
 * File scope
 * ============================================================================ */
extern gps_manager_runtime_t g_gps;

extern GpsConfig g_gps_config;

/** Reset in @c gps_ops_set_runtime_defaults so the configured announcement can fire again after deinit. */
static BOOL s_gps_configured_event_sent = FALSE;

/** Consecutive @c gps_ops_query_and_parse failures before soft reset (1 Hz GPS task). */
#define GPS_PARSE_FAIL_SOFT_RESET_COUNT  60u
#define GPS_PARSE_FAIL_RESET_SOURCE      "GPS issue"

/** NMEA streaming but no usable fix for this many samples (~5 min @1 Hz). */
#define GPS_NO_FIX_LOG_COUNT             300u
/** GNSS config (NMEA/mode/rate/start/info) not completing after this many powered
 * cycles. Normal config finishes in a handful of cycles. */
#define GPS_GNSS_CONFIG_FAIL_CYCLES      60u

/** Reject position jumps vs last accepted fix when implied speed exceeds this (km/h). */
#define GPS_IMPLAUSIBLE_SPEED_KMH_MAX       500.0f
/** After this many consecutive over-max samples, accept the current position anyway. */
#define GPS_IMPLAUSIBLE_SPEED_STREAK_ACCEPT 5u
#define GPS_IMPLAUSIBLE_SPEED_MIN_DT_SEC    1u

static UINT32 s_implausible_speed_streak = 0u;

static UINT32 s_parse_fail_streak      = 0u;
static BOOL   s_parse_fail_reset_sent  = FALSE;
static UINT32 s_last_gnss_power_status_check_uptime = 0u;
static BOOL   s_cached_gnss_power_on = FALSE;

static BOOL s_pwr_nmea_setup_done = FALSE;

/** One-shot guard so a wedged GNSS engine logs the power-on failure once,
 * not every task cycle. Cleared on a successful power-on. */
static BOOL s_power_on_err_logged = FALSE;

/** No-fix watchdog and malformed-fix guard. */
static UINT32 s_no_fix_streak         = 0u;
static BOOL   s_no_fix_logged         = FALSE;
static BOOL   s_fix_coords_bad_logged = FALSE;
/** GNSS config-completion watchdog. */
static UINT32 s_gnss_cfg_attempts     = 0u;
static BOOL   s_gnss_cfg_err_logged   = FALSE;
/** One-shot log for the walnut A-GPS NOT_SUPPORTED stub. */
static BOOL   s_agps_not_supported_logged = FALSE;

UINT32 gps_ops_pop_gps_msg_queue_latch_append(char *buf, UINT32 buf_size)
{
    /* TODO(gps): reference pops the GPS module msg_q here (BLE payloads for
     * send-with-GPS, destination TCP) and latches
     * GPS_TRIGGER_MSG_Q_TCP_APPEND. Module message queues are not ported. */
    (void)buf;
    (void)buf_size;
    return 0u;
}

static BOOL gps_ops_is_valid_utc(UINT32 utc_time)
{
    return (utc_time >= GPS_TIME_VALID_MIN_UTC_UNIX) ? TRUE : FALSE;
}

/** Last sent / accepted position for implausible-jump checks (never @c current_gps_data). */
static const GpsPacket *gps_ops_get_jump_filter_reference(void)
{
    if (g_gps.last_valid_gps_data.utc_time != 0u &&
        gps_validate_coordinates(g_gps.last_valid_gps_data.latitude_deg,
                                 g_gps.last_valid_gps_data.longitude_deg)) {
        return &g_gps.last_valid_gps_data;
    }
    return NULL;
}

/**
 * @brief Drop random lat/lon jumps vs last sent @c last_valid_gps_data.
 * @note @a out is the freshly parsed sample (usually @c current_gps_data); do not
 *       compare it to current - that buffer is already overwritten before this runs.
 */
static void gps_ops_filter_implausible_jump(GpsPacket *out)
{
    const GpsPacket *ref;

    if (!out)
        return;

    if (!gps_validate_coordinates(out->latitude_deg, out->longitude_deg))
        return;

    ref = gps_ops_get_jump_filter_reference();
    if (!ref || ref == out) {
        s_implausible_speed_streak = 0u;
        return;
    }

    if (!gps_ops_is_valid_utc(out->utc_time) || !gps_ops_is_valid_utc(ref->utc_time)) {
        s_implausible_speed_streak = 0u;
        return;
    }

    UINT32 t_cur = out->utc_time;
    UINT32 t_last = ref->utc_time;
    if (t_cur <= t_last) {
        s_implausible_speed_streak = 0u;
        return;
    }

    UINT32 dt_sec = t_cur - t_last;
    if (dt_sec < GPS_IMPLAUSIBLE_SPEED_MIN_DT_SEC)
        dt_sec = GPS_IMPLAUSIBLE_SPEED_MIN_DT_SEC;

    float dist_m = utils_calculate_gps_distance(
        ref->latitude_deg, ref->longitude_deg,
        out->latitude_deg, out->longitude_deg);

    float speed_kmh = (dist_m * 3.6f) / (float)dt_sec;

    if (speed_kmh <= GPS_IMPLAUSIBLE_SPEED_KMH_MAX) {
        s_implausible_speed_streak = 0u;
        return;
    }

    s_implausible_speed_streak++;
    wm_sdk_log_warning("[gps-parse] implausible speed %d km/h (dist=%dm dt=%us streak=%u)",
                    (int)speed_kmh, (int)dist_m, (unsigned)dt_sec,
                    (unsigned)s_implausible_speed_streak);

    if (s_implausible_speed_streak < GPS_IMPLAUSIBLE_SPEED_STREAK_ACCEPT) {
        memcpy(out, ref, sizeof(GpsPacket));
        out->utc_time = t_cur;
        return;
    }

    wm_sdk_log_error("[gps-parse] accepting position after %u implausible-speed samples (glitch/spoof?)",
                  (unsigned)GPS_IMPLAUSIBLE_SPEED_STREAK_ACCEPT);
    s_implausible_speed_streak = 0u;
}

static BOOL gps_ops_get_gsm_utc_time(UINT32 *out_utc_time)
{
    wm_SdkNetworkTime rtc;

    if (!out_utc_time) {
        wm_sdk_log_warning("gsm utc: null out");
        return FALSE;
    }

    memset(&rtc, 0, sizeof(rtc));
    if (wm_sdk_network_rtc_get_utc_time(&rtc) != WM_SDK_RESULT_SUCCESS) {
        wm_sdk_log_warning("gsm utc: rtc_get_utc_time failed");
        return FALSE;
    }

    *out_utc_time = utils_time_to_unix(&rtc);

    if (!gps_ops_is_valid_utc(*out_utc_time)) {
        return FALSE;
    }

    return TRUE;
}

void gps_ops_apply_time_source_to_packet(GpsPacket *p)
{
    if (!p)
        return;

    GpsTimeSource source = GPS_TIME_SOURCE_AUTO;
    if (g_gps.config)
        source = g_gps.config->time_source;

    UINT32 gsm_utc = 0;
    UINT32 gps_utc = p->utc_time;
    BOOL gps_valid_utc = gps_ops_is_valid_utc(gps_utc);
    BOOL gsm_valid_utc = gps_ops_get_gsm_utc_time(&gsm_utc);

    switch (source)
    {
    case GPS_TIME_SOURCE_GPS:
        if (!gps_valid_utc && gsm_valid_utc) { p->utc_time = gsm_utc; }
        break;

    case GPS_TIME_SOURCE_GSM:
        if (gps_valid_utc && !gsm_valid_utc) { break; }
        p->utc_time = gsm_utc;
        break;

    case GPS_TIME_SOURCE_AUTO:
    default:
        if (p->fix_valid_real && gps_valid_utc) {
            break;
        }
        if (gsm_valid_utc) {
            p->utc_time = gsm_utc;
            break;
        }
        if (gps_valid_utc) {
            break;
        }
        p->utc_time = gsm_utc;
        break;
    }
}

/* ============================================================================
 * GNSS configuration
 * ============================================================================ */
/** @return TRUE when NMEA output is configured. */
static BOOL gps_ops_configure_nmea_output(void)
{
    wm_SdkResult nr;

    if (g_gps.gnss_nmea_output_configured)
        return TRUE;

    /* Walnut: both data-get modes behave the same (SDK owns the receiver
     * UART and parses NMEA internally; location read via navdata). */
    nr = wm_sdk_gps_enable_nmea_output(GPS_WALNUT_NMEA_DATA_GET_MODE);
    if (nr == WM_SDK_RESULT_SUCCESS) {
        g_gps.gnss_nmea_output_configured = TRUE;
        wm_sdk_log_info("GNSS NMEA output configured; navdata should refresh");
        return TRUE;
    }
    if (nr == WM_SDK_RESULT_NOT_SUPPORTED) {
        g_gps.gnss_nmea_output_configured = TRUE;
        return TRUE;
    }

    wm_sdk_log_warning("GNSS NMEA output configure failed (%d), retry next cycle", (int)nr);
    return FALSE;
}

/** One config step per retry cycle when NMEA is not yet configured. */
static BOOL gps_ops_try_start_nmea_output(void)
{
    if (g_gps.gnss_nmea_output_configured)
        return FALSE;

    (void)gps_ops_configure_nmea_output();
    return TRUE;
}

BOOL gps_ops_power_on_and_configure_nmea_step(void)
{
    if (s_pwr_nmea_setup_done)
        return TRUE;

    wm_sdk_task_sleep(GPS_GNSS_POWER_ON_PRE_DELAY_MS);

    if (wm_sdk_gps_set_power_status(1) != WM_SDK_RESULT_SUCCESS) {
        if (!s_power_on_err_logged) {
            wm_sdk_log_error("GNSS power on failed");
            s_power_on_err_logged = TRUE;
        }
        return FALSE;
    }
    s_power_on_err_logged = FALSE;

    wm_sdk_task_sleep(GPS_GNSS_POWER_STABILIZE_MS);

    if (!gps_ops_configure_nmea_output())
        return FALSE;

    s_pwr_nmea_setup_done = TRUE;
    wm_sdk_log_info("GNSS power and NMEA ready");
    return TRUE;
}

#define GPS_POWER_STATUS_CHECK_GAP_SEC  5u

/**
 * Proceed only when runtime NMEA stream is active OR the SDK reports GNSS
 * power on. To avoid hammering the SDK power-status API, poll at most once
 * every 5 seconds.
 */
static BOOL gps_ops_can_proceed_config(void)
{
    if (g_gps.gnss_nmea_output_started)
        return TRUE;

    {
        UINT32 now = utils_get_uptime_seconds();
        if (s_last_gnss_power_status_check_uptime == 0u ||
            (now - s_last_gnss_power_status_check_uptime) >= GPS_POWER_STATUS_CHECK_GAP_SEC) {
            UINT8 power_on = 0;
            if (wm_sdk_gps_get_power_status(&power_on) == WM_SDK_RESULT_SUCCESS)
                s_cached_gnss_power_on = power_on ? TRUE : FALSE;
            else
                s_cached_gnss_power_on = FALSE;
            s_last_gnss_power_status_check_uptime = now;
        }
    }

    return s_cached_gnss_power_on;
}

void gps_ops_retry_configuration(void)
{
    /* Broadcast EVENT_GPS_CONFIGURED once when core config is done: mode, NMEA rate, NMEA output, start mode. */
    {
        BOOL all_configured = g_gps.gnss_mode_set && g_gps.gnss_nmea_rate_set &&
                            g_gps.gnss_nmea_output_configured && g_gps.gnss_start_mode_set &&
                            g_gps.gnss_info_report_set;

        if (!s_gps_configured_event_sent && all_configured) {
            s_gps_configured_event_sent = TRUE;
            event_manager_broadcast(EVENT_GPS_CONFIGURED, "GPS", NULL, 0);
            wm_sdk_log_info("GPS configured (all config done)");
        }

        if (all_configured) {
            s_gnss_cfg_attempts   = 0u;
            s_gnss_cfg_err_logged = FALSE;
            return;
        }
    }

    if (!gps_ops_can_proceed_config())
        return;

    /* GNSS is powered but config still isn't complete. If it never completes, a
     * config step (mode/rate/start/info set) is persistently failing - log once. */
    if (!s_gnss_cfg_err_logged && ++s_gnss_cfg_attempts >= GPS_GNSS_CONFIG_FAIL_CYCLES) {
        wm_sdk_log_error("GNSS config stuck: nmea=%d mode=%d rate=%d start=%d info=%d",
                      g_gps.gnss_nmea_output_configured ? 1 : 0, g_gps.gnss_mode_set ? 1 : 0,
                      g_gps.gnss_nmea_rate_set ? 1 : 0, g_gps.gnss_start_mode_set ? 1 : 0,
                      g_gps.gnss_info_report_set ? 1 : 0);
        s_gnss_cfg_err_logged = TRUE;
    }

    if (gps_ops_try_start_nmea_output())
        return;

    /* Set GPS mode */
    if (!g_gps.gnss_mode_set) {
        UINT32 target_mode = WM_SDK_GPS_SYS_GPS | WM_SDK_GPS_SYS_GLO;  /* reference: GPS+GLONASS */
        if (wm_sdk_gps_set_mode(target_mode) == WM_SDK_RESULT_SUCCESS) {
            g_gps.gnss_mode_set = TRUE;
            wm_sdk_log_info("GNSS mode set (0x%x)", (unsigned)target_mode);
        }
        return;
    }

    /* Set GPS NMEA rate */
    if (!g_gps.gnss_nmea_rate_set) {
        if (wm_sdk_gps_set_nmea_rate(GPS_NMEA_RATE_DEFAULT) == WM_SDK_RESULT_SUCCESS) {
            g_gps.gnss_nmea_rate_set = TRUE;
            wm_sdk_log_info("GNSS nmea rate set (%d)Hz", GPS_NMEA_RATE_DEFAULT);
        }
        return;
    }

    if (!g_gps.gnss_start_mode_set && g_gps.config) {
        UINT32 start_mode;

        if (!gps_post_boot_is_power_on_reset()) {
            start_mode = SDK_GNSS_START_HOT;
            wm_sdk_log_info("GNSS start mode HOT (%s)", gps_post_boot_reset_reason_string());
        } else {
            start_mode = g_gps.config->start_mode;
            if (start_mode != SDK_GNSS_START_HOT && start_mode != SDK_GNSS_START_WARM && start_mode != SDK_GNSS_START_COLD) {
                start_mode = SDK_GNSS_START_WARM;
            }
            wm_sdk_log_info("GNSS start mode %u (power-on, config)", (unsigned)start_mode);
        }

        if (wm_sdk_gps_start_mode(start_mode) == WM_SDK_RESULT_SUCCESS)
            g_gps.gnss_start_mode_set = TRUE;

        return;
    }

    if (!g_gps.gnss_info_report_set) {
        wm_SdkResult ir = wm_sdk_gps_set_gnss_info_period(GPS_GNSS_INFO_PERIOD_S);
        g_gps.gnss_info_report_set = TRUE;
        if (ir == WM_SDK_RESULT_SUCCESS)
            wm_sdk_log_info("GNSS info periodic report set (%us)", (unsigned)GPS_GNSS_INFO_PERIOD_S);
        else if (ir != WM_SDK_RESULT_NOT_SUPPORTED)
            wm_sdk_log_warning("GNSS info period set failed (%d)", (int)ir);
        return;
    }
}

void gps_ops_open_agps_if_needed(void)
{
    if (!g_gps.config || !g_gps.config->enable_agps)
        return;
    /* Reference also gates on command_config_adoc_net_task_blocked();
     * command module not ported. TODO(gps) */
    if (!weware_network_is_connected())
        return;
    if (!weware_network_is_stable())
        return;
    /* Open A-GPS only after core GNSS config (mode, NMEA rate, start mode all set) */
    if (!g_gps.gnss_mode_set || !g_gps.gnss_nmea_rate_set || !g_gps.gnss_nmea_output_configured || !g_gps.gnss_start_mode_set)
        return;
    if (g_gps.gnss_agps_set)
        return;
    /* Cooldown after a failed open attempt */
    UINT32 uptime = utils_get_uptime_seconds();
    if (g_gps.last_agps_open_attempt_time != 0 &&
        (uptime - g_gps.last_agps_open_attempt_time) < GPS_AGPS_FAIL_COOLDOWN_SEC)
        return;
    wm_SdkResult ret = wm_sdk_gps_open_agps_service();
    if (ret == WM_SDK_RESULT_SUCCESS) {
        g_gps.gnss_agps_set = TRUE;
        g_gps.last_agps_open_time = utils_get_uptime_seconds();
        g_gps.last_agps_open_attempt_time = 0;
        wm_sdk_log_info("A-GPS service opened");
        return;
    }
    if (ret == WM_SDK_RESULT_NOT_SUPPORTED) {
        /* Walnut: the on-board receiver has no assistance-server path.
         * Mark as done so the reference retry/cooldown loop does not spin. */
        g_gps.gnss_agps_set = TRUE;
        if (!s_agps_not_supported_logged) {
            wm_sdk_log_info("A-GPS not supported on this receiver (skipped)");
            s_agps_not_supported_logged = TRUE;
        }
        return;
    }
    g_gps.last_agps_open_attempt_time = utils_get_uptime_seconds();
    wm_sdk_log_error("A-GPS open failed, reason: %d; cooldown %u s",
                  (int)ret, (unsigned)GPS_AGPS_FAIL_COOLDOWN_SEC);
}

/** When no fix and A-GPS was opened >=4 h ago, clear gnss_agps_set for re-open (data valid 4 h only). */
void gps_ops_agps_refresh_if_needed(void)
{
    if (!g_gps.config || !g_gps.config->enable_agps || g_gps.gnss_agps_set == FALSE)
        return;
    if (g_gps.current_gps_data.fix_valid_calculated)
        return;
    if (g_gps.last_agps_open_time == 0)
        return;
    UINT32 uptime = utils_get_uptime_seconds();
    if ((uptime - g_gps.last_agps_open_time) < GPS_AGPS_VALIDITY_SEC)
        return;
    g_gps.gnss_agps_set = FALSE;
    wm_sdk_log_info("A-GPS refresh: no fix for %u h, clearing for re-open",
                 (unsigned)((uptime - g_gps.last_agps_open_time) / 3600u));
}

/* ============================================================================
 * NMEA parse family (reference: SIMCOM nav-data-from-URC block, verbatim
 * except: talker check accepts $GN and $GP - the reference is GN-only
 * because its receiver always ran multi-GNSS; stall recovery re-registers
 * the kernel NMEA callback instead of re-enabling NMEA-by-URC).
 * Guarded like the reference (#if SDK_URC_GNSS_MASK): compiled out when the
 * navdata backup build is selected.
 * ============================================================================ */

#if !defined(GPS_QUERY_NAVDATA_POLL)

#define GPS_NMEA_RMC_GGA_MAX_DIST_M  200.0f
/** Task cycles with an empty NMEA queue before re-registering the callback. */
#define GPS_NMEA_STALL_CYCLES        20u

static void nmea_strip_checksum(char *line)
{
    char *star = strchr(line, '*');
    if (star)
        *star = '\0';
}

static int nmea_split_fields(char *payload, char **tok, int maxtok)
{
    int n = 0;
    if (!payload || maxtok < 1)
        return 0;
    tok[n++] = payload;
    for (char *p = payload; *p && n < maxtok; ++p) {
        if (*p == ',') {
            *p = '\0';
            tok[n++] = p + 1;
        }
    }
    return n;
}

static int nmea_parse_dec2(const char *s)
{
    if (!s || s[0] < '0' || s[0] > '9' || s[1] < '0' || s[1] > '9')
        return -1;
    return (s[0] - '0') * 10 + (s[1] - '0');
}

static BOOL nmea_is_leap(int year)
{
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

static int nmea_days_in_month(int year, int month)
{
    static const int md[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > 12)
        return 0;
    if (month == 2 && nmea_is_leap(year))
        return 29;
    return md[month - 1];
}

static UINT32 nmea_rmc_time_date_to_unix(const char *hhmmss, const char *ddmmyy)
{
    static const int month_days_before[] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };

    if (!hhmmss || !ddmmyy || hhmmss[0] == '\0' || ddmmyy[0] == '\0')
        return 0;

    int day = nmea_parse_dec2(ddmmyy);
    int month = nmea_parse_dec2(ddmmyy + 2);
    int year2 = nmea_parse_dec2(ddmmyy + 4);
    int hour = nmea_parse_dec2(hhmmss);
    int minute = nmea_parse_dec2(hhmmss + 2);
    int second = nmea_parse_dec2(hhmmss + 4);

    if (day < 0 || month < 0 || year2 < 0 || hour < 0 || minute < 0 || second < 0)
        return 0;

    int year = year2 + (year2 >= 70 ? 1900 : 2000);
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > nmea_days_in_month(year, month))
        return 0;

    UINT64 days = 0;
    for (int y = 1970; y < year; ++y)
        days += nmea_is_leap(y) ? 366ULL : 365ULL;
    days += (UINT64)month_days_before[month - 1];
    if (month > 2 && nmea_is_leap(year))
        days += 1ULL;
    days += (UINT64)(day - 1);

    UINT64 ts = days * 86400ULL + (UINT64)hour * 3600ULL + (UINT64)minute * 60ULL + (UINT64)second;
    return (ts <= (UINT64)0xFFFFFFFFu) ? (UINT32)ts : 0;
}

/** GGA field 8 = MSL altitude (m); field 9 = M (meters). Reject misaligned / bogus values. */
static BOOL nmea_parse_gga_msl_altitude(int ng, char **g_fields, float *out_m)
{
    if (!out_m || ng < 9 || !g_fields[8] || g_fields[8][0] == '\0')
        return FALSE;
    if (ng >= 10 && g_fields[9][0] != '\0' && g_fields[9][0] != 'M' && g_fields[9][0] != 'm')
        return FALSE;

    float m = (float)atof(g_fields[8]);
    if (m < -450.f || m > 10000.f)
        return FALSE;

    *out_m = m;
    return TRUE;
}

static BOOL nmea_parse_latlon(const char *ddmm, const char *hemi, BOOL is_lat, double *out_deg)
{
    if (!out_deg)
        return FALSE;
    *out_deg = 0.0;

    if (!ddmm || !hemi || ddmm[0] == '\0' || hemi[0] == '\0')
        return FALSE;

    double v = atof(ddmm);
    if (v < 0.0)
        return FALSE;

    int ideg = (int)(v / 100.0);
    double minutes = v - (double)ideg * 100.0;
    if (minutes < 0.0 || minutes >= 60.0)
        return FALSE;

    if (is_lat) {
        if (ideg > 90)
            return FALSE;
        if (hemi[0] != 'N' && hemi[0] != 'n' && hemi[0] != 'S' && hemi[0] != 's')
            return FALSE;
    } else {
        if (ideg > 180)
            return FALSE;
        if (hemi[0] != 'E' && hemi[0] != 'e' && hemi[0] != 'W' && hemi[0] != 'w')
            return FALSE;
    }

    double deg = (double)ideg + minutes / 60.0;
    if (is_lat) {
        if (hemi[0] == 'S' || hemi[0] == 's')
            deg = -deg;
    } else {
        if (hemi[0] == 'W' || hemi[0] == 'w')
            deg = -deg;
    }

    *out_deg = deg;
    return TRUE;
}

static BOOL nmea_coords_from_sentence(BOOL active,
                                      const char *lat_ddmm, const char *lat_hemi,
                                      const char *lon_ddmm, const char *lon_hemi,
                                      double *out_lat, double *out_lon)
{
    double lat = 0.0;
    double lon = 0.0;

    if (!active || !out_lat || !out_lon)
        return FALSE;
    if (!nmea_parse_latlon(lat_ddmm, lat_hemi, TRUE, &lat))
        return FALSE;
    if (!nmea_parse_latlon(lon_ddmm, lon_hemi, FALSE, &lon))
        return FALSE;
    if (!gps_validate_coordinates(lat, lon))
        return FALSE;

    *out_lat = lat;
    *out_lon = lon;
    return TRUE;
}

/**
 * @brief Resolve lat/lon from RMC and GGA; both must agree when both are active.
 * @return TRUE when trusted coordinates are written to @a out_lat / @a out_lon.
 */
static BOOL nmea_resolve_latlon(BOOL rmc_ok,
                                const char *rmc_lat, const char *rmc_ns,
                                const char *rmc_lon, const char *rmc_ew,
                                int gga_qual,
                                const char *gga_lat, const char *gga_ns,
                                const char *gga_lon, const char *gga_ew,
                                double *out_lat, double *out_lon)
{
    double rmc_lat_deg = 0.0;
    double rmc_lon_deg = 0.0;
    double gga_lat_deg = 0.0;
    double gga_lon_deg = 0.0;
    BOOL have_rmc;
    BOOL have_gga;
    float dist_m;

    if (!out_lat || !out_lon)
        return FALSE;

    have_rmc = nmea_coords_from_sentence(rmc_ok, rmc_lat, rmc_ns, rmc_lon, rmc_ew,
                                         &rmc_lat_deg, &rmc_lon_deg);
    have_gga = nmea_coords_from_sentence(gga_qual > 0, gga_lat, gga_ns, gga_lon, gga_ew,
                                         &gga_lat_deg, &gga_lon_deg);

    if (have_rmc && have_gga) {
        dist_m = utils_calculate_gps_distance(rmc_lat_deg, rmc_lon_deg, gga_lat_deg, gga_lon_deg);
        if (dist_m > GPS_NMEA_RMC_GGA_MAX_DIST_M) {
            wm_sdk_log_warning("[nmea] RMC/GGA mismatch dist=%.1fm rmc=%.6f,%.6f gga=%.6f,%.6f",
                            (double)dist_m, rmc_lat_deg, rmc_lon_deg, gga_lat_deg, gga_lon_deg);
            return FALSE;
        }
        *out_lat = gga_lat_deg;
        *out_lon = gga_lon_deg;
        return TRUE;
    }

    if (have_gga) {
        *out_lat = gga_lat_deg;
        *out_lon = gga_lon_deg;
        return TRUE;
    }

    if (have_rmc) {
        *out_lat = rmc_lat_deg;
        *out_lon = rmc_lon_deg;
        return TRUE;
    }

    return FALSE;
}

/** Line starts "$GN<type>," or "$GP<type>," (walnut: talker depends on constellation mode). */
static BOOL nmea_line_is(const char *line, const char *type3)
{
    if (!line || line[0] != '$' || line[1] != 'G' ||
        (line[2] != 'N' && line[2] != 'P'))
        return FALSE;
    return strncmp(line + 3, type3, 3) == 0 && line[6] == ',';
}

/** Parse combined buffer "$GNRMC,...\\n$GNGGA,..." into GpsPacket. Returns 1 on success. */
static int gps_ops_parse_gnrmc_gngga_combined(char *combined, GpsPacket *out)
{
    for (char *q = combined; *q; ++q) {
        if (*q == '\r')
            *q = '\n';
    }

    char *nl = strchr(combined, '\n');
    if (!nl)
        return 0;
    *nl = '\0';
    char *line_a = combined;
    char *line_b = nl + 1;
    while (*line_b == '\n' || *line_b == '\r')
        line_b++;

    char *rmc = NULL;
    char *gga = NULL;
    if (nmea_line_is(line_a, "RMC"))
        rmc = line_a;
    else if (nmea_line_is(line_a, "GGA"))
        gga = line_a;
    if (nmea_line_is(line_b, "RMC"))
        rmc = line_b;
    else if (nmea_line_is(line_b, "GGA"))
        gga = line_b;

    if (!rmc || !gga)
        return 0;

    {
        char *x = strchr(rmc, '\n');
        if (x) *x = '\0';
    }
    {
        char *x = strchr(gga, '\n');
        if (x) *x = '\0';
    }

    nmea_strip_checksum(rmc);
    nmea_strip_checksum(gga);

    char *comma_r = strchr(rmc, ',');
    char *comma_g = strchr(gga, ',');
    if (!comma_r || !comma_g)
        return 0;

    char *r_fields[16];
    char *g_fields[16];
    int nr = nmea_split_fields(comma_r + 1, r_fields, 16);
    int ng = nmea_split_fields(comma_g + 1, g_fields, 16);
    if (nr < 9 || ng < 9)
        return 0;

    const char *rmc_time = r_fields[0];
    const char *rmc_status = r_fields[1];
    const char *rmc_lat = r_fields[2];
    const char *rmc_ns = r_fields[3];
    const char *rmc_lon = r_fields[4];
    const char *rmc_ew = r_fields[5];
    const char *rmc_sog = r_fields[6];
    const char *rmc_cog = r_fields[7];
    const char *rmc_date = r_fields[8];

    const char *gga_lat = g_fields[1];
    const char *gga_ns = g_fields[2];
    const char *gga_lon = g_fields[3];
    const char *gga_ew = g_fields[4];
    const char *gga_qual = g_fields[5];
    const char *gga_sats = g_fields[6];
    const char *gga_hdop = g_fields[7];

    utils_memset_safe(out, sizeof(GpsPacket), 0, sizeof(GpsPacket));

    BOOL rmc_ok = (rmc_status[0] == 'A');
    int qual = atoi(gga_qual);
    BOOL fix_indicated = (rmc_ok || qual > 0) ? TRUE : FALSE;

    double lat = 0.0, lon = 0.0;
    if (!nmea_resolve_latlon(rmc_ok, rmc_lat, rmc_ns, rmc_lon, rmc_ew,
                             qual, gga_lat, gga_ns, gga_lon, gga_ew, &lat, &lon)) {
        if (fix_indicated) {
            wm_sdk_log_warning("[nmea] fix indicated but coordinates rejected (rmc=%d qual=%d)",
                            rmc_ok ? 1 : 0, qual);
            return 0;
        }
    }

    out->latitude_deg = lat;
    out->longitude_deg = lon;
    out->fix_valid_real = fix_indicated ? 1 : 0;
    out->fix_valid_calculated = 0;

    out->utc_time = nmea_rmc_time_date_to_unix(rmc_time, rmc_date);

    if (rmc_sog[0] != '\0')
        out->speed_knots = (float)atof(rmc_sog);
    if (rmc_cog[0] != '\0') {
        float c = (float)atof(rmc_cog);
        if (c >= 0.f && c <= 360.f)
            out->course_deg = c;
    }

    {
        float alt_m = 0.f;
        if (nmea_parse_gga_msl_altitude(ng, g_fields, &alt_m))
            out->altitude_m = alt_m;
    }

    if (gga_sats[0] != '\0')
        out->sats_in_use = (UINT8)atoi(gga_sats);

    if (gga_hdop[0] != '\0') {
        float h = (float)atof(gga_hdop);
        if (h > 0.0f && h < 100.0f)
            out->hdop_x100 = (UINT16)(h * 100.0f);
    }

    wm_sdk_log_info("[nmea] fix_real=%d lat=%.6f lon=%.6f kts=%.2f sats=%u hdop=%u utc=%u\r\n",
                    out->fix_valid_real, out->latitude_deg, out->longitude_deg,
                    (double)out->speed_knots, (unsigned)out->sats_in_use,
                    (unsigned)out->hdop_x100, (unsigned)out->utc_time);

    return 1;
}

/** Drain the NMEA queue, parse the newest pair. Returns 1 on a parsed sample. */
static int gps_ops_query_and_parse_from_urc_queue(GpsPacket *out)
{
    Module *gps_mod = module_manager_get_module(MODULE_ID_GPS);
    static UINT8 s_no_gps_nmea_count = 0;
    gps_urc_queued_t last;
    BOOL             have = FALSE;

    if (!gps_mod || !gps_mod->config.urc_q)
        return 0;

    for (;;) {
        gps_urc_queued_t el;
        UINT32           popped = 0;
        if (queue_pop(gps_mod->config.urc_q, &gps_mod->config.urc_q_config,
                      &el, 1U, &popped) != RESULT_SUCCESS || popped == 0U)
            break;
        if (el.combined[0] != '\0') {
            last = el;
            have = TRUE;
        }
    }

    /* Stall recovery (reference re-enables NMEA-by-URC): re-register the
     * kernel NMEA callback when the feed has been silent too long. */
    if (s_no_gps_nmea_count > GPS_NMEA_STALL_CYCLES) {
        (void)gps_manager_nmea_feed_register();
        s_no_gps_nmea_count = 0;
    }

    if (!have) {
        s_no_gps_nmea_count++;
        return 0;
    }
    s_no_gps_nmea_count = 0;

    char scratch[GPS_URC_COMBINED_MAX];
    if (utils_strncpy_safe(scratch, last.combined, sizeof(scratch)) < 0)
        return 0;

    if (!gps_ops_parse_gnrmc_gngga_combined(scratch, out))
        return 0;

    BOOL coordinates_valid = gps_validate_coordinates(out->latitude_deg, out->longitude_deg);
    if (out->fix_valid_real && !coordinates_valid)
        wm_sdk_log_warning("[gps_ops] NMEA fix but invalid coords (lat=%.6f, lon=%.6f)",
                        out->latitude_deg, out->longitude_deg);

    if (!coordinates_valid) {
        out->latitude_deg = 0.0;
        out->longitude_deg = 0.0;
        out->speed_knots = 0.0f;
    }

    return 1;
}

#endif /* !GPS_QUERY_NAVDATA_POLL */

/* ============================================================================
 * BACKUP source - compiled out (reference structured/navdata path).
 * The pre-NMEA walnut source, validated on-target; carries no HDOP and no
 * RMC/GGA cross-check. Build with -DGPS_QUERY_NAVDATA_POLL to use it instead
 * of the NMEA queue (mirrors the reference's #if SDK_URC_GNSS_MASK split).
 * ============================================================================ */

#if defined(GPS_QUERY_NAVDATA_POLL)
static int gps_ops_query_and_parse_navdata(GpsPacket *out)
{
    wm_SdkGpsNavData nav_data;
    wm_SdkResult r;

    memset(&nav_data, 0, sizeof(nav_data));
    r = wm_sdk_gps_get_navdata(&nav_data);
    /* Walnut: BUSY = receiver running, no valid fix yet - the sample is still
     * populated and fix_valid says so. Only real failures count as the
     * reference's "get_navdata failed". */
    if (r != WM_SDK_RESULT_SUCCESS && r != WM_SDK_RESULT_BUSY) {
        wm_sdk_log_error("GPS get_navdata failed\r\n");
        return 0;
    }

    memset(out, 0, sizeof(*out));
    out->fix_valid_real = nav_data.fix_valid ? 1 : 0;
    out->fix_valid_calculated = 0;
    out->latitude_deg = nav_data.latitude;
    out->longitude_deg = nav_data.longitude;

    BOOL coordinates_valid = gps_validate_coordinates(out->latitude_deg, out->longitude_deg);
    if (out->fix_valid_real && !coordinates_valid && !s_fix_coords_bad_logged) {
        wm_sdk_log_error("[gps_ops] GPS reports fix but coordinates invalid");
        s_fix_coords_bad_logged = TRUE;
    }

    if (!coordinates_valid)
    {
        out->latitude_deg = 0.0;
        out->longitude_deg = 0.0;
        out->speed_knots = 0.0;
    }

    out->altitude_m = nav_data.altitude_m;
    out->speed_knots = nav_data.speed_kmh / GPS_KMH_TO_KNOTS;
    out->course_deg = nav_data.course_deg;
    out->sats_in_use = nav_data.satellites;

    out->hdop_x100 = 0;     /* structured navdata carries no HDOP (reference same) */
    out->utc_time = utils_time_to_unix(&nav_data.utc);

    return 1;
}
#endif /* GPS_QUERY_NAVDATA_POLL */

/* ============================================================================
 * Query and parse (reference structure: one source, chosen at compile time)
 * ============================================================================ */

int gps_ops_query_and_parse(GpsPacket *out)
{
    int rc = 0;

    if (!out)
        return 0;

#if defined(GPS_QUERY_NAVDATA_POLL)
    rc = gps_ops_query_and_parse_navdata(out);
#else
    /* Paired RMC+GGA from the kernel NMEA callback (HDOP + cross-check).
     * Empty queue = parse failure, exactly like the reference URC build -
     * the fail streak in gps_ops_refresh_current_sample escalates it. */
    rc = gps_ops_query_and_parse_from_urc_queue(out);
#endif

    if (rc == 1)
        gps_ops_filter_implausible_jump(out);
    return rc;
}

void gps_ops_update_fix_valid_calculated(GpsPacket *p)
{
    if (!p)
        return;
    p->fix_valid_calculated = gps_validate_coordinates(p->latitude_deg, p->longitude_deg) ? 1 : 0;
}

/* ============================================================================
 * Fix state and trigger
 * ============================================================================ */

static BOOL gps_ops_has_valid_fix(void)
{
    return g_gps.current_gps_data.fix_valid_real ? TRUE : FALSE;
}

static void gps_ops_loc_storage_on_edge(BOOL active_on)
{
    if (!gps_ops_has_valid_fix())
        return;
    if (active_on)
        (void)gps_storage_clear();
    else
        (void)gps_storage_save_packet(&g_gps.current_gps_data);
}

void gps_ops_loc_storage_update_on_motion_changed(BOOL mot_on)
{
    gps_ops_loc_storage_on_edge(mot_on);
}

GpsFixTransition gps_ops_process_fix_transition(void)
{
    BOOL has_fix = gps_ops_has_valid_fix();
    static BOOL had_fix = FALSE;

    if (!had_fix && has_fix) {
        had_fix = TRUE;
        wm_sdk_log_info("GPS fix acquired");
        return GPS_FIX_TRANSITION_CONNECTED;
    }
    if (had_fix && !has_fix) {
        had_fix = FALSE;
        wm_sdk_log_info("GPS fix lost");
        return GPS_FIX_TRANSITION_DISCONNECTED;
    }
    had_fix = has_fix;
    return GPS_FIX_TRANSITION_NONE;
}

void gps_ops_refresh_current_sample(void)
{
    if (gps_ops_query_and_parse(&g_gps.current_gps_data) != 1) {
        g_gps.gnss_nmea_output_started = FALSE;
        if (!s_parse_fail_reset_sent)
            wm_sdk_log_warning("GPS query and parse failed; clearing current sample");
        memset(&g_gps.current_gps_data, 0, sizeof(g_gps.current_gps_data));

        if (!s_parse_fail_reset_sent) {
            s_parse_fail_streak++;
            if (s_parse_fail_streak >= GPS_PARSE_FAIL_SOFT_RESET_COUNT) {
                wm_sdk_log_error("GPS parse failed %u consecutive times; soft reset (%s)",
                              (unsigned)GPS_PARSE_FAIL_SOFT_RESET_COUNT,
                              GPS_PARSE_FAIL_RESET_SOURCE);
                s_parse_fail_reset_sent = TRUE;
                /* Reference broadcasts EVENT_RESET_SOFT; walnut: graceful reboot. */
                wm_sdk_system_reboot();
            }
        }
    } else {
        g_gps.gnss_nmea_output_started = TRUE;
        s_parse_fail_streak     = 0u;
        s_parse_fail_reset_sent = FALSE;
    }
    gps_ops_apply_time_source_to_packet(&g_gps.current_gps_data);
    gps_ops_update_fix_valid_calculated(&g_gps.current_gps_data);

    /* No-fix watchdog: nav data is flowing but we hold no usable fix
     * (antenna/sky). Logged once after a sustained streak; cleared on the
     * first valid fix (which also re-arms the malformed-fix guard). */
    if (g_gps.gnss_nmea_output_started) {
        if (g_gps.current_gps_data.fix_valid_calculated) {
            s_no_fix_streak         = 0u;
            s_no_fix_logged         = FALSE;
            s_fix_coords_bad_logged = FALSE;
        } else if (!s_no_fix_logged && ++s_no_fix_streak >= GPS_NO_FIX_LOG_COUNT) {
            wm_sdk_log_error("No GPS fix for %u samples despite nav data (antenna/sky?)",
                          (unsigned)GPS_NO_FIX_LOG_COUNT);
            s_no_fix_logged = TRUE;
        }
    }
}

void gps_ops_handle_fix_transition(GpsFixTransition ev)
{
    switch (ev) {
    case GPS_FIX_TRANSITION_CONNECTED:
        g_gps.should_trigger = TRUE;
        g_gps.trigger_reason = GPS_TRIGGER_GPS_FIX;
        if (!g_gps.mot_status)
            (void)gps_storage_save_packet(&g_gps.current_gps_data);
        else
            (void)gps_storage_clear();
        update_last_valid_gps_data(&g_gps.current_gps_data);
        /* Reference broadcasts EVENT_GPS_CONNECTED (event manager TODO). */
        break;

    case GPS_FIX_TRANSITION_DISCONNECTED:
        (void)gps_storage_save_last_valid();
        /* Reference broadcasts EVENT_GPS_DISCONNECTED (event manager TODO). */
        break;

    default:
        break;
    }
}

/* ============================================================================
 * Packet build (shared by GPS task and TCP login-with-GPS)
 * ============================================================================ */

void update_last_valid_gps_data(GpsPacket *pkt)
{
    if (!pkt)
        return;
    memcpy(&g_gps.last_valid_gps_data, pkt, sizeof(GpsPacket));
    g_gps.last_valid_gps_data.speed_knots = 0.0f;
}

float gps_ops_calc_speed_kmh_from_last_sent(void)
{
    const GpsPacket *cur = &g_gps.current_gps_data;
    const GpsPacket *last = &g_gps.last_valid_gps_data;
    UINT32 last_send = g_gps.last_packet_send_uptime_sec;
    UINT32 now;
    UINT32 dt_sec;
    float dist_m;

    if (!cur->fix_valid_calculated || last->utc_time == 0u || last_send == 0u)
        return 0.0f;
    if (!gps_validate_coordinates(last->latitude_deg, last->longitude_deg))
        return 0.0f;

    now = utils_get_uptime_seconds();
    dt_sec = (now >= last_send) ? (now - last_send) : 0u;
    if (dt_sec == 0u)
        dt_sec = GPS_IMPLAUSIBLE_SPEED_MIN_DT_SEC;

    dist_m = utils_calculate_gps_distance(
        last->latitude_deg, last->longitude_deg,
        cur->latitude_deg, cur->longitude_deg);
    return (dist_m * 3.6f) / (float)dt_sec;
}

/** Min km/h for SOG backfill; matches @c speed_filter_kmh when enabled. */
static float gps_ops_speed_backfill_min_kmh(void)
{
    if (g_gps.config && g_gps.config->speed_filter_kmh > 0.0f)
        return g_gps.config->speed_filter_kmh;
    return GPS_DEFAULT_SPEED_FILTER_KMH;
}

/** When reported SOG is missing/zero, derive speed from last sent position if above min threshold. */
static void gps_ops_backfill_speed_from_motion(GpsPacket *pkt)
{
    float speed_kmh;
    float min_kmh;

    if (!pkt || pkt->speed_knots > 0.0f || !pkt->fix_valid_calculated)
        return;

    speed_kmh = gps_ops_calc_speed_kmh_from_last_sent();
    if (speed_kmh <= 0.0f)
        return;

    min_kmh = gps_ops_speed_backfill_min_kmh();
    if (speed_kmh < min_kmh)
        return;

    pkt->speed_knots = speed_kmh / GPS_KMH_TO_KNOTS;
}

static BOOL gps_ops_speed_filter_send_last_valid(void)
{
    float speed_kmh;
    float threshold;

    if (!g_gps.config || g_gps.config->speed_filter_kmh <= 0.0f)
        return FALSE;

    speed_kmh = gps_ops_calc_speed_kmh_from_last_sent();
    if (speed_kmh <= 0.0f)
        return FALSE;

    threshold = gps_ops_speed_backfill_min_kmh();
    return speed_kmh < threshold;
}

GpsPacketBuildKind gps_ops_pick_send_kind(void)
{
    /* Reference reads PowerInfo.charge_connected; walnut: runtime latch. */
    BOOL charge_on = g_gps.charge_status;
    BOOL cur_fix_valid = g_gps.current_gps_data.fix_valid_calculated;

    if ((!charge_on && cur_fix_valid) || g_gps.trigger_reason == GPS_TRIGGER_DISTANCE)
        return GPS_PACKET_BUILD_CURRENT;

    if (g_gps.config &&
        ((g_gps.config->send_last_valid_on_no_fix && !cur_fix_valid) ||
         (g_gps.config->send_last_valid_on_ign_off_no_movement && !g_gps.mot_status)))
        return GPS_PACKET_BUILD_LAST_VALID_MERGED;

    if (gps_ops_speed_filter_send_last_valid())
        return GPS_PACKET_BUILD_LAST_VALID_MERGED;

    return GPS_PACKET_BUILD_CURRENT;
}

int gps_ops_build_position_packet(char *buffer, int buffer_size, GpsPacketBuildKind kind)
{
    if (!buffer || buffer_size < GPS_PACKET_TOTAL_SIZE)
        return 0;

    switch (kind) {
    case GPS_PACKET_BUILD_CURRENT:
        g_gps.current_gps_data.sats_in_use =
            (g_gps.current_gps_data.sats_in_use < GPS_LAST_VALID_MIN_SATS &&
             g_gps.current_gps_data.fix_valid_calculated)
                ? GPS_LAST_VALID_MIN_SATS
                : g_gps.last_valid_gps_data.sats_in_use;
        gps_ops_backfill_speed_from_motion(&g_gps.current_gps_data);
        return gps_packet_create(buffer, buffer_size, &g_gps.current_gps_data);

    case GPS_PACKET_BUILD_LAST_VALID_MERGED:
        g_gps.last_valid_gps_data.fix_valid_real = g_gps.current_gps_data.fix_valid_real;
        g_gps.last_valid_gps_data.speed_knots = 0.0f;
        g_gps.last_valid_gps_data.sats_in_use =
            (g_gps.last_valid_gps_data.sats_in_use < GPS_LAST_VALID_MIN_SATS &&
             g_gps.last_valid_gps_data.fix_valid_calculated)
                ? GPS_LAST_VALID_MIN_SATS
                : g_gps.last_valid_gps_data.sats_in_use;
        g_gps.last_valid_gps_data.utc_time = g_gps.current_gps_data.utc_time;
        return gps_packet_create(buffer, buffer_size, &g_gps.last_valid_gps_data);

    default:
        return 0;
    }
}

int gps_ops_build_send_packet(char *buffer, int buffer_size, GpsPacketBuildKind kind)
{
    int len = gps_ops_build_position_packet(buffer, buffer_size, kind);
    if (len == GPS_PACKET_TOTAL_SIZE && kind == GPS_PACKET_BUILD_CURRENT)
        update_last_valid_gps_data(&g_gps.current_gps_data);
    return len;
}

UINT32 gps_ops_merge_tcp_append(char *msg_buf, UINT32 gps_len,
                                const char *append, UINT32 append_len)
{
    if (!msg_buf || gps_len == 0u)
        return gps_len;
    if (!append || append_len == 0u)
        return gps_len;
    if (gps_len + append_len < MODULE_MESSAGE_INLINE_SIZE) {
        memcpy(msg_buf + gps_len, append, append_len);
        return gps_len + append_len;
    }
    return gps_len;
}

wm_SdkResult gps_ops_push_tcp_position_message(const char *data, UINT32 len)
{
    const ModuleConfig *tcp_config;

    if (!data || len == 0u || len > MODULE_MESSAGE_INLINE_SIZE)
        return WM_SDK_RESULT_ERROR;

    /* Reference parity: push a ModuleMessage row straight into the TCP send
     * queue (TCP_SEND_Q) - absorbs packets while TCP is disconnected; rows
     * are popped only after the server ACK and persist across soft reboot. */
    tcp_config = module_manager_get_config(MODULE_ID_TCP);
    if (!tcp_config || !tcp_config->msg_q || tcp_config->msg_q_config.element_size == 0u) {
        g_gps.packets_dropped++;
        wm_sdk_log_warning("GPS queue push failed: TCP send queue unavailable (dropped=%u)",
                        (unsigned)g_gps.packets_dropped);
        return WM_SDK_RESULT_ERROR;
    }

    ModuleMessage module_msg = {0};
    module_msg.source_module      = MODULE_ID_GPS;
    module_msg.destination_module = MODULE_ID_TCP;
    module_msg.use_dynamic_buffer = FALSE;
    memcpy(module_msg.message, data, len);
    module_msg.data_len = len;

    Result qr = queue_push(tcp_config->msg_q, &tcp_config->msg_q_config, &module_msg);
    if (qr != RESULT_SUCCESS) {
        g_gps.packets_dropped++;
        wm_sdk_log_warning("GPS queue push failed (%d, dropped=%u)",
                        (int)qr, (unsigned)g_gps.packets_dropped);
        return WM_SDK_RESULT_ERROR;
    }

    g_gps.last_packet_send_uptime_sec = utils_get_uptime_seconds();
    return WM_SDK_RESULT_SUCCESS;
}

int gps_ops_build_last_valid_position_for_tcp(char *buffer, int buffer_size)
{
    return gps_packet_create(buffer, buffer_size, &g_gps.last_valid_gps_data);
}

/* ============================================================================
 * Validation
 * ============================================================================ */

/**
 * @brief Validate GPS coordinates
 * @param lat Latitude in degrees
 * @param lon Longitude in degrees
 * @return TRUE if coordinates are valid, FALSE otherwise
 */
BOOL gps_validate_coordinates(double lat, double lon)
{
    /* Check for NaN (Not a Number) */
    if (lat != lat || lon != lon)  /* NaN comparison: NaN != NaN is always true */
    {
        return FALSE;
    }

    /* Check for infinity */
    if (lat == (1.0/0.0) || lat == (-1.0/0.0) || lon == (1.0/0.0) || lon == (-1.0/0.0))
    {
        return FALSE;
    }

    /* Check if coordinates are within valid ranges */
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0)
    {
        return FALSE;
    }

    /* Check if coordinates are exactly zero (invalid) */
    if (lat == 0.0 && lon == 0.0)
    {
        return FALSE;
    }

    return TRUE;
}

/* ============================================================================
 * Runtime reset (called from gps_manager init/deinit)
 * ============================================================================ */

void gps_ops_set_runtime_defaults(void)
{
    g_gps.task_ref = NULL;
    g_gps.config = &g_gps_config;
    g_gps.ign_status = FALSE;
    g_gps.mot_status = FALSE;
    g_gps.charge_status = FALSE;
    g_gps.trigger_reason = 0;
    memset(&g_gps.current_gps_data, 0, sizeof(g_gps.current_gps_data));
    memset(&g_gps.last_valid_gps_data, 0, sizeof(g_gps.last_valid_gps_data));
    g_gps.should_trigger = FALSE;
    g_gps.last_packet_send_uptime_sec = 0;
    g_gps.packets_dropped = 0;
    g_gps.gnss_mode_set = FALSE;
    g_gps.gnss_nmea_rate_set = FALSE;
    g_gps.gnss_nmea_output_configured = FALSE;
    g_gps.gnss_nmea_output_started = FALSE;
    g_gps.gnss_info_report_set = FALSE;
    g_gps.gnss_start_mode_set = FALSE;
    g_gps.gnss_agps_set = FALSE;
    g_gps.last_agps_open_time = 0;
    g_gps.last_agps_open_attempt_time = 0;
    s_gps_configured_event_sent = FALSE;
    s_parse_fail_streak = 0u;
    s_parse_fail_reset_sent = FALSE;
    s_last_gnss_power_status_check_uptime = 0u;
    s_cached_gnss_power_on = FALSE;
    s_pwr_nmea_setup_done = FALSE;
    s_power_on_err_logged = FALSE;
    s_implausible_speed_streak = 0u;
    s_no_fix_streak = 0u;
    s_no_fix_logged = FALSE;
    s_fix_coords_bad_logged = FALSE;
    s_gnss_cfg_attempts = 0u;
    s_gnss_cfg_err_logged = FALSE;
    s_agps_not_supported_logged = FALSE;
}
