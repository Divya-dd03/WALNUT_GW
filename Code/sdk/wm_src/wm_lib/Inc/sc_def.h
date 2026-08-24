/**
  ******************************************************************************
  * @file    sc_def.h
  * @brief   Custom definitions for Walnut Medical API calls.
  * @author  Walnut Medical
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */

#ifndef __SC_DEF_H__
#define __SC_DEF_H__

/* Include */
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>  
#include "typedef.h"
#include "sc_enum.h"

/* Define */
#define SC_FIFO 11
#define SC_SUSPEND 0xFFFFFFFF
#define FALSE false
#define TRUE  true
#define SC_LONG_LONG_MAX 9223372036854775807

/* Typedef */
typedef void* sTaskRef;
typedef void* sSemaRef;
typedef void* sMutexRef;
typedef void* sMsgQRef;
typedef void* sTimerRef;
typedef void* sFlagRef;
typedef int WM_timer_resp_code;
typedef int TIMER_t;
#ifndef CHAR
typedef char CHAR;
#endif
#ifndef BOOL
typedef bool BOOL;
#endif

/* OS */
typedef enum
{
    SC_SUCCESS = 0,
    SC_FAIL,
} SC_STATUS;

typedef struct sim_msg_cell
{
    UINT32 msg_id;
    int arg1;
    int arg2;
    void* arg3;
} SIM_MSG_T;

typedef struct
{
    int tv_sec;        /* seconds */
    int tv_usec;       /* and microseconds */
} sTimeval;

typedef struct
{
    char* task_name;                /* Pointer to thread's name     */
    unsigned int        task_priority;             /* Priority of thread (0-255)  */
    unsigned long        task_stack_def_val;             /* default vaule of  thread  */
    SC_TASK_STATE      task_state;                /* Thread's execution state     */
    unsigned long       task_stack_ptr;           /* Thread's stack pointer   */
    unsigned long       task_stack_start;         /* Stack starting address   */
    unsigned long       task_stack_end;           /* Stack ending address     */
    unsigned long       task_stack_size;           /* Stack size               */
    unsigned long       task_run_count;            /* Thread's run counter     */

} sTaskInfo;

/* RTC */
typedef struct {
    int tm_ms;    //milliseconds[0,999]
    int tm_sec;	//seconds [0,59]
    int tm_min;	//minutes [0,59]
    int tm_hour;  //hour [0,23]
    int tm_mday;  //day of month [1,31]
    int tm_mon;   //month of year [1,12]
    int tm_year; 	// since 1970
    int tm_wday; 	// day of week [0,6] (sunday = 0)
}t_rtc_new_ms;

/* MQTT */
typedef struct {
    int client_index;
    unsigned int topic_len;
    char* topic_P;
    unsigned int payload_len;
    char* payload_P;
}SCmqttData;

/* Network */
typedef struct {
    UINT8 cid;
    char iptype[8];
    char ipv4addr[16];
    char ipv6addr[64];
}SCcgpaddrParm;

typedef struct {
    UINT8 cid;
    char iptype[8];
    char apn[40];
}SCApnParmGet;

typedef struct {
    UINT8 cid_new;
    char iptype_new[8];
    char apn_new[40];
}SCApnParmSet;

typedef struct {
    char networkmode[40];
    char Mnc_Mcc[20];
    char GSMBandStr[20];
    char LTEBandStr[20];
    int LAC;
    int CellID;
    int TAC;
    int Rsrp;
    int Rsrq;
    int RXLEV;
    int TA;
    int SINR;
    int Rssi;
    int dlEuArfcn;
    int subframeAssignment;
    int systemFrameNumber;
    int pCellID;
}SCcpsiParm;

typedef enum
{
    WM_SSL_3_0 = 0,
    WM_TLS_1_0,
    WM_TLS_1_1,
    WM_TLS_1_2,
    WM_SSL_TLS_ALL,
    WM_SSL_ERROR
} WM_SSL_SELECTION;


#endif