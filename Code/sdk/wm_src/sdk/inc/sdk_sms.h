/**
 ******************************************************************************
 * @file    sdk_sms.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - SMS API.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __SDK_SMS_H__
#define __SDK_SMS_H__

#include "sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * The queue-based operations below report their outcome by posting one
 * SdkSmsMessage (see sdk_types.h) to the supplied queue; incoming messages are
 * reported the same way as SDK_SMS_EVT_INCOMING events. A queue used with these
 * functions must be created for a message size of sizeof(SdkSmsMessage). A NULL
 * queue performs the operation without reporting a result.
 *
 * Each received message's 'text' is heap-owned by the caller: after handling a
 * message drained from the queue, release its text with sdk_sms_msg_free().
 */

/**
 * @brief  Bring up the SMS subsystem (vendor task/queue + default configuration:
 *         text mode, GSM charset, +CMTI new-message indication) and register the
 *         incoming-SMS hook. Call once from SDK init. Does NOT create a receive
 *         queue - the application supplies its own via the queue-taking calls
 *         (sdk_sms_read / _delete / _delete_all / _msgq_poll), which register it
 *         as the destination for asynchronous incoming messages.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_sms_init(void);

/**
 * @brief  Release the heap-owned 'text' of a message received from the SMS
 *         queue. Safe to call on any received message (no-op when text is NULL);
 *         clears the pointer so it cannot be freed twice.
 * @param  msg  message previously received via sdk_msgq_recv().
 */
void sdk_sms_msg_free(SdkSmsMessage *msg);

/**
 * @brief  Set SMS message format (AT+CMGF).
 * @param  format  0=PDU mode, 1=text mode.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_sms_set_format(UINT8 format);

/**
 * @brief  Set SMS character set (AT+CSCS).
 * @param  charset  character-set selector (0=GSM, 1=UCS2, 2=IRA).
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_sms_set_charset(UINT8 charset);

/**
 * @brief  Configure new-message URC indication (AT+CNMI).
 * @param  mode,mt,bm,ds,bfr  the five CNMI parameters.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_sms_set_new_msg_ind(UINT8 mode, UINT8 mt, UINT8 bm, UINT8 ds, UINT8 bfr);

/**
 * @brief  Send a text-mode SMS. Blocks until the network accepts the message or
 *         the attempt fails / times out.
 * @param  number  destination phone number, NUL-terminated (e.g. "7814304806").
 * @param  text    message body, NUL-terminated; up to SDK_SMS_MAX_BODY_LEN
 *                 characters.
 * @return SdkResult - 0 success; SDK_RESULT_INVALID_PARAM on a NULL or oversized
 *                     argument; negative on failure.
 */
SdkResult sdk_sms_send(const char *number, const char *text);

/**
 * @brief  Query the occupancy of the active SMS storage (AT+CPMS?), i.e. the
 *         area messages are currently read from and stored to. Read-only: it
 *         does not change the configured preferred storage.
 * @param  name       [out] buffer for the store mnemonic (e.g. "SM"); may be
 *                     NULL if the name is not needed.
 * @param  name_size  size of @p name in bytes.
 * @param  used       [out] number of messages currently stored.
 * @param  total      [out] total capacity (message slots).
 * @return SdkResult - 0 success; SDK_RESULT_INVALID_PARAM on a NULL @p used /
 *                     @p total; SDK_RESULT_ERROR if the module is not ready.
 */
SdkResult sdk_sms_get_storage_status(char *name, UINT32 name_size, UINT32 *used, UINT32 *total);

/**
 * @brief  Read a stored SMS by index; the outcome is delivered to @p msgq as an
 *         SdkSmsMessage (type SDK_SMS_EVT_READ_RESULT, with the message text on
 *         success).
 * @param  storage  storage area selector (0=SM, 1=ME, 2=MT).
 * @param  index    message index to read.
 * @param  msgq     queue to receive the read result (may be NULL).
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_sms_read(UINT8 storage, UINT32 index, void *msgq);

/**
 * @brief  Delete a stored SMS by index; the outcome is delivered to @p msgq as
 *         an SdkSmsMessage (type SDK_SMS_EVT_DELETE_RESULT).
 * @param  index  message index to delete.
 * @param  msgq   queue to receive the result (may be NULL).
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_sms_delete(UINT32 index, void *msgq);

/**
 * @brief  Delete all stored SMS; the outcome is delivered to @p msgq as an
 *         SdkSmsMessage (type SDK_SMS_EVT_DELETE_RESULT, index -1).
 * @param  msgq  queue to receive the result (may be NULL).
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_sms_delete_all(void *msgq);

/**
 * @brief  Poll the SMS message queue for the number of pending events (read
 *         results and asynchronous incoming messages).
 * @param  msgq       the SMS message queue.
 * @param  msg_count  [out] number of SdkSmsMessage events pending.
 * @return SdkResult - 0 success; SDK_RESULT_INVALID_PARAM on NULL argument.
 */
SdkResult sdk_sms_msgq_poll(void *msgq, UINT32 *msg_count);

#ifdef __cplusplus
}
#endif

#endif /* __SDK_SMS_H__ */
