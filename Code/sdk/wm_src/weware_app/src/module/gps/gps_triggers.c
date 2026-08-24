/**
  ******************************************************************************
  * @file    gps_triggers.c
  * @author  WheelsEye
  * @brief   GPS send trigger evaluation (IGN, angle, distance; events via the
  *          latched @c should_trigger) - walnut port of the reference
  *          firmware's module/gps/gps_triggers.c. Logic is verbatim; only the
  *          per-cycle debug logging was dropped (compiled out in the
  *          reference as well).
  ******************************************************************************
  */

// sdk
#include "wm_global.h"

// app
#include "module/gps/gps_triggers.h"
#include "module/gps/gps_manager.h"
#include "common/utils.h"

extern gps_manager_runtime_t g_gps;

static BOOL gps_check_ign_trigger(int *trigger_reason);
static BOOL gps_check_angle_trigger(const GpsPacket *current_gps, int *trigger_reason);
static BOOL gps_check_distance_trigger(const GpsPacket *current_gps, int *trigger_reason);
static BOOL gps_triggers_compute_decider(int *out_reason);

static UINT32 gps_uptime_elapsed_since_last_send(void)
{
    UINT32 now = utils_get_uptime_seconds();
    UINT32 last = g_gps.last_packet_send_uptime_sec;

    if (last == 0U)
        return now;
    if (now >= last)
        return now - last;
    return UINT32_MAX - last + now + 1U;
}

/**
 * @brief IGN interval using monotonic uptime since last successful TCP send.
 * @note Do not use @c last_valid_gps_data.utc_time for intervals (RTC/NMEA skew).
 */
static BOOL gps_check_ign_trigger(int *trigger_reason)
{
    if (!g_gps.config)
        return FALSE;

    UINT32 interval = (UINT32)(g_gps.mot_status
                                   ? g_gps.config->ign_on_interval_sec
                                   : g_gps.config->ign_off_interval_sec);
    if (interval == 0U)
        interval = 1U;

    UINT32 last = g_gps.last_packet_send_uptime_sec;
    UINT32 elapsed = gps_uptime_elapsed_since_last_send();

    if (last == 0U) {
        if (elapsed < interval)
            return FALSE;
    } else if (elapsed < interval) {
        return FALSE;
    }
    if (trigger_reason)
        *trigger_reason = GPS_TRIGGER_IGN_INTERVAL;
    return TRUE;
}

/** Angle trigger: current sample vs last successfully sent packet. */
static BOOL gps_check_angle_trigger(const GpsPacket *current_gps, int *trigger_reason)
{
    if (!g_gps.config->enable_angle_trigger || !current_gps->fix_valid_calculated || !g_gps.mot_status)
        return FALSE;
    if (g_gps.last_valid_gps_data.utc_time == 0)
        return FALSE;

    float th = g_gps.config->angle_threshold_deg;
    float change = utils_calculate_angle_change(
        g_gps.last_valid_gps_data.course_deg, current_gps->course_deg);
    if (change < th)
        return FALSE;
    if (trigger_reason)
        *trigger_reason = GPS_TRIGGER_ANGLE;
    return TRUE;
}

static BOOL gps_check_distance_trigger(const GpsPacket *current_gps, int *trigger_reason)
{
    if (!g_gps.config->enable_distance_trigger || !current_gps->fix_valid_calculated ||
        (!g_gps.mot_status && g_gps.config->send_last_valid_on_ign_off_no_movement) || g_gps.last_valid_gps_data.utc_time == 0)
        return FALSE;

    float distance = utils_calculate_gps_distance(
        g_gps.last_valid_gps_data.latitude_deg,
        g_gps.last_valid_gps_data.longitude_deg,
        current_gps->latitude_deg,
        current_gps->longitude_deg);

    UINT32 ign_on_interval = (UINT32)g_gps.config->ign_on_interval_sec;
    if (ign_on_interval == 0U)
        ign_on_interval = 1U;

    if (distance < g_gps.config->distance_threshold_m ||
        gps_uptime_elapsed_since_last_send() < ign_on_interval)
        return FALSE;
    if (trigger_reason)
        *trigger_reason = GPS_TRIGGER_DISTANCE;
    return TRUE;
}

static BOOL gps_triggers_compute_decider(int *out_reason)
{
    if (!g_gps.config || !out_reason)
        return FALSE;

    const GpsPacket *current_gps = &g_gps.current_gps_data;
    int reason = 0;
    BOOL triggered = gps_check_ign_trigger(&reason)
                  || gps_check_angle_trigger(current_gps, &reason)
                  || gps_check_distance_trigger(current_gps, &reason);

    if (triggered)
        *out_reason = reason;
    return triggered;
}

BOOL gps_triggers_should_send(void)
{
    if (g_gps.should_trigger)
        return TRUE;

    int reason = 0;
    if (gps_triggers_compute_decider(&reason)) {
        g_gps.trigger_reason = reason;
        return TRUE;
    }
    return FALSE;
}

void gps_triggers_clear_after_send(void)
{
    g_gps.should_trigger = FALSE;
}
