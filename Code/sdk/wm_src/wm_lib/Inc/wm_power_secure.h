/**
******************************************************************************
* @file    wm_power_secure.h
* @author  Walnut Medical
* @brief   Header file of power management functions.
******************************************************************************
* @attention
*
* Copyright (c) 2022 Walnut Medical.
* All rights reserved.
*
******************************************************************************
*/

#ifndef __WM_POWER_SECURE_H__
#define __WM_POWER_SECURE_H__

/*******************************************************************************
** Header Files
******************************************************************************/
#include "wm_gpio.h"
#include "wm_usb_secure.h"
#include "wm_batt_secure.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** Defines
******************************************************************************/
#define WDT_URC_PROCESS_TASK_STACK_SIZE (1024*4) 
typedef void (*PowerKeyIntCallback)(void);
#define sAPI_SysPowerOff() osiSysPoweroff()
#define sAPI_SysReset() osiSysReset()

/*******************************************************************************
** Variables
******************************************************************************/


/*******************************************************************************
** External Functions
******************************************************************************/


/*******************************************************************************
** Type Definitions
******************************************************************************/


/*******************************************************************************
** Functions
******************************************************************************/
/* WDT Functions */
int wm_wdtimer_enable_new(void);
int wm_wdtimer_disable_new(void);
int wm_wdtimer_feed_new(void);

/* Hardware Config Functions */
void wm_pre_boot_init(void);
void wm_configure_module_hw(void);
void wm_sb_on(void);

/* Logs Functions */
void wm_start_kernel_logs(void);
void wm_stop_kernel_logs(void);
void wm_logger_mode(BOOL t_mode);

/* System Sleep */
void start_system_sleep(void);
void wm_WDT_mngr_init(void);

/* System Mutex */
void wm_system_mutex_init(void);

/* USB POWER Functions */
bool sAPI_UsbVbusDetect(void);

/* Power Key Functions */
void sAPI_PowerKeyRegisterCallback(PowerKeyIntCallback cb);
POWER_KEY_STATUS sAPI_GetPowerKeyStatus(void);

/* System Config Functions */
void wm_print_system_config(void);

#ifdef __cplusplus
}
#endif

#endif /*__WM_POWER_SECURE_H__*/