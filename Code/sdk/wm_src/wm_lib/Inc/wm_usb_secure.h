/**
******************************************************************************
* @file    wm_usb_secure.h
* @author  Walnut Medical
* @brief   Header file of USB management functions.
******************************************************************************
* @attention
*
* Copyright (c) 2022 Walnut Medical.
* All rights reserved.
*
******************************************************************************
*/

#ifndef __WM_USB_SECURE_H__
#define __WM_USB_SECURE_H__


#include "wm_gpio.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** Defines
******************************************************************************/
#define USB_VCOM_RX_BUFFER_SIZE (5 * 1024)
#define USB_VCOM_TX_BUFFER_SIZE (5 * 1024)

/*******************************************************************************
** External Functions
******************************************************************************/


/*******************************************************************************
** Type Definitions
******************************************************************************/


/*******************************************************************************
** Functions
******************************************************************************/
/* USB Communication Functions */
int wm_USB_VOCM_init(void);
void sendMsgToUIDemo(SIM_MSG_T UartMsg);
void PrintfResp(char* format);
SIM_MSG_T GetParamFromUart(void);

#ifdef __cplusplus
}
#endif

#endif /*__WM_POWER_SECURE_H__*/