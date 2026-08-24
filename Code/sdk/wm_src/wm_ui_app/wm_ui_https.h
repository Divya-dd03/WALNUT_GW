/**
  ******************************************************************************
  * @file    wm_ui_https.h
  * @author  Walnut Medical
  * @brief   Common Gateway (WEGW) reference application - HTTPS demos.
  *
  *          Declares the four menu handlers defined in wm_ui_https.c, each run
  *          from the "UIPROC" dispatcher in wm_ui_app.c.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */
#ifndef __WM_UI_HTTPS__H__
#define __WM_UI_HTTPS__H__

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
/* GET a JSON endpoint synchronously and print the reply. */
void wm_ui_https_get_demo(void);

/* POST a JSON body with an API-key header, and print the reply. */
void wm_ui_https_post_demo(void);

/* Queue the same GET asynchronously and return to the menu. The result is
 * delivered on a queue and printed by this demo's "HTTPMON" task. */
void wm_ui_https_async_demo(void);

/* Fetch a file a range at a time, writing each chunk to storage, then check the
 * saved file against its known SHA-256. */
void wm_ui_https_download_demo(void);

#ifdef __cplusplus
}
#endif

#endif /* __WM_UI_HTTPS__H__ */
