/**
  ******************************************************************************
  * @file    gps_ops.h
  * @author  WheelsEye
  * @brief   GPS operations: query/parse, task cycle, packet build - walnut
  *          port of the reference firmware's module/gps/gps_ops.h.
  *          Triggers: gps_triggers.h.
  ******************************************************************************
  */

#ifndef WEWARE_GPS_OPS_H
#define WEWARE_GPS_OPS_H

#include "wm_sdk_types.h"
#include "module/gps/gps_manager.h"
#include "module/gps/gps_triggers.h"

#ifdef __cplusplus
extern "C" {
#endif

int gps_ops_query_and_parse(GpsPacket *out);

void gps_ops_set_runtime_defaults(void);

/**
 * GNSS power-on + stabilize + NMEA configure (one-shot, blocking delays).
 * @return TRUE when complete; FALSE on error (retry next GPS loop). No-op after success.
 */
BOOL gps_ops_power_on_and_configure_nmea_step(void);

void gps_ops_retry_configuration(void);

/**
 * TEMPORARY (field debug) manual GNSS power control, driven by
 * MOD:SET-GPS-ON / MOD:SET-GPS-OFF.
 *
 * OFF powers the receiver down (wm_sdk_gps_set_power_status(0)) and suppresses
 * both the parse-fail warning and the 60-cycle soft-reset escalation, so the
 * device does NOT reboot a minute later. ON re-arms the full bring-up ladder so
 * power-on AND the app's own config (mode/rate/start) are re-applied - the SDK
 * alone would only restore its own $POLCFGSYS,193 default.
 *
 * Both are idempotent. Remove together with the two command-table entries.
 * @return RESULT_SUCCESS, or RESULT_ERROR if the SDK refused the power change.
 */
Result gps_ops_set_power_enabled(BOOL on);

/** @return TRUE while the receiver is powered down by MOD:SET-GPS-OFF. */
BOOL gps_ops_is_power_forced_off(void);

/** Open A-GPS once when configured and allowed (walnut: NOT_SUPPORTED stub). */
void gps_ops_open_agps_if_needed(void);

/** Clear A-GPS after 4 h without fix when @c agps_ref enabled. */
void gps_ops_agps_refresh_if_needed(void);

BOOL gps_validate_coordinates(double lat, double lon);

/** Apply config time source (AUTO/GPS/GSM) to @a p. */
void gps_ops_apply_time_source_to_packet(GpsPacket *p);

/** Set @c fix_valid_calculated from @c fix_valid_real and coordinates. */
void gps_ops_update_fix_valid_calculated(GpsPacket *p);

/** Last-valid file on motion edge (fix held): ON clears, OFF saves current fix. */
void gps_ops_loc_storage_update_on_motion_changed(BOOL mot_on);

typedef enum {
    GPS_PACKET_BUILD_CURRENT = 0,
    GPS_PACKET_BUILD_LAST_VALID_MERGED,
} GpsPacketBuildKind;

typedef enum {
    GPS_FIX_TRANSITION_NONE = 0,
    GPS_FIX_TRANSITION_CONNECTED,
    GPS_FIX_TRANSITION_DISCONNECTED,
} GpsFixTransition;

/** @return fix edge this cycle: connected, disconnected, or @c GPS_FIX_TRANSITION_NONE. */
GpsFixTransition gps_ops_process_fix_transition(void);

/** Query/parse, clear on failure, apply time source and calculated fix. */
void gps_ops_refresh_current_sample(void);

/** Storage, last-valid RAM and first-fix send trigger on fix edge. */
void gps_ops_handle_fix_transition(GpsFixTransition ev);

/** Reference: pop GPS msg_q append payload (BLE send-with-GPS).
 *  TODO(gps): module message queues not ported yet - always returns 0. */
UINT32 gps_ops_pop_gps_msg_queue_latch_append(char *buf, UINT32 buf_size);

GpsPacketBuildKind gps_ops_pick_send_kind(void);

/** Calculated speed (km/h) from last sent location to current fix; 0 if unavailable. */
float gps_ops_calc_speed_kmh_from_last_sent(void);

void update_last_valid_gps_data(GpsPacket *pkt);

/**
 * Build a GPS binary packet (same rules as the GPS task send path).
 * @return @c GPS_PACKET_TOTAL_SIZE on success, 0 on failure.
 */
int gps_ops_build_position_packet(char *buffer, int buffer_size, GpsPacketBuildKind kind);

/** @c gps_ops_build_position_packet; updates last-valid RAM on @c GPS_PACKET_BUILD_CURRENT. */
int gps_ops_build_send_packet(char *buffer, int buffer_size, GpsPacketBuildKind kind);

/** Append @a append after GPS bytes in @a msg_buf when inline cap allows. */
UINT32 gps_ops_merge_tcp_append(char *msg_buf, UINT32 gps_len,
                                const char *append, UINT32 append_len);

/** Send GPS (+ optional append) message via the TCP module. */
wm_SdkResult gps_ops_push_tcp_position_message(const char *data, UINT32 len);

/** Login-with-GPS: build last-valid GPS binary (does not use the TCP send path). */
int gps_ops_build_last_valid_position_for_tcp(char *buffer, int buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_GPS_OPS_H */
