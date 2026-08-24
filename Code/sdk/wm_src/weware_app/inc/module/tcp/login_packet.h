/**
  ******************************************************************************
  * @file    login_packet.h
  * @author  WheelsEye
  * @brief   Login packet structure definitions for the weware application.
  *
  *          Wire format (24 bytes total):
  *            [0..1]   start id "WE" (0x57 0x45)
  *            [2]      packet type 0x48
  *            [3..4]   payload length, big endian (0x0012 = 18 bytes,
  *                     counted from the length field through the reserved
  *                     bytes inclusive)
  *            [5..12]  IMEI packed as 8 BCD bytes (15 digits, leading 0)
  *            [13..14] firmware version
  *            [15..16] hardware version
  *            [17..18] session count, little endian
  *            [19..20] reserved (0x00 0x00)
  *            [21]     XOR checksum over bytes [3..20]
  *            [22..23] stop id "we" (0x77 0x65)
  ******************************************************************************
  */

#ifndef WEWARE_TCP_LOGIN_PACKET_H
#define WEWARE_TCP_LOGIN_PACKET_H

/* Login packet framing */
#define LOGIN_START_ID1         0x57
#define LOGIN_START_ID2         0x45
#define LOGIN_PACKET_TYPE       0x48
#define LOGIN_STOP_ID1          0x77
#define LOGIN_STOP_ID2          0x65
#define LOGIN_PACKET_LENGTH     0x12  /* 18 bytes from Length to Reserved Byte */
#define LOGIN_PACKET_TOTAL_SIZE 24    /* Total login packet size */

/* Login packet field offsets */
#define LOGIN_OFFSET_START_ID         0
#define LOGIN_OFFSET_PACKET_TYPE      2
#define LOGIN_OFFSET_PACKET_LENGTH    3
#define LOGIN_OFFSET_IMEI             5
#define LOGIN_OFFSET_FIRMWARE_VERSION 13
#define LOGIN_OFFSET_HARDWARE_VERSION 15
#define LOGIN_OFFSET_SESSION_COUNT    17
#define LOGIN_OFFSET_RESERVED         19
#define LOGIN_OFFSET_ERROR_CHECK      21
#define LOGIN_OFFSET_STOP_ID          22

/**
 * @brief  Create a login packet from the cached IMEI and the compile-time
 *         FW/HW versions. Each call increments the session count.
 * @param  buffer       [out] receives the packet; must hold at least
 *                      LOGIN_PACKET_TOTAL_SIZE bytes.
 * @param  buffer_size  size of @p buffer.
 * @return LOGIN_PACKET_TOTAL_SIZE on success; 0 on failure (bad arguments
 *         or IMEI not cached yet).
 */
int login_packet_create(char *buffer, int buffer_size);

#endif /* WEWARE_TCP_LOGIN_PACKET_H */
