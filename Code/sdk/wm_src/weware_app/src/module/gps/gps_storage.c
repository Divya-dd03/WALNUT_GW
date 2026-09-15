/**
  ******************************************************************************
  * @file    gps_storage.c
  * @author  WheelsEye
  * @brief   On-disk last valid @c GpsPacket for the GPS module - walnut port
  *          of the reference firmware's module/gps/gps_storage.c using the
  *          wm_sdk_file API (reference: file_system wrapper).
  *
  *          Walnut adaptations:
  *          - power-on-reset detection uses wm_sdk_get_reset_reason() ('N' =
  *            normal power-on; 0/unknown treated as power-on for safety).
  *            Reference: post_boot_handler_boot_is_power_on_reset().
  *          - RTC validity uses wm_sdk_network_rtc_get_utc_time (NITZ-synced).
  ******************************************************************************
  */

#include <string.h>

// sdk
#include "wm_global.h"
#include "wm_sdk_log.h"
#include "wm_sdk_file.h"
#include "wm_sdk_network.h"
#include "wm_sdk_system.h"

// app
#include "module/gps/gps_storage.h"
#include "module/gps/gps_config.h"
#include "module/gps/gps_manager.h"
#include "module/gps/gps_ops.h"
#include "common/utils.h"

extern gps_manager_runtime_t g_gps;

#define GPS_STORE_MAGIC   0x47505356u /* 'GPSV' */
#define GPS_STORE_VERSION 2u

#pragma pack(push, 1)
typedef struct {
    UINT32 magic;
    UINT32 version;
    GpsPacket packet;
} GpsStoredOnDisk;
#pragma pack(pop)

/*---------------------------------------------------------------
 * wm_sdk_file helpers (reference: file_system_write_file/read_file)
 *--------------------------------------------------------------*/
static wm_SdkResult gps_file_write(const char *path, const void *data, UINT32 len)
{
    void *f;
    UINT32 written = 0;
    wm_SdkResult r;

    /* Parent directory best-effort (exists -> mkdir fails silently) */
    (void)wm_sdk_file_mkdir("C:/config");

    f = wm_sdk_file_open(path, "wb");
    if (!f)
        return WM_SDK_RESULT_ERROR;

    r = wm_sdk_file_write(f, data, len, &written);
    if (r == WM_SDK_RESULT_SUCCESS && written != len)
        r = WM_SDK_RESULT_ERROR;
    (void)wm_sdk_file_sync(f);
    (void)wm_sdk_file_close(f);
    return r;
}

static wm_SdkResult gps_file_read(const char *path, void *data, UINT32 len, UINT32 *read_out)
{
    void *f;
    wm_SdkResult r;

    if (read_out)
        *read_out = 0;

    f = wm_sdk_file_open(path, "rb");
    if (!f)
        return WM_SDK_RESULT_ERROR;

    r = wm_sdk_file_read(f, data, len, read_out);
    (void)wm_sdk_file_close(f);
    return r;
}

/*---------------------------------------------------------------
 * Record read/write (verbatim reference logic)
 *--------------------------------------------------------------*/
static wm_SdkResult disk_write_record(const GpsStoredOnDisk *rec, const char *ctx)
{
    wm_SdkResult w = gps_file_write(GPS_LAST_VALID_FILE_PATH, rec, (UINT32)sizeof(*rec));
    if (w != WM_SDK_RESULT_SUCCESS)
        wm_sdk_log_error("GPS store %s: write %s ret=%d", ctx, GPS_LAST_VALID_FILE_PATH, (int)w);
    return w;
}

static BOOL disk_read_record(GpsStoredOnDisk *out)
{
    UINT32 n = 0;
    memset(out, 0, sizeof(*out));
    if (gps_file_read(GPS_LAST_VALID_FILE_PATH, out, (UINT32)sizeof(*out), &n)
            != WM_SDK_RESULT_SUCCESS)
        return FALSE;
    return (n == sizeof(*out) && out->magic == GPS_STORE_MAGIC &&
            out->version == GPS_STORE_VERSION);
}

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/
wm_SdkResult gps_storage_clear(void)
{
    if (wm_sdk_file_delete(GPS_LAST_VALID_FILE_PATH) != WM_SDK_RESULT_SUCCESS)
        wm_sdk_debug_print("GPS store clear: file absent or delete failed\r\n");
    return WM_SDK_RESULT_SUCCESS;
}

wm_SdkResult gps_storage_save_packet(const GpsPacket *pkt)
{
    if (!pkt || !gps_validate_coordinates(pkt->latitude_deg, pkt->longitude_deg))
        return WM_SDK_RESULT_SUCCESS;

    GpsPacket to_write;
    memcpy(&to_write, pkt, sizeof(to_write));

    /* Stamp with current UTC when the RTC is usable (reference: time_utils) */
    {
        wm_SdkNetworkTime now;
        memset(&now, 0, sizeof(now));
        if (wm_sdk_network_rtc_get_utc_time(&now) == WM_SDK_RESULT_SUCCESS) {
            UINT32 utc = utils_time_to_unix(&now);
            if (utc != 0u)
                to_write.utc_time = utc;
        }
    }

    GpsStoredOnDisk rec = {
        .magic   = GPS_STORE_MAGIC,
        .version = GPS_STORE_VERSION,
    };
    memcpy(&rec.packet, &to_write, sizeof(GpsPacket));
    return disk_write_record(&rec, "save");
}

wm_SdkResult gps_storage_save_last_valid(void)
{
    if (g_gps.last_valid_gps_data.utc_time == 0 ||
        !gps_validate_coordinates(g_gps.last_valid_gps_data.latitude_deg,
                                  g_gps.last_valid_gps_data.longitude_deg)) {
        wm_sdk_debug_print("GPS store save_last_valid: nothing valid\r\n");
        return WM_SDK_RESULT_SUCCESS;
    }
    return gps_storage_save_packet(&g_gps.last_valid_gps_data);
}

/*---------------------------------------------------------------
 * Post-boot
 *--------------------------------------------------------------*/
BOOL gps_post_boot_is_power_on_reset(void)
{
    UINT32 reason = wm_sdk_get_reset_reason();
    /* 'N' = normal power-on; 0 = not available (treated as power-on). */
    return (reason == (UINT32)'N' || reason == 0u) ? TRUE : FALSE;
}

const char *gps_post_boot_reset_reason_string(void)
{
    return wm_sdk_get_reset_reason_string(wm_sdk_get_reset_reason());
}

static BOOL gps_storage_rtc_utc_is_valid(UINT32 *out_utc)
{
    wm_SdkNetworkTime now;
    UINT32 utc;

    if (!out_utc)
        return FALSE;
    memset(&now, 0, sizeof(now));
    if (wm_sdk_network_rtc_get_utc_time(&now) != WM_SDK_RESULT_SUCCESS) {
        wm_sdk_debug_print("GPS post-boot: RTC read failed\r\n");
        return FALSE;
    }
    utc = utils_time_to_unix(&now);
    if (utc < GPS_TIME_VALID_MIN_UTC_UNIX) {
        wm_sdk_debug_print("GPS post-boot: RTC below threshold\r\n");
        return FALSE;
    }
    *out_utc = utc;
    return TRUE;
}

static void gps_storage_load_from_disk(void)
{
    GpsStoredOnDisk rec;

    if (!disk_read_record(&rec)) {
        wm_sdk_debug_print("GPS post-boot: no stored GPS location\r\n");
        return;
    }

    if (!gps_validate_coordinates(rec.packet.latitude_deg, rec.packet.longitude_deg)) {
        wm_sdk_log_warning("GPS post-boot: stored coordinates invalid, ignoring");
        return;
    }

    memcpy(&g_gps.last_valid_gps_data, &rec.packet, sizeof(GpsPacket));
    wm_sdk_log_info("GPS post-boot: loaded stored GPS location from %s", GPS_LAST_VALID_FILE_PATH);
}

void gps_storage_handle_post_boot(void)
{
    if (gps_post_boot_is_power_on_reset()) {
        UINT32 now_utc = 0;

        if (gps_storage_rtc_utc_is_valid(&now_utc)) {
            wm_sdk_log_info("GPS post-boot: power-on reset with valid RTC (utc=%lu) - keep stored GPS",
                         (unsigned long)now_utc);
            gps_storage_load_from_disk();
            return;
        }

        wm_sdk_log_info("GPS post-boot: power-on reset (RTC invalid) - clear stored GPS location");
        (void)gps_storage_clear();
        return;
    }

    gps_storage_load_from_disk();
}
