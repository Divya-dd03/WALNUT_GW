/**
  ******************************************************************************
  * @file    wm_ui_ble.h
  * @author  Walnut Medical
  * @brief   Common Gateway (WEGW) reference application - BLE demos.
  *
  *          Declares the menu handlers defined in wm_ui_ble.c, run from the
  *          "UIPROC" dispatcher in wm_ui_app.c.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 Walnut Medical.
  * All rights reserved.
  *
  ******************************************************************************
  */
#ifndef __WM_UI_BLE__H__
#define __WM_UI_BLE__H__

/*******************************************************************************
** Header Files
******************************************************************************/
#include "wm_global.h"
#include "wm_sdk_api.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** Menu handlers
******************************************************************************/
/* Start or stop scanning for the fuel probes. Powers BLE up on first use. */
void wm_ui_ble_scan_demo(void);

/* Start or stop the background task that prints the latest fuel readings and
 * runs an Autoguard health exchange on a fixed period. Returns immediately. */
void wm_ui_ble_monitor_demo(void);

/* Stop the monitor, drop any link and power BLE down. */
void wm_ui_ble_power_off_demo(void);

#ifdef __cplusplus
}
#endif

#endif /* __WM_UI_BLE__H__ */
