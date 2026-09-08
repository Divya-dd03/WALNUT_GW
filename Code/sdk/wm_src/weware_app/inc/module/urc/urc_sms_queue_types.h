/**
 * @file urc_sms_queue_types.h
 * @brief SMS URC element for module urc_q (no heap; inbound text embedded)
 *
 * Walnut port of the reference firmware's module/urc/urc_sms_queue_types.h.
 *
 * Reference: SMS modem URCs arrive as sdk_msg_t with a transient +CMTI line in
 * arg3; the URC processor copies the string into arg3_inline, queue_push copies
 * the whole struct into the ring buffer, and the consumer sets hdr.arg3 =
 * arg3_inline after pop.
 *
 * Walnut delta: the kernel emits no SMS URC through urc_processor (see the
 * dispatch note in urc_processor.c). Inbound SMS is delivered by the SDK to the
 * SMS message queue as an SdkSmsMessage of type SDK_SMS_EVT_INCOMING, whose
 * heap-owned 'text' is the raw +CMGR response the kernel already read for that
 * index. sms_manager.c drains that queue and parks such events here (the same
 * queue the reference's URC processor filled), so the identical "pop URC ->
 * sms_process_urc()" flow runs in the SMS task. The consumer sets hdr.text =
 * arg3_inline after pop, exactly as the reference sets hdr.arg3.
 */

#ifndef WEWARE_URC_SMS_QUEUE_TYPES_H
#define WEWARE_URC_SMS_QUEUE_TYPES_H

#include "sdk_platform.h"
#include "sdk_types.h"   /* SdkSmsMessage (reference: sdk_msg_t from sdk_platform.h) */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Reference sized this at 72 bytes - enough for a "+CMTI: \"SM\",12" line.
 * Walnut carries the whole raw +CMGR response instead (status + address +
 * timestamp header ~64 B plus a 160-character body), so it needs 256.
 */
#define SMS_URC_TEXT_MAX 256U

typedef struct {
    SdkSmsMessage hdr;                      /* reference: sdk_msg_t hdr */
    char          arg3_inline[SMS_URC_TEXT_MAX];
} sms_urc_queued_t;

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_URC_SMS_QUEUE_TYPES_H */
