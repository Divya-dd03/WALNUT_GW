/**
  ******************************************************************************
  * @file    wm_ui_tcp.h
  * @author  Walnut Medical
  * @brief   Common Gateway (WEGW) reference application - TCP demo.
  *
  *          Declares the menu handler defined in wm_ui_tcp.c, run from the
  *          "UIPROC" dispatcher in wm_ui_app.c.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */
#ifndef __WM_UI_TCP__H__
#define __WM_UI_TCP__H__

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
/* Open an event-driven socket to an echo host, send a short payload, and print
 * the reply. Returns to the menu immediately: the exchange is reported through
 * the socket callback. */
void wm_ui_tcp_demo(void);

#ifdef __cplusplus
}
#endif

#endif /* __WM_UI_TCP__H__ */
