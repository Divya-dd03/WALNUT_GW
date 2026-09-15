/**
 * @file sms_manager.h
 * @brief SMS manager API for weware platform - walnut port of the reference
 *        firmware's module/sms/sms_manager.h.
 *
 * Walnut adaptations vs reference (business logic unchanged):
 * - The reference includes "functionality/sdk_functionality_sms.h" (the CG
 *   platform dispatcher). Walnut has no functionality layer for SMS: the
 *   kernel SDK exports the wm_sdk_sms_* calls directly from "wm_sdk_sms.h".
 * - SDK_SMS_MAX_ADDRESS_LENGTH does not exist in the walnut SDK - it is
 *   defined below with the same value the CG compat header used (21).
 * - task_stack[] is dropped: walnut's wm_sdk_task_create() allocates the stack
 *   when stack_ptr is NULL (the convention every ported walnut module uses),
 *   so only the size is kept. TaskStats is a walnut addition (task_stats.c
 *   stack-usage sampling, same as GPS/TCP/SIM).
 */

#ifndef WEWARE_SMS_MANAGER_H
#define WEWARE_SMS_MANAGER_H

#include "common/types.h"
#include "common/task_stats.h"
#include "module/module_manager.h"
#include "module/sms/sms_config.h"
#include "sdk_platform.h"
#include "wm_sdk_sms.h"      /* reference: functionality/sdk_functionality_sms.h */

#ifdef __cplusplus
extern "C" {
#endif

/*===============================================================
 * Configuration Constants
 *==============================================================*/

/**
 * @brief Maximum SMS message content length (excluding NULL terminator)
 * @note Matches the walnut SDK's WM_SDK_SMS_MAX_BODY_LEN (single GSM-7 segment).
 */
#define SMS_MANAGER_MAX_MESSAGE_LENGTH    160

/**
 * @brief Maximum SMS sender/recipient address length
 * @note Reference took this from SDK_SMS_MAX_ADDRESS_LENGTH in
 *       sdk_functionality_sms.h. MISSING IN WALNUT SDK - defined here with
 *       the same value (21 = E.164 max 20 digits + '+').
 */
#ifndef SDK_SMS_MAX_ADDRESS_LENGTH
#define SDK_SMS_MAX_ADDRESS_LENGTH        21
#endif
#define SMS_MANAGER_MAX_ADDRESS_LENGTH    SDK_SMS_MAX_ADDRESS_LENGTH

/**
 * @brief Internal SMS SDK queue depth
 */
#define SMS_MANAGER_QUEUE_SIZE            16

/**
 * @brief SMS task stack size (bytes)
 * @note Reference used a static 2048-byte task_stack[] member; on walnut the
 *       kernel allocates the stack (wm_sdk_task_create stack_ptr = NULL) and the
 *       ported modules all use 4096.
 */
#define SMS_MANAGER_TASK_STACK_SIZE       4096U

/*===============================================================
 * Type Definitions
 *==============================================================*/

/**
 * @brief SMS message structure (received messages)
 */
typedef struct
{
    char   sender_number[SMS_MANAGER_MAX_ADDRESS_LENGTH + 1];
    char   message_content[SMS_MANAGER_MAX_MESSAGE_LENGTH + 1];
    int    message_index;
    UINT32 timestamp;
    BOOL   is_read;
} sms_message_t;

/**
 * @brief SMS send request structure (internal queue payload)
 */
typedef struct
{
    char recipient[SMS_MANAGER_MAX_ADDRESS_LENGTH + 1];
    char message[SMS_MANAGER_MAX_MESSAGE_LENGTH + 1];
} sms_send_request_t;

/**
 * @brief SMS manager runtime statistics
 */
typedef struct
{
    UINT32 total_received;
    UINT32 total_sent;
    UINT32 total_deleted;
    UINT32 send_errors;
    UINT32 receive_errors;
    UINT32 delete_errors;
} sms_manager_stats_t;

/*===============================================================
 * Runtime State Structure
 *==============================================================*/
/**
 * @brief SMS manager runtime state structure
 * @note Contains all runtime state for SMS manager (configuration is separate in SmsConfig)
 */
typedef struct
{
    /* Task Configuration */
    sdk_task_ref_t      task_ref;                   /**< SMS send task reference */
    TaskStats           task_stats;                 /**< Walnut addition: stack usage sampling */
    UINT32              task_priority;              /**< Task priority level */
    UINT32              task_interval_ms;           /**< Task sleep interval (ms) */

    /* Runtime State */
    Module             *module;                     /**< Pointer to SMS module from module_manager */
    SmsConfig          *config;                      /**< Pointer to SMS configuration */
    sms_manager_stats_t stats;                      /**< SMS manager statistics */
} sms_manager_runtime_t;

/*===============================================================
 * Public API
 *==============================================================*/

/**
 * @brief Initialize SMS manager
 *
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result sms_manager_init(void);

/**
 * @brief Deinitialize SMS manager
 *
 * @return RESULT_SUCCESS on success
 */
Result sms_manager_deinit(void);

/**
 * @brief Send SMS message asynchronously
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result sms_manager_send(const char *recipient, const char *message);

/**
 * @brief Delete SMS message by index
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result sms_manager_delete(int message_index);

/**
 * @brief Set SMS format mode (0 = PDU, 1 = TEXT)
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result sms_manager_set_format_mode(int format_mode);

/**
 * @brief Get SMS manager runtime statistics
 *
 * @return Pointer to internal statistics structure (read-only)
 */
const sms_manager_stats_t *sms_manager_get_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_SMS_MANAGER_H */
