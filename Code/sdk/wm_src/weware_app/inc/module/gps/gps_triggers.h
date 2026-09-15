/**
  ******************************************************************************
  * @file    gps_triggers.h
  * @author  WheelsEye
  * @brief   GPS send trigger reason codes (decider, events) - walnut port of
  *          the reference firmware's module/gps/gps_triggers.h.
  ******************************************************************************
  */

#ifndef WEWARE_GPS_TRIGGERS_H
#define WEWARE_GPS_TRIGGERS_H

#include "wm_sdk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GPS_TRIGGER_IGN_INTERVAL        1
#define GPS_TRIGGER_ANGLE               2
#define GPS_TRIGGER_DISTANCE            3
#define GPS_TRIGGER_SIM_REMOVED         4
#define GPS_TRIGGER_MSG_Q_TCP_APPEND    5
#define GPS_TRIGGER_CHARGE_CONNECTED    6
#define GPS_TRIGGER_CHARGE_DISCONNECTED 7
#define GPS_TRIGGER_SIM_INSERTED        8
#define GPS_TRIGGER_IGN_ON              9
#define GPS_TRIGGER_IGN_OFF             10
#define GPS_TRIGGER_GPS_FIX             11
#define GPS_TRIGGER_MOTION_ON           12
#define GPS_TRIGGER_MOTION_OFF          13

/** Whether this cycle should build and send a TCP GPS packet. */
BOOL gps_triggers_should_send(void);

/** Clear latched triggers after a successful TCP send. */
void gps_triggers_clear_after_send(void);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_GPS_TRIGGERS_H */
