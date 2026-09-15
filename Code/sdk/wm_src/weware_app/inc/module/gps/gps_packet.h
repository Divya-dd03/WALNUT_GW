/**
  ******************************************************************************
  * @file    gps_packet.h
  * @author  WheelsEye
  * @brief   GPS position packet (type 0x49) for the weware application.
  *          Wire format is byte-identical to the reference firmware
  *          (module/gps/gps_packet.h there), so the same server parses both.
  *          55 bytes total; all multi-byte fields big-endian except Packet
  *          Length and Packet Count (little-endian).
  ******************************************************************************
  */

#ifndef WEWARE_GPS_GPS_PACKET_H
#define WEWARE_GPS_GPS_PACKET_H

#include "wm_sdk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Types
 * ============================================================================ */

/** GPS data packet (used by gps_packet, gps_ops, gps_manager) */
typedef struct
{
    /** GNSS-reported fix (receiver's own validity flag). */
    int fix_valid_real;
    /** Policy fix: @c fix_valid_real and coordinates pass @c gps_validate_coordinates(). */
    int fix_valid_calculated;
    double latitude_deg;
    double longitude_deg;
    float altitude_m;
    float speed_knots;
    float course_deg;
    UINT8 sats_in_use;
    UINT16 hdop_x100;
    UINT32 utc_time;        /* UTC Unix seconds */
} GpsPacket;

/* ============================================================================
 * Packet layout (size, IDs, offsets) - verbatim from the reference firmware
 * ============================================================================ */

#define GPS_PACKET_TOTAL_SIZE 55  /* Total packet size including start/stop identifiers */

#define GPS_PACKET_START_ID1 0x57   /* 'W' */
#define GPS_PACKET_START_ID2 0x45   /* 'E' */
#define GPS_PACKET_TYPE 0x49
#define BLE_PACKET_TYPE 0x4A
#define GPS_PACKET_STOP_ID1 0x77    /* 'w' */
#define GPS_PACKET_STOP_ID2 0x65    /* 'e' */
#define GPS_PACKET_LENGTH 0x31      /* 49 bytes from Length to Reserved Byte */

#define GPS_OFFSET_START_ID 0
#define GPS_OFFSET_PACKET_TYPE 2
#define GPS_OFFSET_PACKET_LENGTH 3
#define GPS_OFFSET_GNSS_STATUS 5
#define GPS_OFFSET_TIMESTAMP 6
#define GPS_OFFSET_LATITUDE 10
#define GPS_OFFSET_LONGITUDE 14
#define GPS_OFFSET_SPEED 18
#define GPS_OFFSET_COURSE 19
#define GPS_OFFSET_ALTITUDE 21
#define GPS_OFFSET_SATELLITE_COUNT 23
#define GPS_OFFSET_HDOP 24
#define GPS_OFFSET_VDOP 25          /* repurposed: network state (diagnostic) */
#define GPS_OFFSET_PDOP 26          /* repurposed: TCP client state (diagnostic) */
#define GPS_OFFSET_GPS_FIX 27
#define GPS_OFFSET_SIGNAL_STRENGTH 28
#define GPS_OFFSET_MCC 29
#define GPS_OFFSET_MNC 31
#define GPS_OFFSET_LAC 32
#define GPS_OFFSET_CELL_ID 34
#define GPS_OFFSET_GATEWAY_STATUS1 38
#define GPS_OFFSET_GATEWAY_STATUS2 41
/** Gateway Status1[0..1]: input-wire voltage (mV, big-endian); [2]: digout */
#define GPS_GW_STATUS1_IDX_DIGOUT      2
/** Gateway Status2[0]: ignition ON; [1]: charge connected; [2]: SIM inserted */
#define GPS_GW_STATUS2_IDX_IGNITION   0
#define GPS_GW_STATUS2_IDX_CHARGE    1
#define GPS_GW_STATUS2_IDX_SIM       2
#define GPS_GW_STATUS2_OFF           0x00u
#define GPS_GW_STATUS2_ON            0x01u
#define GPS_OFFSET_EXTERNAL_VOLTAGE 44
#define GPS_OFFSET_BATTERY_PERCENT 46
#define GPS_OFFSET_PACKET_COUNT 48
#define GPS_OFFSET_RESERVED 50
/** Reserved[0]: accel orientation; Reserved[1]: motion ON/OFF (@c GPS_GW_STATUS2_ON/OFF) */
#define GPS_OFFSET_ERROR_CHECK 52
#define GPS_OFFSET_STOP_ID 53

/* ============================================================================
 * API
 * ============================================================================ */

/**
 * @brief Create GPS binary packet from GPS data
 * @param buffer Output buffer for GPS packet (must be at least GPS_PACKET_TOTAL_SIZE bytes)
 * @param buffer_size Size of the output buffer
 * @param gps_data GPS data to encode
 * @return Size of created packet on success, 0 on failure
 */
int gps_packet_create(char *buffer, int buffer_size, const GpsPacket *gps_data);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_GPS_GPS_PACKET_H */
