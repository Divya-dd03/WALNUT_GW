/**
  ******************************************************************************
  * @file    gps_config.h
  * @author  WheelsEye
  * @brief   GPS module constants and configuration - walnut port of the
  *          reference firmware's module/gps/gps_config.h. Single place for
  *          all tunable and fixed values; packet format stays in gps_packet.h.
  ******************************************************************************
  */

#ifndef WEWARE_GPS_CONFIG_H
#define WEWARE_GPS_CONFIG_H

#include <stddef.h>

#include "sdk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * GPS module constants
 * ============================================================================= */

/** Task and loop */
#define GPS_TASK_STACK_SIZE           4096   /**< Stack size (bytes) */
#define GPS_TASK_LOOP_SLEEP_MS        1000   /**< Main task loop period (ms) */
#define GPS_NMEA_RATE_DEFAULT         1      /**< NMEA output rate (1 = 1 Hz) */

/** Walnut: sdk_gps_enable_nmea_output() data-get mode. Both modes behave the
 *  same on this receiver (the SDK owns the receiver UART and parses NMEA
 *  internally); reference SIMCOM used BY_URC / BY_PORT here. */
#define GPS_WALNUT_NMEA_DATA_GET_MODE 0u
/** Reference: sAPI_GnssInfoGet period; walnut stub returns NOT_SUPPORTED. */
#define GPS_GNSS_INFO_PERIOD_S        1u

/** Last-valid location and packet build */
#define GPS_LAST_VALID_MIN_SATS       4      /**< Minimum satellite count when sending last-valid location packet */

/** Unit conversion (speed) */
#define GPS_KMH_TO_KNOTS             1.852f  /**< 1 knot = this many km/h */

/** Binary packet encoding (used by gps_packet.c) */
#define GPS_LAT_LON_FIXED_SCALE       1800000.0  /**< Lat/lon fixed-point scale */

/** GNSS start modes (walnut sdk_gps_start_mode() takes 0/1/2 directly;
 *  reference gets these from sdk_platform). */
#define SDK_GNSS_START_HOT            0u
#define SDK_GNSS_START_WARM           1u
#define SDK_GNSS_START_COLD           2u

/** Default configuration values (reference values; overridable via config string) */
#define GPS_DEFAULT_IGN_ON_INTERVAL_SEC   10    /**< Default IGN ON interval (sec) */
#define GPS_DEFAULT_IGN_OFF_INTERVAL_SEC  30    /**< Default IGN OFF interval (sec) */
#define GPS_DEFAULT_ANGLE_THRESHOLD_DEG   20.0f /**< Default angle change threshold (degrees) */
#define GPS_DEFAULT_DISTANCE_THRESHOLD_M  50.0f /**< Default distance change threshold (meters) */
#define GPS_DEFAULT_ENABLE_ANGLE_TRIGGER  TRUE  /**< Default: angle trigger on */
#define GPS_DEFAULT_ENABLE_DISTANCE_TRIGGER TRUE /**< Default: distance trigger on */
#define GPS_DEFAULT_ENABLE_AGPS           TRUE  /**< Default: A-GPS on (walnut: NOT_SUPPORTED, see gps_ops) */
#define GPS_DEFAULT_AGPS_REF              FALSE /**< Default: do not refresh agps every 4h */
#define GPS_AGPS_VALIDITY_SEC             (4u * 3600u)  /**< A-GPS data validity (sec) */
#define GPS_AGPS_FAIL_COOLDOWN_SEC        600u  /**< After a failed open, do not retry for this long (sec) */
#define GPS_GNSS_POWER_ON_PRE_DELAY_MS    2000u /**< Delay before first GNSS power-on (ms) */
#define GPS_GNSS_POWER_STABILIZE_MS       2000u /**< Delay after power-on before NMEA configure (ms) */
#define GPS_DEFAULT_START_MODE            SDK_GNSS_START_HOT /**< Default start mode: HOT */
#define GPS_DEFAULT_SEND_LAST_VALID_ON_BOOT_NO_FIX     TRUE  /**< Default: send last valid after boot when no fix */
#define GPS_DEFAULT_SEND_LAST_VALID_ON_IGN_OFF_NO_MOVEMENT FALSE /**< Default: send last valid on IGN off, no movement */
#define GPS_DEFAULT_SEND_LAST_VALID_ON_NO_FIX          TRUE  /**< Default: send last valid on generic no-fix trigger */
#define GPS_DEFAULT_TIME_SOURCE               0     /**< Default timestamp source: AUTO */
#define GPS_DEFAULT_SPEED_FILTER_KMH          5.0f  /**< Speed-based send filter (0 = disabled) */
/** Reject GPS/GSM UTC values older than 2020-01-01. */
#define GPS_TIME_VALID_MIN_UTC_UNIX 1577836800u

/** Reference module_manager.h MODULE_MESSAGE_INLINE_SIZE; local until the
 *  module manager / message queues are ported. TODO(gps) */
#define MODULE_MESSAGE_INLINE_SIZE    256

/* ============================================================================
 * Types
 * ============================================================================ */

typedef enum
{
    GPS_TIME_SOURCE_AUTO = 0, /**< Choose valid time automatically (GPS preferred, fallback GSM). */
    GPS_TIME_SOURCE_GPS  = 1, /**< Use GPS UTC only when valid. */
    GPS_TIME_SOURCE_GSM  = 2, /**< Use GSM/network UTC API only when valid. */
} GpsTimeSource;

/**
 * @brief GPS module configuration structure
 * @note Contains GPS Trigger Conditions and Data Send Policy
 */
typedef struct
{
    /* GPS Trigger Conditions */
    int ign_on_interval_sec;          /**< Time interval for ign_on case (seconds) */
    int ign_off_interval_sec;         /**< Time interval for ign_off case (seconds) */
    float angle_threshold_deg;        /**< Angle change threshold in degrees */
    float distance_threshold_m;       /**< Distance change threshold in meters */
    BOOL enable_angle_trigger;        /**< Enable angle-based triggering */
    BOOL enable_distance_trigger;     /**< Enable distance-based triggering */
    BOOL enable_agps;                 /**< Enable A-GPS initialization */
    BOOL agps_ref;                    /**< If TRUE, clear A-GPS every 4 h without fix for re-open */
    UINT32 start_mode;                /**< GPS start mode: 0=HOT, 1=WARM, 2=COLD */

    /* Data send policy: when to send last valid location */
    BOOL send_last_valid_on_boot_no_fix;         /**< Config only; not wired in send policy yet */
    BOOL send_last_valid_on_ign_off_no_movement; /**< Send last valid when IGN off interval reached but no movement */
    BOOL send_last_valid_on_no_fix;              /**< Send last valid when trigger fires but no fix (generic) */
    GpsTimeSource time_source;                   /**< Packet timestamp source: AUTO/GPS/GSM */
    /** 0 = off; else send last valid when calc speed (vs last sent loc) is below this (km/h). */
    float speed_filter_kmh;

    /* Note: IGN trigger is always enabled (mandatory). Last-valid persistence: gps_storage.h. */
} GpsConfig;

/* ============================================================================
 * API
 * ============================================================================ */

/**
 * @brief Get GPS module default configuration
 * @param config Output GPS configuration structure (must not be NULL)
 */
void gps_config_get_defaults(GpsConfig *config);

/**
 * @brief  TRUE once defaults (or a config-file load) initialized g_gps_config.
 */
BOOL gps_config_defaults_applied(void);

/**
 * @brief Set GPS configuration parameters from comma-separated "key:value" string
 *        Keys: i-on, i-off, ang, dist, ang-tr, dist-tr, agps, start, lv-boot,
 *        lv-ign-off, lv-no-fix, time-src, agps-ref, spd-filt. Partial updates
 *        allowed; all provided values validate before any is applied.
 * @return SDK_RESULT_SUCCESS if valid and set, SDK_RESULT_INVALID_PARAM otherwise
 */
SdkResult gps_config_set(const char *config_string);

/**
 * @brief Get GPS configuration parameters as comma-separated string
 * @return SDK_RESULT_SUCCESS; SDK_RESULT_INVALID_PARAM on bad args;
 *         SDK_RESULT_ERROR if buffer is too small
 */
SdkResult gps_config_get_string(char *buffer, size_t buffer_size);

/**
 * @brief Validate GPS configuration
 * @return SDK_RESULT_SUCCESS if valid, SDK_RESULT_INVALID_PARAM if invalid
 */
SdkResult gps_config_validate(const GpsConfig *config);

/**
 * @brief Get GPS module configuration storage pointer
 */
GpsConfig *gps_config_get_storage(void);

/** GPS module configuration storage (defined in gps_config.c) */
extern GpsConfig g_gps_config;

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_GPS_CONFIG_H */
