/**
 ******************************************************************************
 * @file    wm_nmea.h
 * @author  Walnut Medical
 * @brief   Dependency-free NMEA0183 parser (GGA + RMC) for the GNSS receiver.
 *
 *          This header intentionally pulls in nothing from the RTOS / UART /
 *          global layers - only <stdint.h>/<stdbool.h> - so the parser can be
 *          unit-tested off-target. wm_gps_secure.h re-exports WM_GPS_Position
 *          by including this file.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical.
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __WM_NMEA_H__
#define __WM_NMEA_H__

/*******************************************************************************
** Header Files
******************************************************************************/
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** Defines
******************************************************************************/
/* NMEA v4.10 caps a sentence at 82 chars, but multi-constellation GSA/GSV run
 * past it. Longer lines are discarded to the next '$'. */
#define WM_NMEA_LINE_MAX 128

/*******************************************************************************
** Type Definitions
******************************************************************************/
/* GGA fix-quality indicator (field 6). Standard NMEA0183 values. */
typedef enum
{
    WM_GPS_FIX_NONE = 0,   /* no fix / invalid                     */
    WM_GPS_FIX_GPS  = 1,   /* autonomous SPS fix                   */
    WM_GPS_FIX_DGPS = 2,   /* differential / SBAS                  */
    WM_GPS_FIX_PPP  = 3,   /* precise point positioning            */
    WM_GPS_FIX_RTK  = 4,   /* RTK fixed                            */
    WM_GPS_FIX_RTKF = 5,   /* RTK float                            */
    WM_GPS_FIX_EST  = 6    /* dead-reckoning / estimated           */
} wm_gps_fix_quality_e;

/* Last-known position, merged from one GGA + one RMC epoch. */
typedef struct
{
    bool     valid;         /* true when the latest RMC status was 'A'      */
    uint32_t stamp_ms;      /* ms since boot when this fix was committed    */

    /* WGS-84 position, decimal degrees, +N / +E */
    double   latitude;      /* -90 .. +90                                   */
    double   longitude;     /* -180 .. +180                                 */
    float    altitude_m;    /* MSL orthometric height (GGA field 9)         */
    float    geoid_sep_m;   /* geoid separation (GGA field 11)              */

    /* Motion (RMC) */
    float    speed_kmh;     /* knots * 1.852                                */
    float    course_deg;    /* true track over ground                       */

    /* Quality (GGA) */
    wm_gps_fix_quality_e quality;
    uint8_t  sats_used;     /* satellites used in the fix (GGA field 7)     */
    float    hdop;          /* horizontal dilution of precision             */

    /* UTC date/time (RMC date + RMC/GGA time) */
    uint16_t year;          /* full year e.g. 2026 (0 if unknown)           */
    uint8_t  month, day;    /* 1..12 / 1..31                                */
    uint8_t  hour, minute, second;
    uint16_t msec;          /* fractional seconds -> ms                     */
} WM_GPS_Position;

/* Streaming line-assembly context. One instance per UART stream. */
typedef struct
{
    char     line[WM_NMEA_LINE_MAX];  /* '$'..'*hh', NUL-terminated when complete */
    uint16_t len;
    bool     in_sentence;
    bool     overflow;                /* line exceeded buffer -> drop to next '$'  */
} wm_nmea_ctx_t;

/*******************************************************************************
** Functions
******************************************************************************/
/* Reset the streaming assembler (call once before first feed). */
void wm_nmea_reset(wm_nmea_ctx_t *ctx);

/* Feed one received byte. Returns true exactly once, when a complete sentence
 * (started with '$', terminated by CR or LF, not overflowed) is sitting
 * NUL-terminated in ctx->line. Non-sentence bytes - including the receiver's
 * ASCII boot word - are ignored. */
bool wm_nmea_feed(wm_nmea_ctx_t *ctx, char c);

/* XOR of every char after '$' up to (not including) '*' or the NUL. Use it to
 * build the "*hh" suffix of an outgoing sentence. */
uint8_t wm_nmea_checksum(const char *s);

/* XOR of every char strictly between '$' and '*', compared against the two hex
 * digits after '*'. Returns false if there is no '*' checksum or it mismatches. */
bool wm_nmea_verify_checksum(const char *s);

/* Copy the Nth comma-separated field (index 0 = the talker/type token, e.g.
 * "GNGGA") into out[out_sz], NUL-terminated. Handles NMEA's empty fields:
 * an empty field yields "" and length 0. Returns the field length, or -1 if
 * the sentence has fewer than index+1 fields. Never use strtok here - it
 * collapses the empty fields and mis-indexes everything after the first blank. */
int wm_nmea_field(const char *s, int index, char *out, int out_sz);

/* "ddmm.mmmm"/"dddmm.mmmm" + hemisphere 'N'/'S'/'E'/'W' -> signed decimal
 * degrees. Returns 0.0 for an empty string. */
double wm_nmea_ddmm_to_deg(const char *ddmm, char hemi);

/* Parse a GGA sentence into pos (fills time/lat/lon/quality/sats/hdop/alt/geoid;
 * leaves other fields untouched so GGA+RMC merge). Returns false if s is not a
 * *GGA sentence. Empty fields are left at their current value. */
bool wm_nmea_parse_gga(const char *s, WM_GPS_Position *pos);

/* Parse an RMC sentence into pos (fills valid/lat/lon/speed/course/date/time).
 * 'valid' always follows the RMC status field ('A'=valid, 'V'=invalid).
 * Returns false if s is not a *RMC sentence. */
bool wm_nmea_parse_rmc(const char *s, WM_GPS_Position *pos);

#ifdef __cplusplus
}
#endif
#endif /* __WM_NMEA_H__ */
