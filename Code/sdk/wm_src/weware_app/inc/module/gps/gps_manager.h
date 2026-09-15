/**
  ******************************************************************************
  * @file    gps_manager.h
  * @author  WheelsEye
  * @brief   GPS Manager - GNSS task; walnut port of the reference firmware's
  *          module/gps/gps_manager.h. Runtime state and task loop live here;
  *          operations in gps_ops, send decisions in gps_triggers.
  *
  *          Walnut adaptations vs reference:
  *          - ignition / motion / charge inputs come from public setters
  *            (reference: event manager + PowerInfo; power/accel subsystems
  *            are not ported yet - defaults ign=ON, motion=ON, charge=OFF).
  *          - SIM presence is polled from the weware SIM module each task
  *            cycle (reference: EVENT_SIM_AVAILABLE/UNAVAILABLE events).
  ******************************************************************************
  */

#ifndef WEWARE_GPS_MANAGER_H
#define WEWARE_GPS_MANAGER_H

#include "wm_sdk_types.h"
#include "module/gps/gps_config.h"
#include "module/gps/gps_packet.h"
#include "common/task_stats.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Runtime state - only fields that vary at runtime (constants in gps_config.h) */
typedef struct
{
    void *task_ref;
    TaskStats task_stats;   /**< Stack usage sampling (reference layout) */

    GpsConfig *config;
    BOOL ign_status;
    BOOL mot_status;
    /** Walnut addition: charge-connected latch (reference reads PowerInfo). */
    BOOL charge_status;
    int trigger_reason;
    GpsPacket current_gps_data;
    /** Last sent or post-boot restored position; used for angle/distance triggers and last-valid sends. */
    GpsPacket last_valid_gps_data;
    BOOL should_trigger;
    /** Uptime (s) at last successful TCP send; IGN interval uses this, not @c last_valid_gps_data.utc_time. */
    UINT32 last_packet_send_uptime_sec;

    unsigned int packets_dropped;

    BOOL gnss_mode_set;
    BOOL gnss_nmea_rate_set;
    BOOL gnss_nmea_output_configured; /**< wm_sdk_gps_enable_nmea_output applied */
    BOOL gnss_nmea_output_started;    /**< TRUE after successful query/parse; FALSE on parse failure */
    BOOL gnss_info_report_set;        /**< wm_sdk_gps_set_gnss_info_period attempted (walnut: NOT_SUPPORTED) */
    BOOL gnss_start_mode_set;
    BOOL gnss_agps_set;               /**< A-GPS opened; cleared every 4 h when no fix (agps_ref) */
    UINT32 last_agps_open_time;       /**< Uptime (sec) when A-GPS was last opened; 0 = never */
    UINT32 last_agps_open_attempt_time; /**< Uptime (sec) of last failed open attempt; cooldown before retry */
} gps_manager_runtime_t;

/* ============================================================================
 * API
 * ============================================================================ */

wm_SdkResult gps_manager_init(void);
wm_SdkResult gps_manager_deinit(void);
BOOL gps_manager_get_last_valid_position(double *lat, double *lon, float *course);

/** @return TRUE when ignition input is on (same as GPS packet IgnOn). */
BOOL gps_manager_is_ignition_on(void);

/** @return TRUE when vehicle motion is on (reporting intervals / movement logic). */
BOOL gps_manager_is_motion_on(void);

/*---------------------------------------------------------------
 * Walnut placeholder inputs (reference: event manager + PowerInfo).
 * Call these from the owning subsystem once power/accel are ported;
 * each edge latches the same send trigger the reference latches.
 *--------------------------------------------------------------*/
void gps_manager_set_ignition_status(BOOL ign_on);
void gps_manager_set_motion_status(BOOL mot_on);
void gps_manager_set_charge_status(BOOL charge_connected);
BOOL gps_manager_get_charge_status(void);

/**
 * @brief  (Re-)register the kernel NMEA sentence callback that feeds the GPS
 *         urc_q with paired RMC+GGA records - the sole position source
 *         (reference URC build). Called at init and by the gps_ops stall
 *         recovery (reference re-enables NMEA-by-URC there).
 * @return TRUE on success; FALSE when the SDK rejects the registration
 *         (stall recovery retries; sustained silence escalates through the
 *         reference parse-fail streak).
 */
BOOL gps_manager_nmea_feed_register(void);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_GPS_MANAGER_H */
