/**
 ******************************************************************************
 * @file    sdk_wm.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - WM platform glue.
 *
 *          Declares the app-side entry that brings up the UI command task and
 *          queue. The implementation also provides the symbols the prebuilt
 *          lib_wmsrc.a calls up into (URC pump, key-event handlers, watchdog
 *          flag, power-key callback).
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __SDK_WM_H__
#define __SDK_WM_H__

#include "sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* URC event codes delivered (by pointer / value) to the g_wm_*_cb callbacks
 * from the URC task. */
typedef enum
{
    URC_PDP_ACTIVE = 0,
    URC_PDP_INACTIVE,
    URC_NET_ACTIVE,
    URC_NET_DISCONNECTED,
    URC_SIM_INSERTED,
    URC_SIM_REMOVED,
    URC_SIM_READY,
    URC_SIM_EJECTED_FOR_LONG_TIME,
    URC_USB_PLUGGED,
    URC_USB_REMOVED,
    URC_NO_PDP_FOR_LONG_TIME,
    URC_RADIO_REFRESH,
} urcEvent_e;

/* Single application URC/event callback (the customer SDK hook). Invoked from
 * the URC task with the event code; pass NULL to clear. */
typedef void (*sdk_urc_cb_t)(urcEvent_e event);

/**
 * @brief  Register (or clear, with NULL) the application URC event callback.
 */
void sdk_set_urc_callback(sdk_urc_cb_t cb);

/**
 * @brief  Internal: forward a URC event to the queues registered via
 *         sdk_urc_register(). Not for application use.
 */
void sdk_urc_dispatch(urcEvent_e event);

/**
 * @brief  Boot-time system bring-up, called from appimg_enter() for non-LIB
 *         builds: power-key registration, module HW config, soundbox power-on,
 *         battery startup, IMEI, SDK version, RTC auto-update and - when
 *         WM_GPS_SUPPORT is set - GNSS bring-up via sdk_gps_init().
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult wm_system_init(void);

/**
 * @brief  Create the UI command queue (WM_UI_msgq) and the "UIPROC" dispatcher
 *         task (sTask_WM_UIProcesser). Idempotent.
 */
void wm_ui_app_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __SDK_WM_H__ */
