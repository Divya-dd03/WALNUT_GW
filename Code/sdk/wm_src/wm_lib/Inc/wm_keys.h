/**
******************************************************************************
* @file    wm_keys.h
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

#ifndef __WM_KEYS_H__
#define __WM_KEYS_H__

/*******************************************************************************
** Header Files
******************************************************************************/
#include "wm_gpio.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** Defines
******************************************************************************/
#define KEY_TASK_STACK (1024*2)
#define KEY_IDLE 99
#define DEBOUNCE_TIME       (20/5)          // Debounce time
#define DOUBLE_PRESS_TIME   (300/5)         // Max time between presses for double press
#define LONG_PRESS_TIME     (2000/5)        // Minimum time for long press
#define FLAG_INIT           (0)   
#define FLAG_KEY_PRESS      (1 << 0)    // Key press detected
#define FLAG_KEY_RELEASE    (1 << 1)    // Key release detected
#define FLAG_TIMEOUT        (1 << 2)    // Timeout occurred
#define LOOP_TIMEOUT_TICKS  (7000/5) // 5s safety timeout
#define KEY_TRIGGER         (88) // Key 1 support


/*******************************************************************************
** External Functions
******************************************************************************/
extern void wm_ev_key_1_state(UINT8 key_press_state);
extern void wm_ev_key_2_state(UINT8 key_press_state);
extern void wm_ev_key_3_state(UINT8 key_press_state);
extern void wm_ev_key_4_state(UINT8 key_press_state);
extern void wm_ev_key_5_state(UINT8 key_press_state);

/*******************************************************************************
** Variables
******************************************************************************/


/*******************************************************************************
 ** Functions
 ******************************************************************************/
void wm_key_init(void);

void WM_key_1_IntHandler(void);
void WM_key_2_IntHandler(void);
void WM_key_3_IntHandler(void);
void WM_key_4_IntHandler(void);
void WM_key_5_IntHandler(void);

void key_1_poll(void* arg);
void key_2_poll(void* arg);
void key_3_poll(void* arg);
void key_4_poll(void* arg);
void key_5_poll(void* arg);

#ifdef __cplusplus
}
#endif
#endif/*__WM_KEYS_H__*/