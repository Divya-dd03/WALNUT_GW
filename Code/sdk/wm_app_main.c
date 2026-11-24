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
#include "wm_sdk_wm.h"

int appimg_enter(void *param)
{
    sAPI_Debug("application image enter, param 0x%x", param);

    wm_board_ID = WM_CURRENT_BOARD;
	wm_battery_ID = WM_CURRENT_BATTERY;
	wm_sleep_mode = WM_CURRENT_SLEEP_MODE;
	wm_mlcd_support = WM_CURRENT_MLCD;
	wm_glcd_support = WM_CURRENT_GLCD;
	wm_audio_codec = WM_CURRENT_AUDIO_CODEC;
	wm_wifi_support = WM_CURRENT_WIFI;
	wm_ble_support = WM_CURRENT_BLE;
	wm_gnss_support = WM_CURRENT_GNSS;
	wm_nfc_support = WM_CURRENT_NFC;
	wm_dual_sim_support = WM_CURRENT_DUAL_SIM;
	wm_enc_led_support = WM_CURRENT_ENC_LED;
	wm_ext_flash_support = WM_CURRENT_EXT_FLASH;
	wm_keypad_type = WM_CURRENT_KEYPAD;
	wm_accel_support = WM_CURRENT_ACCEL;
    RTI_LOG("WM Application Boot Start");
    if (wm_system_init() != WM_SDK_RESULT_SUCCESS)
        RTI_LOG("wm_system_init reported an error");
    WM_Entry_Task_Top_Most();  /* customer application entry */

    return 0;
}

void appimg_exit(void)
{
    sAPI_Debug("application image exit");
}