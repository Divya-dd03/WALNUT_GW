/**
 ******************************************************************************
 * @file    sdk_gps.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - GPS / GNSS API.
 *
 *          The receiver is brought up once at boot by sdk_gps_init(), called
 *          from wm_system_init(); every other call below reports
 *          SDK_RESULT_NOT_INITIALIZED until that has happened. Fixes are read
 *          on demand with sdk_gps_get_navdata(), or pushed to the application
 *          once per epoch through sdk_gps_set_fix_callback().
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __SDK_GPS_H__
#define __SDK_GPS_H__

#include "sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** Constellation bits for sdk_gps_set_mode(). OR together the systems wanted;
** at least one bit must be set.
******************************************************************************/
#define SDK_GPS_SYS_GPS   (1u << 0)   /* GPS L1     */
#define SDK_GPS_SYS_BDS   (1u << 2)   /* Beidou B1I */
#define SDK_GPS_SYS_GLO   (1u << 6)   /* GLONASS G1 */
#define SDK_GPS_SYS_GAL   (1u << 7)   /* Galileo E1 */

/* Every constellation bit the receiver accepts. */
#define SDK_GPS_SYS_ALL   (SDK_GPS_SYS_GPS | SDK_GPS_SYS_BDS | \
                           SDK_GPS_SYS_GLO | SDK_GPS_SYS_GAL)

/*******************************************************************************
** Type Definitions
******************************************************************************/
/* Per-epoch fix notification, registered with sdk_gps_set_fix_callback().
 * Invoked from the GNSS parser task once per position epoch, whether or not the
 * fix is valid ('nav->fix_valid' says which). 'nav' points at a stack copy that
 * is only valid for the duration of the call - copy out what must be kept. Do
 * not block: heavy work belongs in an application task. */
typedef void (*sdk_gps_fix_cb_t)(const SdkGpsNavData *nav);

/* Raw NMEA notification, registered with sdk_gps_set_nmea_callback().
 * Invoked from the GNSS parser task for every complete sentence the receiver
 * sends, before it is checked or parsed - so malformed sentences are delivered
 * too. 'sentence' is NUL-terminated and only valid for the duration of the
 * call; 'len' excludes the terminator. Do not block. */
typedef void (*sdk_gps_nmea_cb_t)(const char *sentence, UINT16 len);

/*******************************************************************************
** Functions
******************************************************************************/
/**
 * @brief  Bring up the GNSS subsystem: the receiver driver task and its UART,
 *         plus receiver power-on with the default configuration. Called once
 *         from wm_system_init() at boot (gated by WM_GPS_SUPPORT), so the
 *         application normally never calls it. Idempotent.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_gps_init(void);

/**
 * @brief  Register (or clear, with NULL) the per-epoch fix callback. Replaces
 *         any previously registered callback.
 * @param  cb  callback invoked from the GNSS parser task, or NULL to stop
 *             notifications.
 * @return SdkResult - 0 success; SDK_RESULT_NOT_INITIALIZED before
 *                     sdk_gps_init().
 */
SdkResult sdk_gps_set_fix_callback(sdk_gps_fix_cb_t cb);

/**
 * @brief  Register (or clear, with NULL) the raw NMEA sentence callback.
 *         Independent of the fix callback: parsing continues either way, so
 *         both can be active at once. Replaces any previously registered
 *         callback.
 * @param  cb  callback invoked from the GNSS parser task once per received
 *             sentence, or NULL to stop notifications.
 * @return SdkResult - 0 success; SDK_RESULT_NOT_INITIALIZED before
 *                     sdk_gps_init().
 */
SdkResult sdk_gps_set_nmea_callback(sdk_gps_nmea_cb_t cb);

/**
 * @brief  Get GNSS receiver power state. Reports the state tracked by the SDK
 *         (on after sdk_gps_init(), and as last set by
 *         sdk_gps_set_power_status()); the receiver exposes no power query.
 * @param  power_on  [out] 1=on, 0=off.
 * @return SdkResult - 0 success; SDK_RESULT_INVALID_PARAM on a NULL pointer;
 *                     SDK_RESULT_NOT_INITIALIZED before sdk_gps_init().
 */
SdkResult sdk_gps_get_power_status(UINT8 *power_on);

/**
 * @brief  Power the GNSS receiver on or off. Powering off closes the receiver
 *         UART and stops NMEA output; powering back on re-applies the default
 *         configuration. Idempotent: asking for the state the receiver is
 *         already in succeeds and does nothing, so a redundant power-on cannot
 *         pulse the reset line or disturb a fix that is being tracked.
 * @param  power_on  1=power on, 0=power off.
 * @return SdkResult - 0 success; SDK_RESULT_INVALID_PARAM if @p power_on is not
 *                     0 or 1; SDK_RESULT_NOT_INITIALIZED before sdk_gps_init().
 */
SdkResult sdk_gps_set_power_status(UINT8 power_on);

/**
 * @brief  Set the GNSS constellation mode (e.g. GPS, GPS+GLONASS).
 * @param  mode  bitmask, OR of SDK_GPS_SYS_* (at least one bit).
 * @return SdkResult - 0 success; SDK_RESULT_INVALID_PARAM if @p mode is 0 or
 *                     carries a bit outside SDK_GPS_SYS_ALL;
 *                     SDK_RESULT_NOT_INITIALIZED before sdk_gps_init().
 */
SdkResult sdk_gps_set_mode(UINT32 mode);

/**
 * @brief  Set the NMEA sentence output rate.
 * @param  rate  output rate in Hz; the receiver accepts only 1, 5, 10 or 20.
 * @return SdkResult - 0 success; SDK_RESULT_INVALID_PARAM for any other rate;
 *                     SDK_RESULT_NOT_INITIALIZED before sdk_gps_init().
 */
SdkResult sdk_gps_set_nmea_rate(UINT32 rate);

/**
 * @brief  Restart the receiver in the given start mode. This receiver
 *         distinguishes only cold from non-cold, so HOT and WARM behave
 *         identically (both keep the retained almanac/ephemeris).
 * @param  mode  0=HOT, 1=WARM, 2=COLD start.
 * @return SdkResult - 0 success; SDK_RESULT_INVALID_PARAM if @p mode is above
 *                     2; SDK_RESULT_NOT_INITIALIZED before sdk_gps_init().
 */
SdkResult sdk_gps_start_mode(UINT32 mode);

/**
 * @brief  Start the NMEA output stream so location data begins flowing. Both
 *         data-get modes behave the same here: the SDK owns the receiver UART
 *         and parses NMEA internally, so location reaches the application
 *         through sdk_gps_get_navdata() / the fix callback rather than over a
 *         serial port.
 * @param  data_get_mode  0=serial port, 1=URC.
 * @return SdkResult - 0 success; SDK_RESULT_INVALID_PARAM if @p data_get_mode
 *                     is not 0 or 1; SDK_RESULT_NOT_INITIALIZED before
 *                     sdk_gps_init().
 */
SdkResult sdk_gps_enable_nmea_output(UINT32 data_get_mode);

/**
 * @brief  Set the periodic GNSS info report interval.
 * @param  period_sec  report period in seconds (0-255).
 * @return SdkResult - always SDK_RESULT_NOT_SUPPORTED: this receiver has no
 *                     periodic-report command. Read fixes on demand with
 *                     sdk_gps_get_navdata(), or take every epoch through
 *                     sdk_gps_set_fix_callback() and throttle in the
 *                     application.
 */
SdkResult sdk_gps_set_gnss_info_period(UINT32 period_sec);

/**
 * @brief  Open the A-GPS assistance service to speed up first fix.
 * @return SdkResult - always SDK_RESULT_NOT_SUPPORTED: the on-board receiver
 *                     has no assistance-server path on this hardware.
 */
SdkResult sdk_gps_open_agps_service(void);

/**
 * @brief  Enable/disable saving ephemeris to AP flash for faster hot starts.
 * @param  enable  TRUE=enable, FALSE=disable.
 * @return SdkResult - always SDK_RESULT_NOT_SUPPORTED: the receiver keeps its
 *                     own ephemeris across restarts and exposes no AP-flash
 *                     caching path.
 */
SdkResult sdk_gps_set_ap_flash_hot_start(BOOL enable);

/**
 * @brief  Get the last known navigation data from the receiver. @p nav_data is
 *         always populated (zeroed until the first epoch arrives), so the
 *         'fix_valid' field can be used instead of the return code.
 *         The data is the last epoch the receiver committed and is not cleared
 *         by sdk_gps_set_power_status(0), so after a power down this keeps
 *         reporting the last known position. Pair it with
 *         sdk_gps_get_power_status() where freshness matters.
 * @param  nav_data  [out] fix flag, lat, lon, altitude, speed, course, sats, utc.
 * @return SdkResult - 0 on a valid fix; SDK_RESULT_BUSY while the receiver is
 *                     running but has no valid fix yet (still acquiring);
 *                     SDK_RESULT_INVALID_PARAM on a NULL pointer;
 *                     SDK_RESULT_NOT_INITIALIZED before sdk_gps_init().
 */
SdkResult sdk_gps_get_navdata(SdkGpsNavData *nav_data);

#ifdef __cplusplus
}
#endif

#endif /* __SDK_GPS_H__ */
