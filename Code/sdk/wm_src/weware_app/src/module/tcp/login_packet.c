/**
  ******************************************************************************
  * @file    login_packet.c
  * @author  WheelsEye
  * @brief   Login packet creation for the weware application. Frame layout
  *          and byte order match the previous-generation gateway firmware,
  *          so the same server parses both.
  ******************************************************************************
  */

#include <string.h>

// sdk
#include "wm_global.h"
#include "wm_sdk_log.h"

// app
#include "tcp/login_packet.h"
#include "device_utils.h"
#include "weware_version.h"

#define LOG_TAG "LOG_PKT"
#define LOG_MODULE_LEVEL LOG_LEVEL_DEBUG
#include "module/log/log.h"

/*---------------------------------------------------------------
 * Static State
 *--------------------------------------------------------------*/
/* Session count sent in each login packet (increments per login) */
static unsigned int g_session_count = 0;

/*---------------------------------------------------------------
 * Internal Helpers
 *--------------------------------------------------------------*/
static unsigned char hex_char_to_val(char c)
{
    if (c >= '0' && c <= '9') return (unsigned char)(c - '0');
    if (c >= 'A' && c <= 'F') return (unsigned char)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (unsigned char)(c - 'a' + 10);
    return 0xFF;
}

/**
 * @brief  Convert a hex string to bytes ("0012" -> 0x00, 0x12).
 * @return 1 on success, 0 on failure.
 */
static int convert_hex_string_to_bytes(const char *hex_str, char *output, int output_size)
{
    int len;
    int i;

    if (!hex_str || !output || output_size < 2)
        return 0;

    len = (int)strlen(hex_str);
    if (len < 4)
        return 0;

    for (i = 0; i < output_size && (i * 2 + 1) < len; i++) {
        unsigned char v1 = hex_char_to_val(hex_str[i * 2]);
        unsigned char v2 = hex_char_to_val(hex_str[i * 2 + 1]);
        if (v1 == 0xFF || v2 == 0xFF)
            return 0;
        output[i] = (char)((v1 << 4) | v2);
    }
    return 1;
}

/**
 * @brief  Pack a 15/16-digit IMEI string into 8 BCD bytes. A 15-digit IMEI
 *         gets a leading 0 nibble ("861...": 0x08 0x61 ...).
 * @return 1 on success, 0 on failure.
 */
static int imei_to_8bytes(const char *imei, char *output)
{
    size_t len;
    int    out_idx = 0;

    if (!imei || !output)
        return 0;

    len = strlen(imei);
    if (len != 15 && len != 16)
        return 0;

    if (len == 15) {
        unsigned char v2 = hex_char_to_val(imei[0]);
        if (v2 == 0xFF)
            return 0;
        output[out_idx++] = (char)v2;
    }

    for (; out_idx < 8; out_idx++) {
        size_t str_idx = (len == 15) ? (size_t)(out_idx * 2 - 1)
                                     : (size_t)(out_idx * 2);
        unsigned char v1;
        unsigned char v2;

        if (str_idx + 1 >= len)
            return 0;
        v1 = hex_char_to_val(imei[str_idx]);
        v2 = hex_char_to_val(imei[str_idx + 1]);
        if (v1 == 0xFF || v2 == 0xFF)
            return 0;
        output[out_idx] = (char)((v1 << 4) | v2);
    }
    return 1;
}

/** XOR checksum over @p length bytes of @p data. */
static unsigned char calculate_checksum(const char *data, int length)
{
    unsigned char checksum = 0;
    int           i;

    for (i = 0; i < length; i++)
        checksum ^= (unsigned char)data[i];
    return checksum;
}

/** Write a "AABB" version string as [0xBB, 0xAA] (previous-firmware wire
 *  order); zeros on a malformed string. */
static void write_version_field(char *dest, const char *version, const char *name)
{
    char temp[2];

    if (strlen(version) >= 4 && convert_hex_string_to_bytes(version, temp, 2)) {
        dest[0] = temp[1];
        dest[1] = temp[0];
    } else {
        LOG_WARN("TCP invalid %s version format: %s, using fallback",
                        name, version);
        dest[0] = 0x00;
        dest[1] = 0x00;
    }
}

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/
int login_packet_create(char *buffer, int buffer_size)
{
    char          imei[DEVICE_UTILS_IMEI_BUFFER_SIZE];
    unsigned char checksum;

    if (!buffer || buffer_size < LOGIN_PACKET_TOTAL_SIZE) {
        LOG_ERROR("TCP invalid parameters for login packet creation");
        return 0;
    }

    memset(buffer, 0, LOGIN_PACKET_TOTAL_SIZE);

    /* Start Identifier (2 bytes) */
    buffer[LOGIN_OFFSET_START_ID]     = LOGIN_START_ID1;
    buffer[LOGIN_OFFSET_START_ID + 1] = LOGIN_START_ID2;

    /* Packet Type (1 byte) */
    buffer[LOGIN_OFFSET_PACKET_TYPE] = LOGIN_PACKET_TYPE;

    /* Packet Length (2 bytes) - big endian */
    buffer[LOGIN_OFFSET_PACKET_LENGTH]     = 0x00;
    buffer[LOGIN_OFFSET_PACKET_LENGTH + 1] = LOGIN_PACKET_LENGTH;

    /* IMEI (8 bytes) */
    if (!device_utils_get_imei(imei)) {
        LOG_ERROR("TCP IMEI cache not available - cannot create login packet");
        return 0;
    }
    if (!imei_to_8bytes(imei, &buffer[LOGIN_OFFSET_IMEI])) {
        LOG_ERROR("TCP failed to convert IMEI to bytes: %s", imei);
        return 0;
    }

    /* Firmware / Hardware Version (2 bytes each) */
    write_version_field(&buffer[LOGIN_OFFSET_FIRMWARE_VERSION],
                        FIRMWARE_VERSION, "firmware");
    write_version_field(&buffer[LOGIN_OFFSET_HARDWARE_VERSION],
                        HARDWARE_VERSION, "hardware");

    /* Session Count (2 bytes) - little endian, increments per login */
    g_session_count++;
    buffer[LOGIN_OFFSET_SESSION_COUNT]     = (char)(g_session_count & 0xFF);
    buffer[LOGIN_OFFSET_SESSION_COUNT + 1] = (char)((g_session_count >> 8) & 0xFF);

    /* Reserved Bytes (2 bytes) - already zeroed */

    /* Error Check (1 byte) - XOR over Length..Reserved inclusive */
    checksum = calculate_checksum(&buffer[LOGIN_OFFSET_PACKET_LENGTH],
                                  LOGIN_OFFSET_RESERVED - LOGIN_OFFSET_PACKET_LENGTH + 2);
    buffer[LOGIN_OFFSET_ERROR_CHECK] = (char)checksum;

    /* Stop Identifier (2 bytes) */
    buffer[LOGIN_OFFSET_STOP_ID]     = LOGIN_STOP_ID1;
    buffer[LOGIN_OFFSET_STOP_ID + 1] = LOGIN_STOP_ID2;

    LOG_INFO("TCP login packet created (size=%d, session=%u, checksum=0x%02X, IMEI=%s)",
                 LOGIN_PACKET_TOTAL_SIZE, g_session_count, checksum, imei);

    return LOGIN_PACKET_TOTAL_SIZE;
}
