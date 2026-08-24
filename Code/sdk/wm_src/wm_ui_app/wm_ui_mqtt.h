/**
  ******************************************************************************
  * @file    wm_ui_mqtt.h
  * @author  Walnut Medical
  * @brief   Common Gateway (WEGW) reference application - MQTT demo.
  *
  *          One demo: connect to the broker and stream the GNSS receiver's raw
  *          NMEA sentences to it. Called from the "UIPROC" dispatcher in
  *          wm_ui_app.c.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */
#ifndef __WM_UI_MQTT__H__
#define __WM_UI_MQTT__H__

/*******************************************************************************
** Header Files
******************************************************************************/
#include "wm_global.h"
#include "sdk_api.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** Menu handler
******************************************************************************/
/* MQTT demo, run from the "UIPROC" dispatcher task.
 *
 * First run: connect to the broker and start streaming raw NMEA to it.
 * Re-running stops the stream, so the one option covers both directions. */
void wm_ui_mqtt_demo(void);

/*******************************************************************************
** Cross-demo coordination
******************************************************************************/
/* Stop the NMEA stream without touching the GNSS callback.
 *
 * The SDK holds a single raw-NMEA callback, so any other demo that registers
 * one takes it over from this stream. The GPS demos call this first so the
 * reported stream state stays truthful instead of claiming to publish
 * sentences it no longer receives. */
void wm_ui_mqtt_nmea_stream_stop(void);

/* TRUE while this demo owns the SDK raw-NMEA callback. */
BOOL wm_ui_mqtt_nmea_stream_active(void);

#ifdef __cplusplus
}
#endif

#endif /* __WM_UI_MQTT__H__ */
