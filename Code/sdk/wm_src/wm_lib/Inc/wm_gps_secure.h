/**
 ******************************************************************************
 * @file    wm_gps_secure.h
 * @author  Walnut Medical
 * @brief   Public GPS API for the on-board GNSS receiver.
 *
 *          Drives the receiver over the existing wm_uart layer (115200 8N1),
 *          parses NMEA (GGA + RMC), and maintains a mutex-guarded last-known
 *          fix. A dedicated RTOS task owns parsing; consumers read via
 *          wm_gps_get_fix() or register a fix callback. The part fitted is
 *          selected by WM_GPS_CURRENT_CHIP below; its proprietary command set
 *          is the only chip-specific part of the module.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical.
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __WM_GPS_SECURE_H__
#define __WM_GPS_SECURE_H__

/*******************************************************************************
** Header Files
******************************************************************************/
#include "sc_os.h"      /* SC_STATUS, UINT types */
#include "wm_nmea.h"    /* WM_GPS_Position, wm_gps_fix_quality_e */

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** Defines
******************************************************************************/
/* GNSS part fitted. Selects the proprietary command set below and in the .c. */
#define WM_GPS_CHIP_BK1616P   1      /* Beken, "$POLCFG..." commands */
#define WM_GPS_CHIP_CC1161W   2      /* ICOE, "ICOE Protocol"        */

#define WM_GPS_CURRENT_CHIP   WM_GPS_CHIP_BK1616P

#if (WM_GPS_CURRENT_CHIP == WM_GPS_CHIP_BK1616P)

/* Constellation bits for wm_gps_set_constellations() ($POLCFGSYS,<mask>). */
#define WM_GPS_SYS_GPS   (1u << 0)   /* GPS L1                */
#define WM_GPS_SYS_BDS   (1u << 2)   /* Beidou B1I            */
#define WM_GPS_SYS_GLO   (1u << 6)   /* GLONASS G1            */
#define WM_GPS_SYS_GAL   (1u << 7)   /* Galileo E1            */

/* NMEA message ids for wm_gps_set_sentence() ($POLCFGMSG,0,<id>,<0|1>). */
#define WM_GPS_MSG_GGA   0
#define WM_GPS_MSG_GSA   1
#define WM_GPS_MSG_GSV   2
#define WM_GPS_MSG_VTG   3
#define WM_GPS_MSG_RMC   5
#define WM_GPS_MSG_ZDA   20

#elif (WM_GPS_CURRENT_CHIP == WM_GPS_CHIP_CC1161W)

/* Signal bits for wm_gps_set_constellations() ($CFGSYS,h<mask>). One bit per
 * signal, not per constellation. ROM default is h11111 (GPS|BDS|GLO|GAL|QZSS). */
#define WM_GPS_SYS_GPS   (1uL << 0)   /* GPS L1C/A             */
#define WM_GPS_SYS_BDS   (1uL << 4)   /* Beidou B1I            */
#define WM_GPS_SYS_BDS_B1C (1uL << 7) /* Beidou B1C (off by default) */
#define WM_GPS_SYS_GLO   (1uL << 8)   /* GLONASS L1            */
#define WM_GPS_SYS_GAL   (1uL << 12)  /* Galileo E1            */
#define WM_GPS_SYS_QZSS  (1uL << 16)  /* QZSS                  */
#define WM_GPS_SYS_SBAS  (1uL << 17)  /* SBAS                  */

/* NMEA message ids for wm_gps_set_sentence() ($CFGMSG,0,<id>,<cycle>,1). */
#define WM_GPS_MSG_GGA   0
#define WM_GPS_MSG_GLL   1
#define WM_GPS_MSG_GSA   2
#define WM_GPS_MSG_GSV   3
#define WM_GPS_MSG_RMC   4
#define WM_GPS_MSG_VTG   5
#define WM_GPS_MSG_ZDA   6

#else
#error "WM_GPS_CURRENT_CHIP: unsupported part"
#endif

/*******************************************************************************
** Type Definitions
******************************************************************************/
/* Invoked from the GPS task (safe context) once per committed RMC epoch. The
 * pointer is a stack copy valid only for the duration of the call - copy out
 * anything you need to keep. 'fix->valid' reflects the current fix status. */
typedef void (*wm_gps_fix_cb)(const WM_GPS_Position *fix);

/* Invoked from the GPS task for every complete sentence the receiver sends,
 * before the checksum is verified and before parsing - so malformed lines are
 * visible too. 'line' is NUL-terminated and valid only for the duration of the
 * call; 'len' excludes the terminator. Keep the work short: the parser task is
 * blocked while this runs. */
typedef void (*wm_gps_nmea_cb)(const char *line, uint16_t len);

/*******************************************************************************
** Functions
******************************************************************************/
/* Create the mutexes/queue/task, attach the UART receive callback and power
 * the receiver on with the default configuration. Idempotent. */
void      wm_gps_init(void);

/* Power the receiver on (UART open + reset pulse + default config) / off
 * (UART close + hold in reset; the shared 3V3 LDO is left up). */
SC_STATUS wm_gps_power_on(void);
SC_STATUS wm_gps_power_off(void);

/* Warm start/stop: resume / mute NMEA output without cutting power. */
SC_STATUS wm_gps_start(void);
SC_STATUS wm_gps_stop(void);

/* Copy the last fix under mutex. Returns SC_FAIL if no valid fix has ever been
 * seen (out is still populated with zeros in that case). */
SC_STATUS wm_gps_get_fix(WM_GPS_Position *out);

/* true if the most recent RMC epoch reported a valid fix. */
bool      wm_gps_has_fix(void);

/* Milliseconds since the last fix was committed (0 if never). */
uint32_t  wm_gps_fix_age_ms(void);

/* Register (or clear, with NULL) the per-epoch fix callback. */
void      wm_gps_set_fix_cb(wm_gps_fix_cb cb);

/* Register (or clear, with NULL) the raw-sentence callback. Parsing is
 * unaffected: the fix callback keeps firing whether or not this is set. */
void      wm_gps_set_nmea_cb(wm_gps_nmea_cb cb);

/* Configuration. Each command waits for the receiver's ACK and returns SC_FAIL
 * on a bad argument, a UART that is not open, a $FAIL reply or an ACK timeout. */
SC_STATUS wm_gps_set_update_rate(uint8_t nav_hz);       /* 1 | 2 | 4 | 5        */
SC_STATUS wm_gps_set_constellations(uint32_t mask);     /* WM_GPS_SYS_* bits    */
SC_STATUS wm_gps_set_sentence(uint8_t msg_id, bool on); /* WM_GPS_MSG_*         */
SC_STATUS wm_gps_save_config(void);                     /* persist to GNSS flash*/
SC_STATUS wm_gps_reset(bool cold);                      /* cold vs hot restart  */

/* Send a raw command body (pass the whole sentence, '$' included). The line
 * terminator is appended per WM_GPS_CMD_TERM. */
SC_STATUS wm_gps_send_cmd(const char *nmea_cmd);

#ifdef __cplusplus
}
#endif
#endif /* __WM_GPS_SECURE_H__ */
