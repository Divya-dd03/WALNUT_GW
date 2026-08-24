/**
  ******************************************************************************
  * @file    sc_os.h
  * @brief   SC net enum and defs of OS.
  * @author  Walnut Medical
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */
#ifndef __SC_OS_H__
#define __SC_OS_H__

#include "wm_config.h"
#include "sc_enum.h"
#include "sc_def.h"
#include "zx_api.h"

/* Define */
#define ONKEY_STATUS POWER_KEY_STATUS	 /*redifination as per new lib*/

#define sAPI_Malloc(size) osiMalloc(size) // malloc(size)
#define sAPI_Debug CAT_LOG
#define sAPI_TaskSleep(ticks) osiThreadSleep(ticks)
#define sAPI_Free(pArg) osiFree(pArg)
#define sAPI_GetTicks() osiGetTicks()

#define sAPI_MsgQSend(msgQRef, msgPtr) \
    (osiMessageQueuePut((osiMessageQueue_t*)(msgQRef), (void*)(msgPtr)) ? SC_SUCCESS : SC_FAIL)

#define sAPI_MsgQSendSuspend(msgQRef, msgPtr, timeout) \
    (osiMessageQueueTryPut((osiMessageQueue_t*)(msgQRef), (void*)(msgPtr),(uint32_t)timeout) ? SC_SUCCESS : SC_FAIL)

#define sAPI_MsgQRecv(msgQRef, recvMsg, timeout) \
    (osiMessageQueueTryGet((osiMessageQueue_t*)(msgQRef), (void*)(recvMsg), (timeout)) ? SC_SUCCESS : SC_FAIL)

#define sAPI_MsgQPoll(msgQRef, pCount)                            \
    ({                                                            \
        UINT32 _count = osiMessageQueuePendingCount((osiMessageQueue_t*)(msgQRef)); \
        if ((pCount) != NULL)                                     \
            *(pCount) = _count;                                   \
        (_count > 0 ? SC_SUCCESS : SC_FAIL);                      \
    })
#define sAPI_MsgQFlush(msgQRef)                                            \
    do {                                                                   \
        if ((msgQRef) != NULL) {                                           \
            void *tmp = NULL;                                              \
            int flush_limit = 100; /* Prevent infinite loops */           \
            while (flush_limit-- > 0 &&                                    \
                   osiMessageQueuePendingCount((osiMessageQueue_t*)(msgQRef)) > 0) { \
                if (!osiMessageQueueTryGet((osiMessageQueue_t*)(msgQRef), &tmp, 0)) \
                    break;                                                 \
            }                                                              \
        }                                                                  \
    } while (0)

#define sAPI_MsgQDelete(msgQRef)               \
    do {                                       \
        if ((msgQRef) != NULL)                 \
        {                                      \
            osiMessageQueueDelete((osiMessageQueue_t*)(msgQRef)); \
            (msgQRef) = NULL;                  \
        }                                      \
    } while (0)

/* Flag Definitions and Wrappers */
#define sAPI_FlagCreate(flagRef)                     \
    ({                                               \
        osiFlag_t* _flag = osiFlagCreate();          \
        *(flagRef) = (sFlagRef)_flag;                \
        (_flag != NULL ? SC_SUCCESS : SC_FAIL);      \
    })

#define sAPI_FlagDelete(flagRef) \
    (osiFlagDelete((osiFlag_t*)flagRef) == 0 ? SC_SUCCESS : SC_FAIL)

#define sAPI_FlagSet(flagRef, mask, operation) \
    (osiFlagSet((osiFlag_t*)flagRef, (mask), (operation)) == 0 ? SC_SUCCESS : SC_FAIL)

#define sAPI_FlagWait(flagRef, mask, operation, flags, timeout) \
    (osiFlagWait((osiFlag_t*)flagRef, (mask), (operation), (flags), (timeout)) == 0 ? SC_SUCCESS : SC_FAIL)

/* Enum */

/* Structure */

/* System Functions */
SC_STATUS sAPI_TaskCreate(sTaskRef* taskRef, void* stackPtr, UINT32 stackSize, UINT8 priority, char* taskName, void (*taskStart)(void*), void* argv);
SC_STATUS sAPI_MsgQCreate(sMsgQRef* msgQRef, char* queueName, UINT32 maxSize, UINT32 maxNumber, UINT8 waitingMode);
#define sAPI_TaskCreateWithoutStackRef(taskRef, stackSize, priority, taskName, taskEntry, argv)         \
    ({                                                                                                  \
        osiThread_t *_task = osiThreadCreate((taskName), (taskEntry), (argv), (priority), (stackSize)); \
        *(taskRef) = (sTaskRef)_task;                                                                   \
        (_task != NULL ? SC_SUCCESS : SC_FAIL);                                                         \
    })

/* Monitoring Functions */
void wm_RAM_info(void);
void wm_CPU_info(void);

/* Print Functions */
void wm_printf(char* format, ...);

/* Mutex Functions */
SC_STATUS sAPI_MutexCreate(sMutexRef* mutexRef, uint8_t waitingMode);
SC_STATUS sAPI_MutexDelete(sMutexRef mutexRef);

#define sAPI_MutexLock(mutexRef, timeout) \
    ((mutexRef) ? (osiMutexTryLock((osiMutex_t*)(mutexRef), (timeout)) ? SC_SUCCESS : SC_FAIL) : SC_FAIL)

#define sAPI_MutexUnLock(mutexRef) \
    ((mutexRef) ? (osiMutexUnlock((osiMutex_t*)(mutexRef)), SC_SUCCESS) : SC_FAIL)

/* Model ID Functions */
void wm_Get_modelid(char t_sdk_version[100]);

/*  Kernel Logs Functions */
void wm_post_boot_kernel_logs_en(void);

void sAPI_DelayUs(uint32_t us); // Only use for delay below 5 ms. Step size is 30 us.

/* Change verbosity on the fly */
void wm_set_log_level(int level);

/* Convert numeric level to a short tag                                  */
static inline const char* wm_log_level_str(int lvl)
{
    switch (lvl) {
    case WM_LOG_LEVEL_DEBUG:    return "DEBUG";
    case WM_LOG_LEVEL_INFO:     return "INFO ";
    case WM_LOG_LEVEL_WARNING:  return "WARN ";
    case WM_LOG_LEVEL_ERROR:    return "ERROR";
    case WM_LOG_LEVEL_CRITICAL: return "CRITC";
    default:                    return "UNKWN";
    }
}

/* Core printer: only emits when the message level >= current threshold  */
#define WM_LOG_PRINT(level, fmt, ...)                                        \
    do {                                                                     \
        if ((level) >= g_wm_log_level) {                                     \
            /* prepend severity and forward to your platform’s logger */     \
            sAPI_Debug("[%s] " fmt, wm_log_level_str(level), ##__VA_ARGS__); \
        }                                                                    \
    } while (0)

/* ---------- convenience wrappers --------------------------------- */
#define WM_LOG_DEBUG(fmt, ...)    WM_LOG_PRINT(WM_LOG_LEVEL_DEBUG,    fmt, ##__VA_ARGS__)
#define WM_LOG_INFO(fmt, ...)     WM_LOG_PRINT(WM_LOG_LEVEL_INFO,     fmt, ##__VA_ARGS__)
#define WM_LOG_WARNING(fmt, ...)  WM_LOG_PRINT(WM_LOG_LEVEL_WARNING,  fmt, ##__VA_ARGS__)
#define WM_LOG_ERROR(fmt, ...)    WM_LOG_PRINT(WM_LOG_LEVEL_ERROR,    fmt, ##__VA_ARGS__)
#define WM_LOG_CRITICAL(fmt, ...) WM_LOG_PRINT(WM_LOG_LEVEL_CRITICAL, fmt, ##__VA_ARGS__)

int sAPI_ExtNorFlashReadID(unsigned char* id);

#endif