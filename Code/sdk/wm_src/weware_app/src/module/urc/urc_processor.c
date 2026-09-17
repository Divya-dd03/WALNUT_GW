/**
 * @file urc_processor.c
 * @brief 
 * @version 0.1
 * @date 2026-08-20
 * 
 */


// sdk
#include "wm_sdk_urc.h"
#include "wm_sdk_wm.h"
#include "wm_sdk_types.h"
#include "wm_sdk_os.h"
#include "wm_sdk_log.h"
#include "wm_global.h"

#include "stdbool.h"

// app
#include "module/urc/urc_processor.h"
#include "module/module_manager.h"
#include "common/queue_manager.h"
#include "common/task_stats.h"

#define LOG_TAG "URC"
#define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

/*--------------------------------------------------------------*/

#define URC_TASK_STACK_SIZE  (4096U)
#define URC_TASK_SLEEP_ACTIVE_MS (5U)
#define URC_TASK_SLEEP_IDLE_MS     (200U)
/* Modem→app msgq depth. wm_sdk_urc_dispatch pushes with timeout 0 and silently
 * drops events when full; elements are 4-byte urcEvent_e codes, so headroom
 * is cheap. */
#define URC_MSGQ_CAPACITY         (16U)
#define URC_TIMEOUT_MS              (1U)

// task and msgq for urc
static void* g_urc_task = NULL;
/* Stack usage sampling for the URC task (reference: g_urc.task_stats) */
static TaskStats g_urc_task_stats;
static void* g_urc_msgq = NULL;
static BOOL g_urc_task_initialied = FALSE;

static const char *urc_event_name(urcEvent_e ev)
{
    switch (ev)
    {
    case URC_PDP_ACTIVE:                return "PDP_ACTIVE";
    case URC_PDP_INACTIVE:              return "PDP_INACTIVE";
    case URC_NET_ACTIVE:               return "NET_ACTIVE";
    case URC_NET_DISCONNECTED:         return "NET_DISCONNECTED";
    case URC_SIM_INSERTED:             return "SIM_INSERTED";
    case URC_SIM_REMOVED:              return "SIM_REMOVED";
    case URC_SIM_READY:                return "SIM_READY";
    case URC_SIM_EJECTED_FOR_LONG_TIME:return "SIM_EJECTED_LONG";
    case URC_USB_PLUGGED:              return "USB_PLUGGED";
    case URC_USB_REMOVED:              return "USB_REMOVED";
    case URC_NO_PDP_FOR_LONG_TIME:     return "NO_PDP_LONG";
    case URC_RADIO_REFRESH:            return "RADIO_REFRESH";
    default:                           return "UNKNOWN";
    }
}

/*
 * CG-parity dispatch (reference urc_mask_to_module_id): walnut URCs are bare
 * urcEvent_e codes with no mask/payload, so routing keys on the event code.
 * USB events have no consumer module. The reference's SMS inlining and GNSS
 * NMEA assembler have no walnut counterpart - the kernel emits neither URC.
 */
static ModuleId urc_event_to_module_id(urcEvent_e event)
{
    switch (event)
    {
    case URC_PDP_ACTIVE:
    case URC_PDP_INACTIVE:
    case URC_NET_ACTIVE:
    case URC_NET_DISCONNECTED:
    case URC_NO_PDP_FOR_LONG_TIME:
    case URC_RADIO_REFRESH:
        return MODULE_ID_NETWORK;

    case URC_SIM_INSERTED:
    case URC_SIM_REMOVED:
    case URC_SIM_READY:
    case URC_SIM_EJECTED_FOR_LONG_TIME:
        return MODULE_ID_SIM;

    default:
        return MODULE_ID_COUNT;
    }
}

static void urc_process_message(urcEvent_e event)
{
    LOG_INFO("URC EVENT [%d]: %s", (int)event, urc_event_name(event));

    ModuleId mid = urc_event_to_module_id(event);
    if (mid >= MODULE_ID_COUNT)
        return;                       /* no consumer module for this event */

    Module *mod = module_manager_get_module(mid);
    if (!mod || !mod->config.enabled || !mod->config.urc_q)
    {
        /* SIM urc_q is not created yet (SIM module polls); rare events, keep visible */
        LOG_WARN("URC not delivered: module %s (id=%u) missing, disabled, or no urc_q",
                        (mod && mod->config.name) ? mod->config.name : "(null)",
                        (unsigned)mid);
        return;
    }

    UINT32 code = (UINT32)event;
    if (queue_push(mod->config.urc_q, &mod->config.urc_q_config, &code) != RESULT_SUCCESS)
        LOG_ERROR("URC queue_push failed for module %s", mod->config.name);
}


static void urc_task_entry(void* arg)
{
    (void)arg;

    if (!g_urc_msgq)
    {
        LOG_ERROR("URC TASK ABORT: URC message queue not created");
        return;
    }

    /* Sole kernel URC registrant (mask = all 12 urcEvent_e codes): every URC
     * lands here and is fanned out to module urc_q's by urc_process_message.
     * Modules must not wm_sdk_urc_register their own queues. */
    wm_SdkResult result = wm_sdk_urc_register(g_urc_msgq, 0xFFFFFFFFu);
    if (result != WM_SDK_RESULT_SUCCESS)
    {
        LOG_ERROR("URC TASK ABORT: Failed to register URC message queue");
        return;
    }

    LOG_INFO("URC task started");

    while (1)
    {
        /* Task-stall watchdog feed (module_manager_monitor_tasks) */
        module_manager_update_uptime(MODULE_ID_URC);
        (void)task_stats_update_periodic(g_urc_task, "URC", MODULE_ID_URC,
                                         &g_urc_task_stats, 0);

        /*
         * Drain modem URC queue in one visit: first recv waits up to
         * URC_TIMEOUT_MS, then timeout 0 until empty. The outer sleep must
         * ALWAYS run — never `continue` past it: wm_sdk_msgq_recv returns
         * WM_SDK_RESULT_TIMEOUT (-2) for both "empty" and "queue gone" (osi bool),
         * and with the 5 ms kernel tick a 1 ms timeout is a no-wait poll, so a
         * recv-only loop busy-spins and starves every lower-priority task.
         */
        UINT32 recv_timeout_ms = URC_TIMEOUT_MS;
        BOOL drained_any = FALSE;

        for (;;)
        {
            UINT32 event = 0;
            wm_SdkResult recv_result = wm_sdk_msgq_recv(g_urc_msgq, &event, recv_timeout_ms);
            recv_timeout_ms = 0U;

            if (recv_result == WM_SDK_RESULT_TIMEOUT)
                break;

            if (recv_result != WM_SDK_RESULT_SUCCESS)
            {
                LOG_ERROR("URC TASK ERROR: unexpected recv result %d", (int)recv_result);
                break;
            }

            drained_any = TRUE;
            urc_process_message((urcEvent_e)event);
        }

        wm_sdk_task_sleep(drained_any ? URC_TASK_SLEEP_ACTIVE_MS : URC_TASK_SLEEP_IDLE_MS);
    }

    // task should not reach here
    LOG_ERROR("URC task exited unexpectedly");
}


static wm_SdkResult urc_create_queue(void)
{
    if (g_urc_msgq) return WM_SDK_RESULT_SUCCESS;

    g_urc_msgq = wm_sdk_msgq_create("urcMsgQ", sizeof(UINT32), URC_MSGQ_CAPACITY, 0);
    if (!g_urc_msgq)
    {
        LOG_ERROR("Failed to create URC message queue");
        return WM_SDK_RESULT_ERROR;
    }

    return WM_SDK_RESULT_SUCCESS;
}


static wm_SdkResult urc_create_task(void)
{
    if (g_urc_task) return WM_SDK_RESULT_SUCCESS;

    g_urc_task = wm_sdk_task_create(urc_task_entry, NULL, "URC_TASK", 
                            NULL, URC_TASK_STACK_SIZE, TP_TIMED_ACTIVITY);
    if (!g_urc_task)
    {
        LOG_ERROR("Failed to create URC task");
        return WM_SDK_RESULT_ERROR;
    }

    return WM_SDK_RESULT_SUCCESS;
}


static void urc_set_defaults(void)
{
    g_urc_task = NULL;
    g_urc_msgq = NULL;
}


wm_SdkResult urc_processor_init(void)
{
    if (g_urc_task_initialied) {
        LOG_WARN("URC processor already initialized");
        return WM_SDK_RESULT_SUCCESS;
    }

    urc_set_defaults();

    if (urc_create_queue() != WM_SDK_RESULT_SUCCESS)
    {
        LOG_ERROR("Failed to create URC message queue");
        return WM_SDK_RESULT_ERROR;
    }

    if (urc_create_task() != WM_SDK_RESULT_SUCCESS)
    {
        LOG_ERROR("Failed to create URC task");
        return WM_SDK_RESULT_ERROR;
    }

    g_urc_task_initialied = TRUE;
    return WM_SDK_RESULT_SUCCESS;
}


wm_SdkResult urc_processor_deinit(void)
{
    if (!g_urc_task_initialied) {
        LOG_WARN("URC processor not initialized");
        return WM_SDK_RESULT_SUCCESS;
    }

    if (g_urc_msgq)
        wm_sdk_urc_unregister(g_urc_msgq);

    if (g_urc_task)
        wm_sdk_task_delete(g_urc_task);

    if (g_urc_msgq)
        wm_sdk_msgq_delete(g_urc_msgq);

    urc_set_defaults();

    g_urc_task_initialied = FALSE;
    return WM_SDK_RESULT_SUCCESS;
}
