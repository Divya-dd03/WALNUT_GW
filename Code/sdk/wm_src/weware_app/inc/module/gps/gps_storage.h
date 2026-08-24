/**
  ******************************************************************************
  * @file    gps_storage.h
  * @author  WheelsEye
  * @brief   Persist last valid GPS position - walnut port of the reference
  *          firmware's module/gps/gps_storage.h (GPS module only).
  *
  * Runtime:
  * - fix -> no fix (@c GPS_FIX_TRANSITION_DISCONNECTED): save last valid packet
  * - no fix -> fix and motion OFF: update stored packet; motion ON: clear
  * - fix held, motion edge (@c gps_ops_loc_storage_update_on_motion_changed):
  *   ON clears storage, OFF saves current fix
  *
  * Post-boot (@c gps_storage_handle_post_boot):
  * - power-on reset: clear storage unless RTC still valid (instant/brief power loss)
  * - otherwise: load into @c g_gps.last_valid_gps_data
  ******************************************************************************
  */

#ifndef WEWARE_GPS_STORAGE_H
#define WEWARE_GPS_STORAGE_H

#include "sdk_types.h"
#include "module/gps/gps_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Last-valid file (walnut FS roots at "C:/" internal storage;
 *  reference: flash_paths.h C:/config/gps_last_valid.bin). */
#define GPS_LAST_VALID_FILE_PATH  "C:/config/gps_last_valid.bin"

/** Post-boot load or clear based on reset reason and RTC validity. */
void gps_storage_handle_post_boot(void);

/** Write @a pkt to disk; no-op if coordinates invalid. */
SdkResult gps_storage_save_packet(const GpsPacket *pkt);

/** Save @c g_gps.last_valid_gps_data when valid; used on fix lost. */
SdkResult gps_storage_save_last_valid(void);

/** Delete stored file (best-effort). */
SdkResult gps_storage_clear(void);

/*---------------------------------------------------------------
 * Post-boot helpers (reference: system/reset/post_boot_handler)
 *--------------------------------------------------------------*/
/** TRUE when this boot is a normal power-on ('N' or unknown reset reason). */
BOOL gps_post_boot_is_power_on_reset(void);

/** Human-readable reset reason (walnut sdk_get_reset_reason_string). */
const char *gps_post_boot_reset_reason_string(void);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_GPS_STORAGE_H */
