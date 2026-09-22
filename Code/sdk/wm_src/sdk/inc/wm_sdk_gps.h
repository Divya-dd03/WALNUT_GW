/**
 ******************************************************************************
 * @file    wm_sdk_gps.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - GPS / GNSS API.
 *
 *          The receiver is brought up once at boot by wm_sdk_gps_init(), called
 *          from wm_system_init(); every other call below reports
 *          WM_SDK_RESULT_NOT_INITIALIZED until that has happened. Fixes are read
 *          on demand with wm_sdk_gps_get_navdata(), or pushed to the application
 *          once per epoch through wm_sdk_gps_set_fix_callback().
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __WM_SDK_GPS_H__
#define __WM_SDK_GPS_H__

#include "wm_sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** GNSS part fitted. Selects the constellation bits below; the build fails if it
** does not match the receiver the SDK library was built for.
******************************************************************************/
#define WM_SDK_GPS_CHIP_BK1616P  1
#define WM_SDK_GPS_CHIP_CC1161W  2

#define WM_SDK_GPS_CURRENT_CHIP  WM_SDK_GPS_CHIP_CC1161W

/*******************************************************************************
** Constellation bits for wm_sdk_gps_set_mode(). OR together the systems wanted;
** at least one bit must be set. The values differ per receiver, so use the
** macros rather than the numbers they expand to.
******************************************************************************/
#if (WM_SDK_GPS_CURRENT_CHIP == WM_SDK_GPS_CHIP_BK1616P)

#define WM_SDK_GPS_SYS_GPS     (1uL << 0)   /* GPS L1     */
#define WM_SDK_GPS_SYS_BDS     (1uL << 2)   /* Beidou B1I */
#define WM_SDK_GPS_SYS_GLO     (1uL << 6)   /* GLONASS G1 */
#define WM_SDK_GPS_SYS_GAL     (1uL << 7)   /* Galileo E1 */

/* Every constellation bit the receiver accepts. */
#define WM_SDK_GPS_SYS_ALL   (WM_SDK_GPS_SYS_GPS | WM_SDK_GPS_SYS_BDS | \
                           WM_SDK_GPS_SYS_GLO | WM_SDK_GPS_SYS_GAL)

#elif (WM_SDK_GPS_CURRENT_CHIP == WM_SDK_GPS_CHIP_CC1161W)

/* One bit per signal, not per constellation. */
#define WM_SDK_GPS_SYS_GPS     (1uL << 0)   /* GPS L1C/A                   */
#define WM_SDK_GPS_SYS_BDS     (1uL << 4)   /* Beidou B1I                  */
#define WM_SDK_GPS_SYS_BDS_B1C (1uL << 7)   /* Beidou B1C (off by default) */
#define WM_SDK_GPS_SYS_GLO     (1uL << 8)   /* GLONASS L1                  */
#define WM_SDK_GPS_SYS_GAL     (1uL << 12)  /* Galileo E1                  */
#define WM_SDK_GPS_SYS_QZSS    (1uL << 16)  /* QZSS                        */
#define WM_SDK_GPS_SYS_SBAS    (1uL << 17)  /* SBAS                        */

/* Every constellation bit the receiver accepts - it is single-band L1. */
#define WM_SDK_GPS_SYS_ALL   (WM_SDK_GPS_SYS_GPS | WM_SDK_GPS_SYS_BDS  | \
                           WM_SDK_GPS_SYS_BDS_B1C | WM_SDK_GPS_SYS_GLO | \
                           WM_SDK_GPS_SYS_GAL | WM_SDK_GPS_SYS_QZSS | \
                           WM_SDK_GPS_SYS_SBAS)

#else
#error "WM_SDK_GPS_CURRENT_CHIP: unsupported part"
#endif

/*******************************************************************************
** Type Definitions
******************************************************************************/
/* Per-epoch fix notification, registered with wm_sdk_gps_set_fix_callback().
 * Invoked from the GNSS parser task once per position epoch, whether or not the
 * fix is valid ('nav->fix_valid' says which). 'nav' points at a stack copy that
 * is only valid for the duration of the call - copy out what must be kept. Do
 * not block: heavy work belongs in an application task. */
typedef void (*wm_sdk_gps_fix_cb_t)(const wm_SdkGpsNavData *nav);

/* Raw NMEA notification, registered with wm_sdk_gps_set_nmea_callback().
 * Invoked from the GNSS parser task for every complete sentence the receiver
 * sends, before it is checked or parsed - so malformed sentences are delivered
 * too. 'sentence' is NUL-terminated and only valid for the duration of the
 * call; 'len' excludes the terminator. Do not block. */
typedef void (*wm_sdk_gps_nmea_cb_t)(const char *sentence, UINT16 len);

/*******************************************************************************
** Functions
******************************************************************************/
/**
 * @brief  Bring up the GNSS subsystem: the receiver driver task and its UART,
 *         plus receiver power-on with the default configuration. Called once
 *         from wm_system_init() at boot (gated by WM_GPS_SUPPORT), so the
 *         application normally never calls it. Idempotent.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_gps_init(void);

/**
 * @brief  Register (or clear, with NULL) the per-epoch fix callback. Replaces
 *         any previously registered callback.
 * @param  cb  callback invoked from the GNSS parser task, or NULL to stop
 *             notifications.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_gps_init().
 */
wm_SdkResult wm_sdk_gps_set_fix_callback(wm_sdk_gps_fix_cb_t cb);

/**
 * @brief  Register (or clear, with NULL) the raw NMEA sentence callback.
 *         Independent of the fix callback: parsing continues either way, so
 *         both can be active at once. Replaces any previously registered
 *         callback.
 * @param  cb  callback invoked from the GNSS parser task once per received
 *             sentence, or NULL to stop notifications.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_gps_init().
 */
wm_SdkResult wm_sdk_gps_set_nmea_callback(wm_sdk_gps_nmea_cb_t cb);

/**
 * @brief  Get GNSS receiver power state. Reports the state tracked by the SDK
 *         (on after wm_sdk_gps_init(), and as last set by
 *         wm_sdk_gps_set_power_status()); the receiver exposes no power query.
 * @param  power_on  [out] 1=on, 0=off.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL pointer;
 *                     WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_gps_init().
 */
wm_SdkResult wm_sdk_gps_get_power_status(UINT8 *power_on);

/**
 * @brief  Power the GNSS receiver on or off. Powering off closes the receiver
 *         UART and stops NMEA output; powering back on re-applies the default
 *         configuration. Idempotent: asking for the state the receiver is
 *         already in succeeds and does nothing, so a redundant power-on cannot
 *         pulse the reset line or disturb a fix that is being tracked.
 * @param  power_on  1=power on, 0=power off.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM if @p power_on is not
 *                     0 or 1; WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_gps_init().
 */
wm_SdkResult wm_sdk_gps_set_power_status(UINT8 power_on);

/**
 * @brief  Set the GNSS constellation mode (e.g. GPS, GPS+GLONASS).
 * @param  mode  bitmask, OR of WM_SDK_GPS_SYS_* (at least one bit). A value from
 *               wm_sdk_gps_get_mode() can be written back unchanged.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM if @p mode is 0 or
 *                     carries a bit outside WM_SDK_GPS_SYS_ALL;
 *                     WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_gps_init().
 */
wm_SdkResult wm_sdk_gps_set_mode(UINT32 mode);

/**
 * @brief  Read the GNSS constellation mode back from the receiver. Queries the
 *         receiver rather than reporting what was last set, so it stays correct
 *         across a reboot - the receiver keeps this setting in its own flash.
 *         Needs the receiver powered on.
 * @param  mode  [out] bitmask of WM_SDK_GPS_SYS_* bits, exactly as reported. The
 *               receiver supports signals beyond WM_SDK_GPS_SYS_ALL, so a bit
 *               outside it is possible and is not filtered out.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL pointer;
 *                     WM_SDK_RESULT_TIMEOUT if the receiver does not answer (it
 *                     cannot while powered off); WM_SDK_RESULT_NOT_INITIALIZED
 *                     before wm_sdk_gps_init().
 */
wm_SdkResult wm_sdk_gps_get_mode(UINT32 *mode);

/**
 * @brief  Set the NMEA sentence output rate.
 * @param  rate  output rate in Hz. The accepted set depends on the receiver:
 *               1, 2, 4 or 5 on the CC1161W; 1, 5, 10 or 20 on the BK1616P.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM for any other rate;
 *                     WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_gps_init().
 */
wm_SdkResult wm_sdk_gps_set_nmea_rate(UINT32 rate);

/**
 * @brief  Restart the receiver in the given start mode. This receiver
 *         distinguishes only cold from non-cold, so HOT and WARM behave
 *         identically (both keep the retained almanac/ephemeris).
 * @param  mode  0=HOT, 1=WARM, 2=COLD start.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM if @p mode is above
 *                     2; WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_gps_init().
 */
wm_SdkResult wm_sdk_gps_start_mode(UINT32 mode);

/**
 * @brief  Start the NMEA output stream so location data begins flowing. Both
 *         data-get modes behave the same here: the SDK owns the receiver UART
 *         and parses NMEA internally, so location reaches the application
 *         through wm_sdk_gps_get_navdata() / the fix callback rather than over a
 *         serial port.
 * @param  data_get_mode  0=serial port, 1=URC.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM if @p data_get_mode
 *                     is not 0 or 1; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_gps_init().
 */
wm_SdkResult wm_sdk_gps_enable_nmea_output(UINT32 data_get_mode);

/**
 * @brief  Set the periodic GNSS info report interval.
 * @param  period_sec  report period in seconds (0-255).
 * @return wm_SdkResult - always WM_SDK_RESULT_NOT_SUPPORTED: this receiver has no
 *                     periodic-report command. Read fixes on demand with
 *                     wm_sdk_gps_get_navdata(), or take every epoch through
 *                     wm_sdk_gps_set_fix_callback() and throttle in the
 *                     application.
 */
wm_SdkResult wm_sdk_gps_set_gnss_info_period(UINT32 period_sec);

/**
 * @brief  Queue an A-GNSS fetch + inject to speed up the next fix. Non-blocking;
 *         runs on the assistance task. Skips the re-fetch interval but not the
 *         daily request cap. Deferred while the receiver is powered off.
 * @return wm_SdkResult - 0 on success, negative on failure.
 */
wm_SdkResult wm_sdk_gps_open_agps_service(void);

/**
 * @brief  Enable/disable saving ephemeris to AP flash for faster hot starts.
 * @param  enable  TRUE=enable, FALSE=disable.
 * @return wm_SdkResult - always WM_SDK_RESULT_NOT_SUPPORTED: the receiver keeps its
 *                     own ephemeris across restarts and exposes no AP-flash
 *                     caching path.
 */
wm_SdkResult wm_sdk_gps_set_ap_flash_hot_start(BOOL enable);

/**
 * @brief  Get the last known navigation data from the receiver. @p nav_data is
 *         always populated (zeroed until the first epoch arrives), so the
 *         'fix_valid' field can be used instead of the return code.
 *         The data is the last epoch the receiver committed and is not cleared
 *         by wm_sdk_gps_set_power_status(0), so after a power down this keeps
 *         reporting the last known position. Pair it with
 *         wm_sdk_gps_get_power_status() where freshness matters.
 * @param  nav_data  [out] fix flag, lat, lon, altitude, speed, course, sats, utc.
 * @return wm_SdkResult - 0 on a valid fix; WM_SDK_RESULT_BUSY while the receiver is
 *                     running but has no valid fix yet (still acquiring);
 *                     WM_SDK_RESULT_INVALID_PARAM on a NULL pointer;
 *                     WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_gps_init().
 */
wm_SdkResult wm_sdk_gps_get_navdata(wm_SdkGpsNavData *nav_data);

#ifdef __cplusplus
}
#endif

#endif /* __WM_SDK_GPS_H__ */
