/**
  ******************************************************************************
  * @file    wm_ui_app.h
  * @author  Walnut Medical
  * @brief   Common Gateway (WEGW) reference UI app - public entry points.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */
#ifndef __WM_UI_APP__H__
#define __WM_UI_APP__H__

/*******************************************************************************
** Header Files
******************************************************************************/
#include "wm_global.h"
#include "wm_extern_fnc.h"
#include "sdk_api.h"      /* Common Gateway SDK platform-abstraction API (incl. sdk_wm.h) */

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** Function Declarations
******************************************************************************/
void WM_Entry_Task_Top_Most(void);        /* customer application entry (demo)    */
void sTask_WM_UIProcesser(void *arg);     /* USB command dispatcher task          */
void PrintfOptionMenu(char *options_list[], int array_size);

#ifdef __cplusplus
}
#endif

#endif /* __WM_UI_APP__H__ */
