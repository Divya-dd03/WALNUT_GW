/**
 * @file gps_urc_queue_types.h
 * @brief GPS NMEA queue element: one combined NMEA record (RMC + GGA pair).
 *
 * Reference (CG) fills this from urc_processor after reassembling modem URC
 * fragments. Walnut delta: the kernel has no GNSS URCs - complete sentences
 * arrive through sdk_gps_set_nmea_callback() (GNSS parser task), are paired
 * in gps_manager.c and queue_push()ed here; no fragment reassembly and no
 * sdk_msg_t header are needed. Payload is NUL-terminated text.
 */

#ifndef WEWARE_GPS_URC_QUEUE_TYPES_H
#define WEWARE_GPS_URC_QUEUE_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

/** One full NMEA sentence (spec <=82; margin for talker variants) */
#define GPS_URC_SENTENCE_MAX 96U
/** Combined payload: RMC line + newline + GGA line + NUL */
#define GPS_URC_COMBINED_MAX (GPS_URC_SENTENCE_MAX * 2U + 8U)

typedef struct {
    /** "$GNRMC,...\n$GNGGA,..." - only full sentences, one queue entry per pair */
    char combined[GPS_URC_COMBINED_MAX];
} gps_urc_queued_t;

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_GPS_URC_QUEUE_TYPES_H */
