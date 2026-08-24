/**
  ******************************************************************************
  * @file    wm_sms_secure.h
  * @author  Walnut Medical
  * @brief   Header file of SMS Functions.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 Walnut Medical.
  * All rights reserved.
  *
  ******************************************************************************
  */

#ifndef __WM_SMS_SECURE_H__
#define __WM_SMS_SECURE_H__

/*******************************************************************************
** Header Files
******************************************************************************/
#include "wm_global.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** Type Definitions
******************************************************************************/
/* SMS message format (AT+CMGF). */
typedef enum
{
    SMS_FORMAT_PDU  = 0,
    SMS_FORMAT_TEXT = 1,
} wm_sms_format_e;

/*
 * Dispatch hook for an incoming SMS. Invoked from wm_sms_task (safe context)
 * after the message at 'index' in 'storage' has been read into 'raw' (the raw
 * +CMGR response text). Register with wm_sms_set_incoming_cb(); if none is set
 * the raw message is echoed via PrintfResp().
 */
typedef void (*wm_sms_incoming_cb)(const char *storage, int index, const char *raw);

/*******************************************************************************
** Functions
******************************************************************************/
/* Configuration (all return 0 on success, -1 on failure). */
int  wm_sms_set_format(int format);
int  wm_sms_get_format(void);                                                    /* returns mode, -1 on failure */
int  wm_sms_set_charset(const char *charset);
int  wm_sms_set_preferred_storage(const char *mem1, const char *mem2, const char *mem3);
int  wm_sms_get_preferred_storage(char *resp, int resp_len);
int  wm_sms_set_new_message_indication(int mode, int mt, int bm, int ds, int bfr);
int  wm_sms_get_new_message_indication(char *resp, int resp_len);

/* Messaging (all return 0 on success, -1 on failure). */
int  wm_sms_send_text(const char *number, const char *text, char *resp, int resp_len);
int  wm_sms_read_message(int index, char *resp, int resp_len);
int  wm_sms_list_messages(const char *stat, char *resp, int resp_len);
int  wm_sms_delete_message(int index, int delflag);

/* Incoming pipeline. */
void sendMsgToSms(const char *storage, int index);          /* enqueue a +CMTI event (non-blocking) */
void wm_sms_set_incoming_cb(wm_sms_incoming_cb cb);         /* optional dispatch hook */

/*
 * Create the SMS queue + task, register the +CMTI callback and apply the
 * default configuration (text mode, GSM charset, +CMTI indication). Idempotent.
 */
void wm_sms_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __WM_SMS_SECURE_H__ */
