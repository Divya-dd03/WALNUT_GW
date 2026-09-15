/**
  ******************************************************************************
  * @file    gps_packet.c
  * @author  WheelsEye
  * @brief   GPS position packet (type 0x49) builder - walnut port of the
  *          reference module/gps/gps_packet.c. Field-for-field identical
  *          wire format.
  *
  *          Real sources on walnut today:
  *            - GNSS sample (caller supplies, from wm_sdk_gps_get_navdata)
  *            - ignition / motion / charge = GPS runtime latches (g_gps)
  *            - VDOP byte   = weware network state (values match reference)
  *            - PDOP byte   = weware TCP client state (values match reference)
  *            - CSQ/MCC/MNC/LAC/cell = weware network radio snapshot
  *            - SIM inserted = weware SIM module
  *          PLACEHOLDERS (subsystem not ported yet - see GPS_PH_* below):
  *            - input-wire / external voltage, battery % (power manager),
  *              digout, accelerometer orientation.
  ******************************************************************************
  */

#include <string.h>

// sdk
#include "wm_global.h"
#include "wm_sdk_log.h"

// app
#include "module/gps/gps_packet.h"
#include "module/gps/gps_config.h"
#include "module/gps/gps_manager.h"
#include "module/network/network.h"
#include "module/sim/sim.h"
#include "module/tcp/tcp.h"

/*---------------------------------------------------------------
 * PLACEHOLDER values - single place to spot and replace when the
 * owning subsystem (power/accel/digout managers) is ported.
 *--------------------------------------------------------------*/
#define GPS_PH_INPUT_WIRE_MV   12000u              /* 12.0 V              */
#define GPS_PH_EXTERNAL_MV     12000u              /* 12.0 V              */
#define GPS_PH_BATTERY_PCT     100u                /* 100 %               */
#define GPS_PH_DIGOUT          GPS_GW_STATUS2_OFF
#define GPS_PH_ACCEL_ORIENT    0x00u

/* Packet sequence counter (increments per built packet) */
static unsigned int g_packet_counter = 0;

extern gps_manager_runtime_t g_gps;

/** Write (value * scale) as a big-endian signed fixed-point field. */
static void write_fixed_point_be(float value, int scale, char *output, int bytes)
{
    int fixed_value = (int)(value * scale);
    int i;

    for (i = 0; i < bytes; i++)
        output[i] = (char)((fixed_value >> ((bytes - i - 1) * 8)) & 0xFF);
}

int gps_packet_create(char *buffer, int buffer_size, const GpsPacket *gps_data)
{
    if (!buffer || buffer_size < GPS_PACKET_TOTAL_SIZE || !gps_data) {
        wm_sdk_log_error("GPS invalid parameters for packet creation");
        return 0;
    }

    memset(buffer, 0, GPS_PACKET_TOTAL_SIZE);

    /* Start Identifier (2 bytes) */
    buffer[GPS_OFFSET_START_ID]     = GPS_PACKET_START_ID1;
    buffer[GPS_OFFSET_START_ID + 1] = GPS_PACKET_START_ID2;

    /* Packet Type (1 byte) */
    buffer[GPS_OFFSET_PACKET_TYPE] = GPS_PACKET_TYPE;

    /* Packet Length (2 bytes) - little endian */
    buffer[GPS_OFFSET_PACKET_LENGTH]     = 0x00;
    buffer[GPS_OFFSET_PACKET_LENGTH + 1] = GPS_PACKET_LENGTH;

    /* GNSS Status (1 byte) */
    buffer[GPS_OFFSET_GNSS_STATUS] = gps_data->fix_valid_calculated ? 0x01 : 0x00;

    /* Timestamp (4 bytes): UTC Unix selected in gps_ops per GPS config time
     * source (AUTO/GPS/GSM). Big-endian. */
    {
        unsigned int timestamp = gps_data->utc_time;
        buffer[GPS_OFFSET_TIMESTAMP]     = (char)((timestamp >> 24) & 0xFF);
        buffer[GPS_OFFSET_TIMESTAMP + 1] = (char)((timestamp >> 16) & 0xFF);
        buffer[GPS_OFFSET_TIMESTAMP + 2] = (char)((timestamp >> 8) & 0xFF);
        buffer[GPS_OFFSET_TIMESTAMP + 3] = (char)(timestamp & 0xFF);
    }

    /* Latitude (4 bytes) - fixed point, big-endian */
    {
        long lat_fixed = (long)(gps_data->latitude_deg * GPS_LAT_LON_FIXED_SCALE);
        buffer[GPS_OFFSET_LATITUDE]     = (char)((lat_fixed >> 24) & 0xFF);
        buffer[GPS_OFFSET_LATITUDE + 1] = (char)((lat_fixed >> 16) & 0xFF);
        buffer[GPS_OFFSET_LATITUDE + 2] = (char)((lat_fixed >> 8) & 0xFF);
        buffer[GPS_OFFSET_LATITUDE + 3] = (char)(lat_fixed & 0xFF);
    }
    /* Longitude (4 bytes) - fixed point, big-endian */
    {
        long lon_fixed = (long)(gps_data->longitude_deg * GPS_LAT_LON_FIXED_SCALE);
        buffer[GPS_OFFSET_LONGITUDE]     = (char)((lon_fixed >> 24) & 0xFF);
        buffer[GPS_OFFSET_LONGITUDE + 1] = (char)((lon_fixed >> 16) & 0xFF);
        buffer[GPS_OFFSET_LONGITUDE + 2] = (char)((lon_fixed >> 8) & 0xFF);
        buffer[GPS_OFFSET_LONGITUDE + 3] = (char)(lon_fixed & 0xFF);
    }

    /* Speed (1 byte) - convert knots to km/h */
    {
        int speed_kmh = (int)(gps_data->speed_knots * GPS_KMH_TO_KNOTS);
        buffer[GPS_OFFSET_SPEED] = (char)(speed_kmh & 0xFF);
    }

    /* Course (2 bytes) - convert to fixed point */
    write_fixed_point_be(gps_data->course_deg, 100, &buffer[GPS_OFFSET_COURSE], 2);

    /* Altitude (2 bytes) - convert to fixed point */
    write_fixed_point_be(gps_data->altitude_m, 10, &buffer[GPS_OFFSET_ALTITUDE], 2);

    /* Satellite Count (1 byte) - 0 when coordinates invalid (e.g. 0,0 no-fix) */
    {
        BOOL coords_valid = (gps_data->latitude_deg != 0.0 ||
                             gps_data->longitude_deg != 0.0);
        buffer[GPS_OFFSET_SATELLITE_COUNT] =
            (char)(coords_valid ? gps_data->sats_in_use : 0);
    }

    /* HDOP (1 byte) - scale HDOP from x100 to single byte */
    buffer[GPS_OFFSET_HDOP] = (char)((gps_data->hdop_x100 / 100) & 0xFF);

    /* VDOP byte repurposed (diagnostic): network state-machine value; the
     * weware NetworkState values match the reference (12=CONNECTED,
     * 13=DISCONNECTED, 14=ERROR, 15=RESTART_CFUN). */
    buffer[GPS_OFFSET_VDOP] = (char)((UINT8)weware_network_get_state());

    /* PDOP byte repurposed (diagnostic): TCP client state value; the weware
     * TcpState values match the reference (5=CONNECTED, 9=SENDING_DATA,
     * 10=WAIT_ACK_DATA, 12=CLOSED, 14=ERROR). */
    buffer[GPS_OFFSET_PDOP] = (char)((UINT8)weware_tcp_get_state());

    /* GPS Fix (1 byte) */
    buffer[GPS_OFFSET_GPS_FIX] = gps_data->fix_valid_real ? 0x01 : 0x00;

    /* Signal / cell: from the weware network radio snapshot */
    {
        NetworkRadioSnapshot radio;
        if (!weware_network_get_radio_snapshot(&radio) || !radio.valid)
            memset(&radio, 0, sizeof(radio));
        buffer[GPS_OFFSET_SIGNAL_STRENGTH] = (char)(radio.signal_strength & 0xFF);
        buffer[GPS_OFFSET_MCC]     = (char)((radio.mcc >> 8) & 0xFF);
        buffer[GPS_OFFSET_MCC + 1] = (char)(radio.mcc & 0xFF);
        buffer[GPS_OFFSET_MNC]     = (char)(radio.mnc & 0xFF);
        buffer[GPS_OFFSET_LAC]     = (char)((radio.lac >> 8) & 0xFF);
        buffer[GPS_OFFSET_LAC + 1] = (char)(radio.lac & 0xFF);
        buffer[GPS_OFFSET_CELL_ID]     = (char)((radio.cell_id >> 24) & 0xFF);
        buffer[GPS_OFFSET_CELL_ID + 1] = (char)((radio.cell_id >> 16) & 0xFF);
        buffer[GPS_OFFSET_CELL_ID + 2] = (char)((radio.cell_id >> 8) & 0xFF);
        buffer[GPS_OFFSET_CELL_ID + 3] = (char)(radio.cell_id & 0xFF);
    }

    /* Gateway Status1 (3 bytes): input-wire voltage mV BE + digout
     * (PLACEHOLDER: power/digout managers not ported) */
    buffer[GPS_OFFSET_GATEWAY_STATUS1] = (char)((GPS_PH_INPUT_WIRE_MV >> 8) & 0xFF);
    buffer[GPS_OFFSET_GATEWAY_STATUS1 + 1] = (char)(GPS_PH_INPUT_WIRE_MV & 0xFF);
    buffer[GPS_OFFSET_GATEWAY_STATUS1 + GPS_GW_STATUS1_IDX_DIGOUT] = (char)GPS_PH_DIGOUT;

    /* Gateway Status2 (3 bytes): ignition, charge (GPS runtime latches), SIM (real) */
    buffer[GPS_OFFSET_GATEWAY_STATUS2 + GPS_GW_STATUS2_IDX_IGNITION] =
        g_gps.ign_status ? (char)GPS_GW_STATUS2_ON : (char)GPS_GW_STATUS2_OFF;
    buffer[GPS_OFFSET_GATEWAY_STATUS2 + GPS_GW_STATUS2_IDX_CHARGE] =
        g_gps.charge_status ? (char)GPS_GW_STATUS2_ON : (char)GPS_GW_STATUS2_OFF;
    buffer[GPS_OFFSET_GATEWAY_STATUS2 + GPS_GW_STATUS2_IDX_SIM] =
        weware_sim_get_sim_status() ? (char)GPS_GW_STATUS2_ON : (char)GPS_GW_STATUS2_OFF;

    /* External Voltage (2 bytes, mV BE) - PLACEHOLDER */
    buffer[GPS_OFFSET_EXTERNAL_VOLTAGE]     = (char)((GPS_PH_EXTERNAL_MV >> 8) & 0xFF);
    buffer[GPS_OFFSET_EXTERNAL_VOLTAGE + 1] = (char)(GPS_PH_EXTERNAL_MV & 0xFF);

    /* Battery % (2 bytes BE) - PLACEHOLDER */
    buffer[GPS_OFFSET_BATTERY_PERCENT]     = (char)((GPS_PH_BATTERY_PCT >> 8) & 0xFF);
    buffer[GPS_OFFSET_BATTERY_PERCENT + 1] = (char)(GPS_PH_BATTERY_PCT & 0xFF);

    /* Packet Count (2 bytes) - little endian, increments per packet */
    g_packet_counter++;
    buffer[GPS_OFFSET_PACKET_COUNT]     = (char)(g_packet_counter & 0xFF);
    buffer[GPS_OFFSET_PACKET_COUNT + 1] = (char)((g_packet_counter >> 8) & 0xFF);

    /* Reserved: [0]=accel orientation (PLACEHOLDER), [1]=motion (runtime latch) */
    buffer[GPS_OFFSET_RESERVED] = (char)GPS_PH_ACCEL_ORIENT;
    buffer[GPS_OFFSET_RESERVED + 1] =
        g_gps.mot_status ? (char)GPS_GW_STATUS2_ON : (char)GPS_GW_STATUS2_OFF;

    /* Error Check (1 byte) - XOR over Length..Reserved inclusive (49 bytes) */
    {
        unsigned char checksum = 0;
        int i;
        for (i = GPS_OFFSET_PACKET_LENGTH;
             i <= GPS_OFFSET_RESERVED + 1; i++)
            checksum ^= (unsigned char)buffer[i];
        buffer[GPS_OFFSET_ERROR_CHECK] = (char)checksum;
    }

    /* Stop Identifier (2 bytes) */
    buffer[GPS_OFFSET_STOP_ID]     = GPS_PACKET_STOP_ID1;
    buffer[GPS_OFFSET_STOP_ID + 1] = GPS_PACKET_STOP_ID2;

    return GPS_PACKET_TOTAL_SIZE;
}
