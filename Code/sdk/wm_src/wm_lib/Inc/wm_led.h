/**
******************************************************************************
* @file    wm_LED.h
* @author  Walnut Medical
* @brief   External fucntion calls of keys.
******************************************************************************
* @attention
*
* Copyright (c) 2022 Walnut Medical.
* All rights reserved.
*
******************************************************************************
*/

#ifndef __WM_LED_H__
#define __WM_LED_H__

/*******************************************************************************
** Header Files
******************************************************************************/
#include "wm_gpio.h"

/*******************************************************************************
** Defines
******************************************************************************/
#define LOW_BLINK_FREQ 500  
#define HIGH_BLINK_FREQ 250  

/*******************************************************************************
** Functions
******************************************************************************/
void wm_set_led_state(char led_red_intensity, char led_green_intensity, char led_blue_intensity, char blink_freq, char blink_count);
void wm_aux_led_on(void);
void wm_aux_led_off(void);
void wm_change_led_state(char led_red_intensity, char led_green_intensity, char led_blue_intensity, char blink_on);
void wm_set_led_single(WM_LEDS_INDICATOR led, BOOL blink_on);

#endif/*__WM_LED_H__*/
