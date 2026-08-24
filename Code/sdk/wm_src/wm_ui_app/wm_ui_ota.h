/**
  ******************************************************************************
  * @file    wm_ui_ota.h
  * @author  Walnut Medical
  * @brief   Common Gateway (WEGW) reference application - OTA / DFOTA demos.
  *
  *          Declares the three menu handlers defined in wm_ui_ota.c, each run
  *          from the "UIPROC" dispatcher in wm_ui_app.c.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */
#ifndef __WM_UI_OTA__H__
#define __WM_UI_OTA__H__

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
** Menu handlers
******************************************************************************/
/* Print the application and platform SDK version strings, and what is staged. */
void wm_ui_ota_version_demo(void);

/* Full application update in one option: prompt for the image URL, download it
 * to C: staging, prompt for the expected SHA-256, verify, then arm and reboot.
 * Prompts, so it runs on the dispatcher and blocks the menu until it finishes. */
void wm_ui_ota_update_demo(void);

/* The same flow for the kernel delta patch (DFOTA). */
void wm_ui_dfota_update_demo(void);

#ifdef __cplusplus
}
#endif

#endif /* __WM_UI_OTA__H__ */
