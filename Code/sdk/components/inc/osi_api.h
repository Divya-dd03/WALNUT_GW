/* All rights reserved.
 *
 */

#ifndef _OSI_API_H_
#define _OSI_API_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "osi_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * get current kervel version info
 */
char *kernelVersion(void);

#define OSI_WAIT_FOREVER (-1U)
#define OSI_DELAY_MAX (-1U)


//ICAT EXPORTED ENUM
typedef enum
{
	OSA_TASK_READY,
	OSA_TASK_COMPLETED,
	OSA_TASK_TERMINATED,
	OSA_TASK_SUSPENDED,
	OSA_TASK_SLEEP,
	OSA_TASK_QUEUE_SUSP,
	OSA_TASK_SEMAPHORE_SUSP,
	OSA_TASK_EVENT_FLAG,
	OSA_TASK_BLOCK_MEMORY,
	OSA_TASK_MUTEX_SUSP,
	OSA_TASK_STATE_UNKNOWN,
}OSA_TASK_STATE;

//ICAT EXPORTED STRUCT
typedef struct OSA_TASK_STRUCT
{
    char                *task_name;                /* Pointer to thread's name     */
    unsigned int        task_priority;             /* Priority of thread (0-255)  */
    unsigned long        task_stack_def_val;             /* default vaule of  thread  */
    OSA_TASK_STATE      task_state;                /* Thread's execution state     */
    unsigned long       task_stack_ptr;           /* Thread's stack pointer   */
    unsigned long       task_stack_start;         /* Stack starting address   */
    unsigned long       task_stack_end;           /* Stack ending address     */
    unsigned long       task_stack_size;           /* Stack size               */
    unsigned long       task_run_count;            /* Thread's run counter     */

} OSA_TASK;

typedef enum{
  NETWORK_LATCH_CEREG_0,
	NETWORK_LATCH_CEREG_1,
  PDP_ACTIVE,
	PDP_DEACTIVATE,
	NETWORK_DETACHED,
  SIM_INSERT_EVENT,
  SIM_REMOVE_EVENT,
  SIM_READY
}drvRadioState_e;
/**
 * radio status callback
 */
typedef void (*RadioStateCallback)(drvRadioState_e state);
void RadioStateSetCallback(RadioStateCallback callback);

#define FOTA_MINI_ERR_PARAM    -1   /* URL missing, empty or too long */
#define FOTA_MINI_ERR_NET      -2   /* no SIM / not registered / PDP not active */
#define FOTA_MINI_ERR_PACKAGE  -3   /* integrity, wrong patch base, wrong target */

typedef void (*miniFotaResultCallback)(int status);
void miniFotaResultSetCallback(miniFotaResultCallback callback);

/**
 * pointer to thread handle
 */
typedef void osiThread_t;

/**
 * function type of thread entry
 */
typedef void (*osiThreadEntry_t)(void*);

/**
 * function type of callback
 */
typedef void (*osiCallback_t)(void *ctx);

/**
 * osique data structure for message queue
 */
typedef void osiMessageQueue_t;

/**
 * osique data structure for Semaphore
 */
typedef void osiSemaphore_t;

/**
 * osique data structure for Timer
 */
typedef void osiTimer_t;

/**
 * osique data structure for Mutex
 */
typedef void osiMutex_t;

/**
 * opaque data structure for work queue
 */
typedef struct osiWorkQueue osiWorkQueue_t;

/**
 * opaque data structure for work
 */
typedef struct osiWork osiWork_t;

/**
 * function type of callback
 */
typedef void (*osiCallback_t)(void *ctx);

/**
 * function type of rtc alarm callback
 */
typedef void (*AlCallback)(void);

/**
 * osique data structure for Flag
 */
typedef void osiFlag_t;

#define OSI_SUSPEND             0xFFFFFFFF
#define OSI_NO_SUSPEND          0
#define OSI_FLAG_AND            5
#define OSI_FLAG_AND_CLEAR      6
#define OSI_FLAG_OR             7
#define OSI_FLAG_OR_CLEAR       8

/**
 * time info structure
 */
typedef struct {
  int tm_sec;	//seconds [0,59]
  int tm_min;	//minutes [0,59]
  int tm_hour;  //hour [0,23]
  int tm_mday;  //day of month [1,31]
  int tm_mon;   //month of year [1,12]
  int tm_year; // since 1970
  int tm_wday; // sunday = 0
}osiTime_t;

typedef void (*nitz_callback)(osiTime_t rtc_tm);
void register_nitz_callback(nitz_callback callback);

/**
 * create a thread
 *
 * Each \p osiThread_t will have an event queue. So, the event queue
 * depth should be specified at creation.
 *
 * \p name will be copied to thread control block. So, \p name can be dynamic
 * memory.
 *
 * After a thread is created, it will be executed immediately. So, it is
 * possible that \p entry will be executed before the return value is
 * assigned to some variable, if the new thread priority is higher than
 * current thread priority. Pay attention to use thread pointer in
 * \p entry.
 *
 * In some underlay RTOS, there are limitation on maximum stack size.
 * For example, when \p configSTACK_DEPTH_TYPE is defined as \p uint16_t,
 * the stack size must be less than 64KB*4.
 *
 * \param name      thread name
 * \param entry     thread entry function
 * \param argument  thread entry function argument
 * \param priority  thread priority
 * \param stack_size    thread stack size in byte
 * \return
 *      - thread pointer
 *      - NULL if failed
 */
osiThread_t *osiThreadCreate(const char *name, osiThreadEntry_t entry, void *argument,
                             uint8_t priority, uint32_t stack_size);

osiThread_t * osiThreadCreateWithStack(const char *name, osiThreadEntry_t entry, void *argument,
                                      uint8_t priority, void *stack, uint32_t stack_size);
                                      
/**
 * current thread exit
 *
 * When a thread is finished, \p osiThreadExit must be called. And
 * kernel will release thread resources at appropriate time.
 */
void osiThreadExit(void);

/**
 * current thread sleep
 *
 * Change current thread into sleep mode, and will be rescheduled
 * after the specified period.
 *
 * This will use the underlay RTOS mechanism. It is possible the sleep
 * time precision is the tick of underlay RTOS.
 *
 * \param ticks        sleep time in ticks 1 tick = 5ms
 */
void osiThreadSleep(uint32_t ticks);

/**
 * current thread sleep
 *
 * Change current thread into sleep mode, and will be rescheduled
 * after the specified period.
 *
 * This will use the underlay RTOS mechanism. It is possible the sleep
 * time precision is the tick of underlay RTOS.
 *
 * \param ms        sleep time in milliseconds
 */
void osiThreadSleepMs(uint32_t ms);

/**
 * get current thread pointer
 *
 * \return      current thread pointer
 */
osiThread_t *osiThreadCurrent(void);

/**
 * thread get priority
 *
 * \param thread        thread handle
 * \param Priority      thread Priority pointer
 */
bool osiThreadGetPriority(osiThread_t *thread, uint8_t *Priority);

/**
 * thread change priority
 *
 * \param thread        thread handle
 * \param newPriority   thread change new priority
 * \param oldPriority   thread old Priority
 */
bool osiThreadChangePriority(osiThread_t *thread, uint8_t newPriority, uint8_t *oldPriority);

/**
 * thread suspend
 *
 * \param thread        thread handle
 */
bool osiThreadSuspend(osiThread_t *thread);

/**
 * thread resume
 *
 * \param thread        thread handle
 */
bool osiThreadResume(osiThread_t *thread);

/**
 * thread delete
 *
 * \param thread        thread handle
 */
bool osiThreadDelete(osiThread_t *thread);

/**
 * \brief create a work
 *
 * \p run can't be NULL, and \p complete can be NULL.
 *
 * \param run       execute function of the work
 * \param complete  callback to be invoked after the work is finished
 * \param ctx       context of \p run and \p complete
 * \return
 *      - the created work
 *      - NULL if invalid parameter or out of memory
 */
osiWork_t *osiWorkCreate(osiCallback_t run, osiCallback_t complete, void *ctx);

/**
 * \brief delete the work
 *
 * When \p work is running when it is called, it will be deleted after the
 * current run finished.
 *
 * \param work      the work to be deleted
 */
void osiWorkDelete(osiWork_t *work);

/**
 * \brief reset callback of existed work
 *
 * This can change the callbacks of existed work. It can be used to setup
 * a kind of *work flow*. An example:
 *
 * \code{.cpp}
 * static void prvPrepare(void *param) {
 *     // ....
 *     osiWorkResetCallback(work, prvRealJob, NULL, ctx);
 * }
 *
 * static void prvRealJob(void *param) {
 *     // ...
 * }
 *
 * void init(void) {
 *     work = osiWorkCreate(prvPrepare, NULL, ctx);
 *     osiWorkEnqueue(work, wa);
 * }
 * \endcode
 *
 * This is clearer than creating multiple work instances.
 *
 * Though it is not typical application, the callbacks can be changed when
 * the work is already queued. In this case, there may exist corner cases
 * about whether the previous or the new callbacks will be invoked, depends
 * on thread priorities. Caller should take care about this.
 *
 * When it is called inside work callback, the new callbacks will be invoked
 * at the next timer when work is invoked.
 *
 * \param work      the work pointer, must be valid
 * \param run       execute function of the work
 * \param complete  callback to be invoked after the work is finished
 * \param ctx       context of \p run and \p complete
 * \return
 *      - true on success
 *      - false on fail, invalid parameters
 */
bool osiWorkResetCallback(osiWork_t *work, osiCallback_t run, osiCallback_t complete, void *ctx);

/**
 * \brief enqueue a work in specified work queue
 *
 * When \p work is running, it will be queued to \p wq again and then it
 * will be invoked again.
 *
 * When \p work is queued, and \p wq is the same as original work queue,
 * nothing will be done. When \p wq is not the same as the original
 * work queue, it will be removed from the original work queue, and
 * queue the work into the specified work queue.
 *
 * This can be called in ISR.
 *
 * \param work      the work pointer, must be valid
 * \param wq        work queue to run the work, must be valid
 * \return
 *      - true on success
 *      - false for invalid parameter, or work is running
 */
bool osiWorkEnqueue(osiWork_t *work, osiWorkQueue_t *wq);

/**
 * \brief enqueue a work in the last of specified work queue
 *
 * It is similar to \p osiWorkEnqueue, except it will consider work order.
 * For example:
 *
 * \code{.cpp}
 *      osiWorkEnqueue(work1, wq);
 *      osiWorkEnqueue(work2, wq);
 *      osiWorkEnqueue(work1, wq);
 * \endcode
 *
 * If work queue is busy on another work during these calls, and when work
 * queue processing these works, it will:
 * - execute work1->callback
 * - execute work2->callback
 *
 * So, even the order of works aren't preserved. \p work1 is the last queued
 * work, and the real last executed work is \p work2.
 *
 * With \p osiWorkEnqueueLast, it will ensure that the last queued work will
 * be executed at the last.
 *
 * \param work      the work pointer, must be valid
 * \param wq        work queue to run the work, must be valid
 * \return
 *      - true on success
 *      - false for invalid parameter, or work is running
 */
bool osiWorkEnqueueLast(osiWork_t *work, osiWorkQueue_t *wq);

/**
 * \brief cancel a work
 *
 * When \p work is running, the current execution won't be interrupted.
 *
 * \param work      the work pointer, must be valid
 */
void osiWorkCancel(osiWork_t *work);

/**
 * \brief wait a work finish
 *
 * When \p work is running or enqued, this will wait the work finish.
 * When \p timeout is 0, it will return immediately. When \p timeout
 * is \p OSI_WAIT_FOREVER, it will wait infinitely until the work is
 * finished.
 *
 * \param work      the work pointer, must be valid
 * \param timeout   wait timeout in ms
 * \return
 *      - true if the work is finished
 *      - false on invalid parameter, or wait timeout
 */
bool osiWorkWaitFinish(osiWork_t *work, unsigned timeout);


/**
 * \brief get the system high priority work queue
 *
 * A work queue with priority \p OSI_PRIORITY_HIGH will be created at kernel
 * start.
 *
 * \return  the system high priority work queue
 */
osiWorkQueue_t *osiSysWorkQueueHighPriority(void);

/**
 * \brief get the system low priority work queue
 *
 * A work queue with priority \p OSI_PRIORITY_LOW will be created at kernel
 * start.
 *
 * \return  the system low priority work queue
 */
osiWorkQueue_t *osiSysWorkQueueLowPriority(void);

/**
 * get system tick
 * 1 tick is equal to 5 milliseconds
 */
uint32_t osiGetTicks(void);

/**
 * get timer tick 
 * 1 TICK is equal to 30 microseconds
 */
uint32_t osiGetTStimerTicks(void);

/**
 * create a message queue
 *
 * \param msg_count maximum message count can be hold in queue
 * \param msg_size  size of each message in bytes
 * \return
 *      - message queue pointer
 *      - NULL on invalid parameter or out of memory
 */
osiMessageQueue_t *osiMessageQueueCreate(uint32_t msg_count, uint32_t msg_size);

/**
 * delete a message queue
 *
 * When \p mq is NULL, nothing will be done, just as \p free.
 *
 * \param mq    message queue pointer
 */
void osiMessageQueueDelete(osiMessageQueue_t *mq);

/**
 * put a message to message queue
 *
 * \p msg should hold content size the same as \p msg_size specified at
 * \p osiMessageQueueCreate.
 *
 * After put, the content of \p msg will be copied to message queue.
 *
 * When \p mq is full, it will be blocked until there are rooms.
 *
 * \param mq    message queue pointer
 * \param msg   mesage pointer
 * \return
 *      - true on success
 *      - false on invalid parameter
 */
bool osiMessageQueuePut(osiMessageQueue_t *mq, const void *msg);

/**
 * put a message to message queue with timeout
 *
 * This can be called in ISR. And in ISR, \p timeout must be 0.
 *
 * \param mq        message queue pointer
 * \param msg       mesage pointer
 * \param timeout   timeout in milliseconds
 * \return
 *      - true on success
 *      - false on invalid parameter or timeout
 */
bool osiMessageQueueTryPut(osiMessageQueue_t *mq, const void *msg, uint32_t timeout);

/**
 * put a message to message queue front with timeout
 *
 * \p msg should hold content size the same as \p msg_size specified at
 * \p osiMessageQueueCreate.
 *
 * \param mq    message queue pointer
 * \param msg   mesage pointer
 * \param timeout   timeout in milliseconds
 * \return
 *      - true on success
 *      - false on invalid parameter
 */
bool osiMessageQueuePutFront(osiMessageQueue_t *mq, const void *msg, uint32_t timeout);

/**
 * get a message to message queue
 *
 * \p msg should be able tp hold content size of \p msg_size specified at
 * \p osiMessageQueueCreate.
 *
 * After get, the content of message will be copied to \p msg.
 *
 * When \p mq is empty, it will be blocked until there are messages.
 *
 * \param mq    message queue pointer
 * \param msg   mesage pointer
 * \return
 *      - true on success
 *      - false on invalid parameter
 */
bool osiMessageQueueGet(osiMessageQueue_t *mq, void *msg);

/**
 * get a message to message queue with timeout
 *
 * This can be called in ISR. And in ISR, \p timeout must be 0.
 *
 * \param mq        message queue pointer
 * \param msg       mesage pointer
 * \param timeout   timeout in milliseconds
 * \return
 *      - true on success
 *      - false on invalid parameter or timeout
 */
bool osiMessageQueueTryGet(osiMessageQueue_t *mq, void *msg, uint32_t timeout);


/**
 * get the pending message count in message queue
 *
 * It can be called in both thread context and ISR context.
 *
 * \param mq        message queue pointer
 * \return
 *      - the pending message count in message queue
 *      - 0 on invalid parameter
 */
int32_t osiMessageQueuePendingCount(osiMessageQueue_t *mq);

/**
 * get the space of message count in message queue
 *
 * It can be called in both thread context and ISR context.
 *
 * \param mq        message queue pointer
 * \return
 *      - the space of message count in message queue
 *      - 0 on invalid parameter
 */
int32_t osiMessageQueueSpaceCount(osiMessageQueue_t *mq);


/**
 * create a semaphore
 *
 * When \p max_count is 1, it is a binary semaphore. Otherwise, it is
 * counting semaphore.
 *
 * \param max_count     maximum count of the semaphore
 * \param init_count    initial count of the semaphore
 * \return
 *      - semaphore pointer
 *      - NULL on invalid parameter or out of memory
 */
osiSemaphore_t *osiSemaphoreCreate(uint32_t max_count, uint32_t init_count);

/**
 * delete the semaphore
 *
 * When there are blocked thread on the semaphore, the behavior is undefined
 * for \p osiSemaphoreDelete.
 *
 * \param semaphore     semaphore pointer
 */
void osiSemaphoreDelete(osiSemaphore_t *semaphore);

/**
 * acquire from semaphore
 *
 * After acquire, the count of semaphore will be decreased by 1.
 *
 * When the count of semaphore is 0, it will be blocked until the count
 * becomes non-zero (increased by \p osiSemaphoreRelease).
 *
 * \param semaphore     semaphore pointer
 */
void osiSemaphoreAcquire(osiSemaphore_t *semaphore);

/**
 * acquire from semaphore with timeout
 *
 * This can be called in ISR. And in ISR, \p timeout must be 0.
 *
 * \param semaphore     semaphore pointer
 * \param timeout       timeout in milliseconds
 * \return
 *      - true on success
 *      - false on timeout
 */
bool osiSemaphoreTryAcquire(osiSemaphore_t *semaphore, uint32_t timeout);

/**
 * release to semaphore
 *
 * After release, the count of semaphore will be increased by 1.
 * When there are blocked thread on the semaphore, one of the blocked
 * thread will be unblocked.
 *
 * This can be called in ISR.
 *
 * \param semaphore     semaphore pointer
 */
void osiSemaphoreRelease(osiSemaphore_t *semaphore);

/**
 * create a mutex
 *
 * After creation, the mutex is in *open* state.
 *
 * \return
 *      - mutex pointer
 *      - NULL on out of memory
 */
osiMutex_t *osiMutexCreate(void);

/**
 * delete the mutex
 *
 * When \p mutex is NULL, nothing will be done, just as \p free.
 *
 * \param mutex     mutex pointer to be deleted
 */
void osiMutexDelete(osiMutex_t *mutex);

/**
 * lock the mutex
 *
 * When \p mutex is locked by another thread, it will wait forever
 * until the mutex is unlocked.
 *
 * \param mutex     mutex pointer to be locked
 */
void osiMutexLock(osiMutex_t *mutex);

/**
 * lock the mutex with timeout
 *
 * \param mutex     mutex pointer to be locked
 * \param timeout   timeout in milliseconds
 * \return
 *      - true on success
 *      - false on timeout
 */
bool osiMutexTryLock(osiMutex_t *mutex, uint32_t timeout);

/**
 * unlock the mutex
 *
 * \param mutex     mutex pointer to be unlocked
 */
void osiMutexUnlock(osiMutex_t *mutex);

/**
 *
 * \brief        create a flag event group.
 *
 * Parameters:
 *  OSFlagRef                     *rEF    [OT]    Reference.
 *
 * Returns:
 *  OSA_STATUS  OSA Complition Code.
 *
 * Notes: It is needed to call \p osiFlagDelete to free resources.
 *
*/

osiFlag_t *osiFlagCreate(void);

/**
 *
 * \brief     Flag   Set an event.
 *
 * Parameters:
 *  osiFlag_t                     *flag    [IN]    Reference.
 *  uint32_t                       mask    [IN]    flag bit mask
 *  uint32_t                       operation  [IN]    OSI_FLAG_AND  OSI_FLAG_OR
 * Returns:
 *  0       osiFlagSet success.
 *  other   error code
 * Notes: 
 *
*/

int osiFlagSet(osiFlag_t *flag, uint32_t mask, uint32_t operation);

/**
 *
 * \brief     Flag   Wait an event.
 *
 * Parameters:
 *  osiFlag_t                     *flag    [IN]    Reference.
 *  uint32_t                       mask    [IN]    flag bit mask
 *  uint32_t                       operation  [IN]    OSI_FLAG_AND  OSI_FLAG_OR
 *  uint32_t                       *flags  [OUT]    flag value
 *  uint32_t                       timeout  [IN]    timeout in milliseconds
 * Returns:
 *  0       osiFlagWait success.
 *  other   error code or timeout
 * Notes: 
 *
*/

int osiFlagWait(osiFlag_t *flag, uint32_t mask, uint32_t operation, uint32_t *flags, uint32_t timeout);

/**
 *
 * \brief     free flag resources
 *
 * Parameters:
 *  osiFlag_t                     *flag    [IN]    Reference.
 * Returns:
 * Notes: 
 *
*/

void osiFlagDelete(osiFlag_t *flag);

/**
 * \brief create a timer
 *
 * Create a timer with specified callback and callback context. After create,
 * the timer is in stop state. Application can start it in various mode.
 *
 *
 * Otherwise, the callback will be executed in the specified thread through
 * *event* mechanism.
 *
 * It is needed to call \p osiTimerDelete to free resources.
 *
 * \param cb        callback to be executed after timer expire
 * \param ctx       callback context
 * \return
 *      - the created timer instance
 *      - NULL at out of memory, or invalid parameter
 */
osiTimer_t *osiTimerCreate( osiCallback_t cb, void *ctx);

/**
 * \brief set/change callback of timer
 *
 * In most cases, timer callback set at create is not needed to be changed.
 *
 * \p timer should be created by \p osiTimerCreate. Special values of
 * \p thread follow \p osiTimerCreate.
 *
 * It is permitted to change \p thread of timer, including special values.
 * For example, it is permitted to change a timer executing in timer service
 * to timer executing in specified thread. However, it is not recommended.
 *
 * This can only be called when \p timer is stopped. Otherwise, it will
 * return false.
 *
 * \param timer     timer created
 * \param cb        callback to be executed after timer expire
 * \param ctx       callback context
 * \return
 *      - true on success
 *      - false on invalid parameter
 */
bool osiTimerSetCallback(osiTimer_t *timer, osiCallback_t cb, void *ctx);

/**
 * \brief delete a timer
 *
 * Delete the timer, and free associated resources.
 *
 * When the timer callback will be executed in thread rather than ISR, and the
 * event for timer expiration has been sent, some resources won't be freed
 * immediately. Rather, they will be freed after the timer expiration event
 * is popped out from thread event queue. However, the callback won't be
 * executed even delete is delayed.
 *
 * Refer to document about corner case of timer thread callback.
 *
 * \param timer     the timer to be deleted
 */
void osiTimerDelete(osiTimer_t *timer);

/**
 * \brief start a timer
 *
 * Start a timer, and the timer will be expired in specified period from
 * now. After the callback is executed, the timer will come to restart timer
 * automatically.
 *
 * When the timer is in stated state before this function, the previous
 * expiration won't be executed.
 *
 * Refer to document about corner case of timer thread callback.
 *
 * It is valid that the timeout period is 0. In this case, the timer will
 * expire very soon.
 *
 * Due to timeout is 32bits of milliseconds, The maximum timeout period is
 * ~50 days.
 *
 * It is the same as \p osiTimerStartRelaxed, with \p relaxed_ms is 0.
 *
 * \param timer     the timer to be started
 * \param ms        timeout period
 * \return
 *      - true on success
 *      - false on invalid parameter
 */
bool osiTimerStart(osiTimer_t *timer, uint32_t ms);

/**
 * \brief stop a time
 *
 * Stop a not-started or stopped timer is valid, just do nothing.
 *
 * Refer to document about corner case of timer thread callback.
 *
 * \param timer     the timer to be stopped
 * \return
 *      - true on success
 *      - false on invalid parameter
 */
bool osiTimerStop(osiTimer_t *timer);

/**
 * \brief clean memory cache
 *
 * \param pMem     memory clean start address
 * \param size     memory clean size
 */
void osiCacheClean(void *pMem, uint32_t size);

/**
 * \brief set dump config
 *
 * \param enable     
 *              - ture    when system comes to crash, wait and dump
 *              - flase    when system comes to crash, reset
 */
void osiExceptionDumpEnable(bool enable);

/**
 * \brief set system power down
 *
 */
void osiSysPoweroff(void);

/**
 * \brief set system power reset
 *
 */
void osiSysReset(void);

/**
 * \brief osiwd functions is unuseful, waiting for sb needs
 *
 */
void osiWdtEnable(bool enable);
void osiWdtSetTimeout(uint8_t timeout);

/**
 * \brief set RTC time info
 *
 */
long osiMkTime(const osiTime_t *tm);

/**
 * \brief record RTC time info
 *
 */
void osiGetTime(osiTime_t *time);

/**
 * \brief Set RTC time
 * 
* \param time RTC time     
 *
 */
void osiSetTime(osiTime_t *time);

/**
 * \brief init RTC alarm callback
 * 
* \param callback alarm callback    
 *
 */
void osiInitAlarm(AlCallback callback);

/**
 * \brief Set RTC alarm
 * 
* \param time RTC time     
 *
 */
void osiSetAlarm(osiTime_t *time);

/**
 * \brief Get RTC alarm
 * 
* \param time RTC time     
 *
 */
void osiGetAlarm(osiTime_t *time);

/**
 * \brief Enable RTC alarm
 */
void osiEnableAlarm(bool enable);

/**
 * \brief record timezone info
 *
 */
int32_t osiGetTimeZone(void);

/**
 * \brief record timezone info in minute
 *
 */
int32_t osiGetTimeZoneMin(void);

/**
 * \brief set timezone info in minute
 *
 */
int32_t osiSetTimeZoneMin(int32_t time_zone);

/**
 * \brief record time info
 *
 */
uint32_t osiGetTimeSec(void);

/**
 * \brief function as the same as malloc
 *
 */
void *osiMalloc(size_t size);

/**
 * \brief function as the same as calloc
 *
 */
void *osiCalloc(size_t num, size_t size);
/**
 * \brief function as the same as free
 *
 */
int osiFree(void *buff);

/**
 * \brief function as the same as realloc
 *
 */
void *osiRealloc(void *ptr, size_t size);

/**
 * \brief get free size of memory
 * 
 * \param total     if total is not NULL, get total size
 * \param str       heap_check
 * 
 * \return          free size
 *
 */
uint32_t osiHeapFreeSizeGet(uint32_t *total, char *str);

/*typedef enum {
	RSTREASON_POWEROFF_CHARGE = 'C', //Charging in "powering off" mode
	RSTREASON_RD_PRODUCTION   = 'R', //Rd production mode
	RSTREASON_RTC_ALARM       = 'A', //rtc Alarm
	RSTREASON_NORMAL_POWERON  = 'N', //Normal powering on
	RSTREASON_ERROR_CONDITION = 'E', //Error reset, eg. watchdog reset triggered by system hang or silent reset
	RSTREASON_ENTER_DOWNLOAD  = 'D', //image Download mode

} SW_RESTART_REASON ;*/

/**
 * \brief Get Restart Reson
 *
 * \return 
 *        - 0         default
 *        - reason    Restart Reason, elements in SW_RESTART_REASON
 */
uint32_t osiGetRestartReason(void);

/**
 * \brief Get PowerUp Reson
 *
 * \return          PowerUp Reason
 */
uint32_t osiGetPowerUpReason(void);

/**
 * \brief Get PowerDown Reson
 *
 * \return          PowerDown Reason
 */
uint32_t osiGetPowerDownReason(void);

/**
 * \brief Operates the "assertFail" as needed
 */
void utilsAssertFail(const char      *cond,
                            const char      *file,
                            signed short    line,
                            unsigned char   allowDiag);


#ifndef assert
#define assert(cOND)	{if (!(cOND)) utilsAssertFail(#cOND, __FILE__, (short)__LINE__, 1);}
#endif

/**
 * \brief used to printf data to AP uart
 *
 */
void RTI_LOG(const char* fmt, ...);
void RTI_LOG1(const char* fmt, ...);
void CAT_LOG(const char* fmt, ...);

/**
 * \brief get at2TmsMsgQue data
 *
 */
osiMessageQueue_t *osiGetAT2TmsMsgQRef(void);

/**
 * \brief get tms2AtMsgQue data
 *
 */
osiMessageQueue_t *osiGetTms2ATMsgQRef(void);

/**
 * \brief set system into asleep
 *
 */
void osiSetPMEnableSleep(bool enable);

/**
 * \brief get eps apn 
 *
 */
void osiGetEpsApn(char *buff);

/**
 * \brief get eps IP type 
 *
 */
int osiGetEpsIpType(void);

/**
 * \brief get security boot flag 
 *
 */
bool osiIsSecBootEnable(void);

/**
 * \brief enter critical section
 *
 * The underlay RTOS may have different implementation for critical
 * section. It may manipulate CPU IRQ enable bit(s), or manipulate
 * IRQ mask.
 *
 * This can be called in ISR.
 *
 * \return  critical section flags
 */
uint32_t osiEnterCritical(void);

/**
 * \brief exit critical section
 *
 * Critical section flags is implementation depend. It should be the value
 * returned by \p osiEnterCritical, and don't change the value manually.
 *
 * This can be called in ISR.
 *
 * \param critical  critical section flags
 */
void osiExitCritical(uint32_t critical);

/**
 * \brief set simcard detect gpio pin
 * \param detect  gpio pin
 */
void setSimCardDetectPin(uint32_t detect_pin);

void GetCPUInfo(unsigned int *cpuUsedRate);
void GetTaskInfo(void* taskRef,OSA_TASK *task_info);
void GetTaskStackInfo(void* taskRef, char **pName, unsigned long *pStackSize, unsigned long *pStackInuse, unsigned long *pStackPeak);

typedef void (*CmtiCallback)(const char *storage, int index);
void smsSetCmtiCallback(CmtiCallback callback);

#ifdef __cplusplus
}
#endif
#endif
