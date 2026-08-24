/**
 * @file system_config.h
 * @brief System-level configuration: ignition and motion detection sources/thresholds
 */

#ifndef WEWARE_SYSTEM_CONFIG_H
#define WEWARE_SYSTEM_CONFIG_H

#include "common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYSTEM_DEFAULT_WIRE_ON_V           5.0f
#define SYSTEM_DEFAULT_WIRE_OFF_V          4.9f
#define SYSTEM_DEFAULT_SOFT_LO_V           12.7f
#define SYSTEM_DEFAULT_SOFT_HI_V           15.0f
#define SYSTEM_DEFAULT_DEBOUNCE_POLLS      5u
#define SYSTEM_DEFAULT_MOT_SPD_KMH         5.0f
#define SYSTEM_DEFAULT_MOT_DIST_M          200.0f
/* accel_g: variance-based motion sensitivity floor, expressed as a combined
 * three-axis standard deviation (g). Variance floor = accel_g^2 (0.05g -> 0.0025 g^2).
 * Lower = more sensitive (risk false "moving"); higher = less sensitive. */
#define SYSTEM_DEFAULT_ACCEL_G           0.05f
/* accel_dur_ms: motion "up" debounce window; converted to ~1 s poll count
 * (2000 ms -> 2 polls) before the moving flag latches on. */
#define SYSTEM_DEFAULT_ACCEL_DUR_MS        2000u
#define SYSTEM_DEFAULT_MOT_STOP_DEBOUNCE_SEC 5u

typedef enum {
    SYSTEM_IGN_SOURCE_WIRE = 0,
    SYSTEM_IGN_SOURCE_SOFT = 1,
} SystemIgnSource;

typedef enum {
    SYSTEM_MOT_SOURCE_IGN = 0,
    SYSTEM_MOT_SOURCE_SOFT = 1,
} SystemMotionSource;

typedef enum {
    SYSTEM_SOFT_MOT_GNSS_SPEED = 0,
    SYSTEM_SOFT_MOT_ACCEL = 1,
    SYSTEM_SOFT_MOT_GPS_DIST = 2,
    SYSTEM_SOFT_MOT_TRIP = 3,
    SYSTEM_SOFT_MOT_ANY = 4,
} SystemSoftMotionMethod;

typedef struct {
    SystemIgnSource ign_source;
    float wire_on_v;
    float wire_off_v;
    float soft_lo_v;
    float soft_hi_v;
    UINT8 debounce_polls;

    SystemMotionSource motion_source;
    SystemSoftMotionMethod soft_motion_method;
    float mot_spd_kmh;
    float mot_dist_m;
    float accel_g;          /**< Accel motion sensitivity floor (combined 3-axis std-dev, g). Variance floor = accel_g^2. */
    UINT32 accel_dur_ms;    /**< Accel motion "up" debounce window (ms); converted to ~1 s poll count. */
    UINT32 mot_stop_debounce_sec;
    /** TRUE: speed from distance vs last sent location; FALSE: GNSS-reported speed. */
    BOOL mot_spd_calc;
    BOOL mot_spd_en;
    BOOL mot_dist_en;
    BOOL accel_en;
} SystemConfig;

extern SystemConfig g_system_config;

void system_config_get_defaults(void *config);
Result system_config_validate(const void *config);
SystemConfig *system_config_get_storage(void);
Result system_config_set(const char *config_string);
Result system_config_get_string(char *buffer, size_t buffer_size);

/** Copy soft ignition thresholds from TCP config when system config still at defaults. */
void system_config_migrate_from_tcp_if_needed(void);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_SYSTEM_CONFIG_H */
