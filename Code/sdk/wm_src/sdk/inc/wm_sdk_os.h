/**
 ******************************************************************************
 * @file    wm_sdk_os.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - OS / RTOS API (tasks, mutex, message queues,
 *          heap memory, ticks and system statistics).
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __WM_SDK_OS_H__
#define __WM_SDK_OS_H__

#include "wm_sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** Tasks
******************************************************************************/
/**
 * @brief  Create and start an RTOS task.
 * @param  task_fn     task entry function.
 * @param  arg         argument passed to the task.
 * @param  name        task name (for debug).
 * @param  stack_ptr   caller-provided stack buffer (NULL => allocate).
 * @param  stack_size  stack size in bytes.
 * @param  priority    task priority.
 * @return task handle on success; NULL on failure.
 */
void *wm_sdk_task_create(void (*task_fn)(void *), void *arg, const char *name,
                      void *stack_ptr, UINT32 stack_size, UINT32 priority);

/**
 * @brief  Delete a task.
 * @param  task_handle  handle from wm_sdk_task_create().
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_task_delete(void *task_handle);

/**
 * @brief  Sleep/yield the current task for the given duration.
 * @param  ms  sleep duration in milliseconds.
 */
void wm_sdk_task_sleep(UINT32 ms);

/**
 * @brief  Get stack usage statistics for a task.
 * @param  task_handle  task handle.
 * @param  stack_size   [out] total stack size in bytes.
 * @param  stack_used   [out] bytes currently in use.
 * @param  stack_peak   [out] peak (high-water) bytes used.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_task_get_stack_info(void *task_handle, UINT32 *stack_size,
                                  UINT32 *stack_used, UINT32 *stack_peak);

/**
 * @brief  Get the millisecond system tick counter (wraps).
 */
UINT32 wm_sdk_get_ticks(void);

/*******************************************************************************
** Mutex
******************************************************************************/
wm_SdkResult wm_sdk_mutex_create(void **mutex, UINT32 wait_mode);
wm_SdkResult wm_sdk_mutex_lock(void *mutex, UINT32 timeout);
wm_SdkResult wm_sdk_mutex_unlock(void *mutex);
wm_SdkResult wm_sdk_mutex_delete(void *mutex);

/*******************************************************************************
** Message queues
******************************************************************************/
/**
 * @brief  Create a message queue.
 * @return queue handle on success; NULL on failure.
 */
void *wm_sdk_msgq_create(const char *name, UINT32 msg_size, UINT32 capacity, UINT32 flags);
wm_SdkResult wm_sdk_msgq_send(void *msgq, const void *msg, UINT32 timeout_ms);
wm_SdkResult wm_sdk_msgq_recv(void *msgq, void *msg, UINT32 timeout_ms);
wm_SdkResult wm_sdk_msgq_delete(void *msgq);

/*******************************************************************************
** Heap memory
******************************************************************************/
void *wm_sdk_memory_alloc(UINT32 size);
void  wm_sdk_memory_free(void *ptr);

/*******************************************************************************
** System statistics
******************************************************************************/
/**
 * @brief  Get RAM/flash/CPU usage statistics.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_system_get_stats(UINT32 *ram_total_kb, UINT32 *ram_free_kb,
                               INT64 *flash_total_kb, INT64 *flash_free_kb,
                               UINT8 *cpu_usage_percent);

#ifdef __cplusplus
}
#endif

#endif /* __WM_SDK_OS_H__ */
