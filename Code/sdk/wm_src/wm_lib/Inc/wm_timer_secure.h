/**
******************************************************************************
* @file    wm_timer_secure.h
* @author  Walnut Medical
* @brief   Header file of timer management functions.
******************************************************************************
* @attention
*
* Copyright (c) 2022 Walnut Medical.
* All rights reserved.
*
******************************************************************************
*/

#ifndef __WM_TIMER_SECURE_H__
#define __WM_TIMER_SECURE_H__

#define MAX_TIMER_COUNT 51   /*INDEX IN ENUM IS 1 TO 50 */

typedef enum{
	TIMER_1 =1,
	TIMER_2 ,
	TIMER_3 ,
	TIMER_4 ,
	TIMER_5 ,
	TIMER_6 ,
	TIMER_7 ,
	TIMER_8 ,
	TIMER_9 ,
	TIMER_10,
	TIMER_11 ,
	TIMER_12,
	TIMER_13 ,
	TIMER_14 ,
	TIMER_15 ,
	TIMER_16 ,
	TIMER_17 ,
	TIMER_18 ,
	TIMER_19 ,
	TIMER_20,
	TIMER_21 ,
	TIMER_22 ,
	TIMER_23 ,
	TIMER_24 ,
	TIMER_25 ,
	TIMER_26 ,
	TIMER_27 ,
	TIMER_28 ,
	TIMER_29 ,
	TIMER_30,
	TIMER_31 ,
	TIMER_32,
	TIMER_33 ,
	TIMER_34 ,
	TIMER_35 ,
	TIMER_36 ,
	TIMER_37 ,
	TIMER_38 ,
	TIMER_39 ,
	TIMER_40,
	TIMER_41 ,
	TIMER_42 ,
	TIMER_43 ,
	TIMER_44 ,
	TIMER_45 ,
	TIMER_46 ,
	TIMER_47 ,
	TIMER_48 ,
	TIMER_49 ,
	TIMER_50,
}TIMER;

TIMER timer;

typedef enum{
	TIMER_NOT_CREATED =0,
	TIMER_RUNNING ,
	TIMER_STANDBY,
	TIMER_START_ERR,
	TIMER_STOP_ERR,
	TIMER_CREATE_ERR,
	TIMER_RESOURCE_OVERLOAD
}WM_timer_resp_code;

typedef struct{
	sTimerRef timerRef;
    int t_value;
	int cyclic;
	int g_timer_status;
	void(*callBackRoutine);
}timer_param;

extern timer_param t_param[];  /*declared in test*/

extern volatile UINT8 validate_board;

sTimerRef g_timerRef[51];
//int g_timer_status=0;

time_t tm;

WM_timer_resp_code wm_TimerCreate(TIMER timer);
WM_timer_resp_code wm_TimerStart(TIMER timer,UINT32 tcountms,UINT8 tcountcyc);
WM_timer_resp_code wm_TimerStop(TIMER timer);
WM_timer_resp_code wm_TimerStatus(TIMER timer);
WM_timer_resp_code wm_StopAllTimers(void);

extern void PrintfResp(char* format);
extern void timerCallback(UINT8 index);

#endif