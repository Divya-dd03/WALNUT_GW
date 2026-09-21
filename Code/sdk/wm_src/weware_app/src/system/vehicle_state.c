/**
 * @file vehicle_state.c
 * @brief Ignition and motion detection with debouncing and events - walnut port.
 *
 * Walnut adaptations vs the reference:
 * - No accelerometer driver exists yet (TODO(accel): STK8321 needs a vendor
 *   i2cc_* driver); eval_soft_accel() reports FALSE and
 *   vehicle_state_accel_tick() is a no-op. accel_en defaults FALSE
 *   (system_config.c) so SOFT/ANY motion uses GNSS speed/distance.
 * - The walnut GPS manager takes ignition/motion through explicit latch
 *   setters (reference reads PowerInfo directly), so debounced edges are
 *   forwarded to gps_manager_set_ignition_status()/set_motion_status().
 */

#include "system/vehicle_state.h"
#include "system/system_config.h"
#include "system/system_manager.h"
#include "common/event_manager.h"
#include "common/utils.h"
#include "module/gps/gps_manager.h"
#include "module/gps/gps_config.h"
#include "module/gps/gps_ops.h"

/*---------------------------------------------------------------
 * Log Configuration
 *--------------------------------------------------------------*/
#define LOG_TAG          "VEH_STATE"
#define LOG_MODULE_LEVEL LOG_LEVEL_DEBUG
#include "module/log/log.h"

extern gps_manager_runtime_t g_gps;

/*---------------------------------------------------------------
 * Static State
 *--------------------------------------------------------------*/
static UINT8  s_ign_debounce_count;
static BOOL   s_motion_raw_on;
static UINT32 s_motion_stop_since_uptime;
static float  s_anchor_lat;
static float  s_anchor_lon;
static BOOL   s_anchor_valid;
static BOOL   s_prev_ign;
static BOOL   s_prev_mot;
static BOOL   s_prev_raw_ign;
static BOOL   s_prev_raw_mot;

/*---------------------------------------------------------------
 * Ignition evaluation
 *--------------------------------------------------------------*/

static float soft_ign_base_voltage(const PowerInfo *pi)
{
    float divisor = (pi->external_voltage > 40.0f) ? 4.2f :
                    (pi->external_voltage > 20.0f) ? 2.1f : 1.0f;
    return pi->external_voltage / divisor;
}

static BOOL eval_ignition_wire(const SystemConfig *cfg, const PowerInfo *pi)
{
    if (pi->input_wire_voltage >= cfg->wire_on_v)
        return TRUE;
    if (pi->input_wire_voltage <= cfg->wire_off_v)
        return FALSE;
    return pi->ignition_on;
}

static BOOL eval_ignition_soft(const SystemConfig *cfg, const PowerInfo *pi)
{
    float ign_base = soft_ign_base_voltage(pi);
    return (ign_base > cfg->soft_lo_v && ign_base <= cfg->soft_hi_v);
}

/*---------------------------------------------------------------
 * Soft motion evaluation
 *--------------------------------------------------------------*/

static BOOL eval_soft_gnss_speed(const SystemConfig *cfg)
{
    float speed_kmh;

    if (!g_gps.current_gps_data.fix_valid_calculated)
        return FALSE;

    if (cfg->mot_spd_calc)
        speed_kmh = gps_ops_calc_speed_kmh_from_last_sent();
    else
        speed_kmh = g_gps.current_gps_data.speed_knots * GPS_KMH_TO_KNOTS;

    return speed_kmh >= cfg->mot_spd_kmh;
}

static BOOL eval_soft_gps_dist(const SystemConfig *cfg)
{
    if (!g_gps.current_gps_data.fix_valid_calculated || !s_anchor_valid)
        return FALSE;

    float dist = utils_calculate_gps_distance(
        s_anchor_lat, s_anchor_lon,
        g_gps.current_gps_data.latitude_deg,
        g_gps.current_gps_data.longitude_deg);
    return dist >= cfg->mot_dist_m;
}

/* TODO(accel): reference reads accel_manager_is_moving() (STK8321 variance);
 * no walnut accelerometer driver yet, so accel never reports motion. */
static BOOL eval_soft_accel(void)
{
    return FALSE;
}

static BOOL eval_soft_motion_raw(const SystemConfig *cfg)
{
    BOOL spd = FALSE;
    BOOL dist = FALSE;
    BOOL accel = FALSE;

    switch (cfg->soft_motion_method) {
    case SYSTEM_SOFT_MOT_GNSS_SPEED:
        return eval_soft_gnss_speed(cfg);
    case SYSTEM_SOFT_MOT_GPS_DIST:
        return eval_soft_gps_dist(cfg);
    case SYSTEM_SOFT_MOT_ACCEL:
        return eval_soft_accel();
    case SYSTEM_SOFT_MOT_TRIP:
        return eval_soft_accel() && eval_soft_gps_dist(cfg);
    case SYSTEM_SOFT_MOT_ANY:
    default:
        if (cfg->mot_spd_en)
            spd = eval_soft_gnss_speed(cfg);
        if (cfg->mot_dist_en)
            dist = eval_soft_gps_dist(cfg);
        if (cfg->accel_en)
            accel = eval_soft_accel();
        return spd || dist || accel;
    }
}

static BOOL eval_motion_raw(const SystemConfig *cfg, BOOL ignition_on)
{
    if (cfg->motion_source == SYSTEM_MOT_SOURCE_IGN)
        return ignition_on;
    return eval_soft_motion_raw(cfg);
}

/*---------------------------------------------------------------
 * Debounce, anchor, and accel gating
 *--------------------------------------------------------------*/

static BOOL motion_needs_accel_poll(const SystemConfig *cfg)
{
    if (cfg->motion_source != SYSTEM_MOT_SOURCE_SOFT)
        return FALSE;
    if (cfg->soft_motion_method == SYSTEM_SOFT_MOT_ACCEL ||
        cfg->soft_motion_method == SYSTEM_SOFT_MOT_TRIP)
        return TRUE;
    if (cfg->soft_motion_method == SYSTEM_SOFT_MOT_ANY && cfg->accel_en)
        return TRUE;
    return FALSE;
}

static BOOL apply_motion_stop_debounce(const SystemConfig *cfg, BOOL raw_on)
{
    UINT32 now = utils_get_uptime_seconds();
    static BOOL stop_hold_logged;

    if (raw_on) {
        s_motion_stop_since_uptime = 0u;
        stop_hold_logged = FALSE;
        return TRUE;
    }

    if (s_motion_stop_since_uptime == 0u)
        s_motion_stop_since_uptime = now;

    if (now >= s_motion_stop_since_uptime) {
        UINT32 elapsed = now - s_motion_stop_since_uptime;
        if (elapsed < cfg->mot_stop_debounce_sec) {
            if (!stop_hold_logged) {
                LOG_DEBUG("Holding motion on during stop debounce");
                stop_hold_logged = TRUE;
            }
            return s_motion_raw_on;
        }
    }
    return FALSE;
}

static void sync_motion_anchor(BOOL motion_on)
{
    if (motion_on) {
        if (s_anchor_valid)
            LOG_DEBUG("GPS anchor cleared");
        s_anchor_valid = FALSE;
        return;
    }

    if (!s_anchor_valid && g_gps.current_gps_data.fix_valid_calculated) {
        s_anchor_lat = g_gps.current_gps_data.latitude_deg;
        s_anchor_lon = g_gps.current_gps_data.longitude_deg;
        s_anchor_valid = TRUE;
        LOG_DEBUG("GPS anchor set");
    }
}

/*---------------------------------------------------------------
 * Poll helpers
 *--------------------------------------------------------------*/

static void poll_ignition(PowerInfo *pi, const SystemConfig *cfg, BOOL adc_poll_done)
{
    if (!adc_poll_done)
        return;

    BOOL raw_ign = (cfg->ign_source == SYSTEM_IGN_SOURCE_WIRE) ?
                   eval_ignition_wire(cfg, pi) :
                   eval_ignition_soft(cfg, pi);

    if (raw_ign) {
        if (s_ign_debounce_count < cfg->debounce_polls)
            s_ign_debounce_count++;
    } else {
        s_ign_debounce_count = 0u;
    }

    pi->ignition_on = raw_ign && (s_ign_debounce_count >= cfg->debounce_polls);

    if (pi->adc_poll_count < 255u)
        pi->adc_poll_count++;
    pi->post_boot_ignition_ready = (pi->adc_poll_count > cfg->debounce_polls);

    if (raw_ign != s_prev_raw_ign || pi->ignition_on != s_prev_ign) {
        LOG_DEBUG("Ignition raw=%d debounced=%d",
                  raw_ign ? 1 : 0, pi->ignition_on ? 1 : 0);
        s_prev_raw_ign = raw_ign;
    }
}

static void poll_motion(PowerInfo *pi, const SystemConfig *cfg, BOOL adc_poll_done)
{
    BOOL raw_mot = eval_motion_raw(cfg, pi->ignition_on);

    pi->motion_on = apply_motion_stop_debounce(cfg, raw_mot);
    s_motion_raw_on = pi->motion_on;
    sync_motion_anchor(pi->motion_on);

    if (adc_poll_done || raw_mot != s_prev_raw_mot || raw_mot != pi->motion_on) {
        LOG_DEBUG("Motion raw=%d debounced=%d anchor=%d",
                  raw_mot ? 1 : 0, pi->motion_on ? 1 : 0, s_anchor_valid ? 1 : 0);
        s_prev_raw_mot = raw_mot;
    }
}

static void emit_state_events(PowerInfo *pi)
{
    if (pi->ignition_on != s_prev_ign) {
        LOG_INFO("Ignition turned %s", pi->ignition_on ? "ON" : "OFF");
        s_prev_ign = pi->ignition_on;
        event_manager_broadcast(pi->ignition_on ? EVENT_IGN_ON : EVENT_IGN_OFF,
                                "vehicle_state", NULL, 0);
        /* Walnut: GPS trigger latch input (reference reads PowerInfo). */
        gps_manager_set_ignition_status(pi->ignition_on);
    }

    if (pi->motion_on != s_prev_mot) {
        LOG_INFO("Motion turned %s", pi->motion_on ? "ON" : "OFF");
        s_prev_mot = pi->motion_on;
        event_manager_broadcast(pi->motion_on ? EVENT_MOTION_ON : EVENT_MOTION_OFF,
                                "vehicle_state", NULL, 0);
        gps_manager_set_motion_status(pi->motion_on);
    }
}

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/

void vehicle_state_poll(BOOL adc_poll_done)
{
    SystemConfig *cfg = system_config_get_storage();
    PowerInfo *pi = (PowerInfo *)system_manager_get_power_info();
    if (!cfg || !pi)
        return;

    poll_ignition(pi, cfg, adc_poll_done);
    poll_motion(pi, cfg, adc_poll_done);

    pi->last_update_timestamp = SDK_GET_TICKS();
    emit_state_events(pi);
}

void vehicle_state_accel_tick(void)
{
    /* TODO(accel): reference runs accel_manager_motion_poll(accel_g, window)
     * here; no walnut accelerometer driver yet. */
}

/*---------------------------------------------------------------
 * Init (last — bottom-up entry)
 *--------------------------------------------------------------*/

void vehicle_state_init(void)
{
    s_ign_debounce_count = 0u;
    s_motion_raw_on = FALSE;
    s_motion_stop_since_uptime = 0u;
    s_anchor_valid = FALSE;
    s_prev_ign = FALSE;
    s_prev_mot = FALSE;
    s_prev_raw_ign = FALSE;
    s_prev_raw_mot = FALSE;
    (void)motion_needs_accel_poll; /* kept for reference parity until accel is ported */
    LOG_INFO("Vehicle state module ready");
}
