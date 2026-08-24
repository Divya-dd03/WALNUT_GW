/**
  ******************************************************************************
  * @file    wm_app_main.c
  * @author  Walnut Medical
  * @brief   Application image entry point. appimg_enter() is invoked by the
  *          platform after boot to bring up the system and start the customer
  *          application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "wm_global.h"
#include "sdk_wm.h"

int appimg_enter(void *param)
{
    sAPI_Debug("application image enter, param 0x%x", param);

    wm_board_ID = WM_CURRENT_BOARD;
	wm_battery_ID = WM_CURRENT_BATTERY;
	wm_sleep_mode = WM_CURRENT_SLEEP_MODE;
    RTI_LOG("WM Application Boot Start");
    if (wm_system_init() != SDK_RESULT_SUCCESS)
        RTI_LOG("wm_system_init reported an error");
    WM_Entry_Task_Top_Most();  /* customer application entry */

    return 0;
}

void appimg_exit(void)
{
    sAPI_Debug("application image exit");
}