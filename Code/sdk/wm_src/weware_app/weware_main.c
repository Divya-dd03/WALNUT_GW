/**
  ******************************************************************************
  * @file    weware_main.c
  * @author  WheelsEye
  * @brief   Application image entry point for the weware customer application.
  *          appimg_enter() is invoked by the platform after boot on the kernel
  *          "app_load" task. That task's stack is small and kernel-owned, so
  *          appimg_enter only bootstraps the system and spawns WEMAIN (the
  *          application's own task, with its own stack), then returns - same
  *          contract the vendor demo follows (WM_Entry_Task_Top_Most). Running
  *          the app inline here overflows app_load's stack ("stack of app_load
  *          overflow by itself" EE dump, or corruption asserts in other tasks).
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 WheelsEye
  * All rights reserved.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "wm_global.h"
#include "wm_sdk_wm.h"
#include "wm_sdk_log.h"
#include "wm_sdk_os.h"
/* Common infra (ports of the reference firmware's system layer) */
#include "common/utils.h"
#include "common/event_manager.h"
#include "module/module_manager.h"
#include "system/system_manager.h"

/* Main application task: each wm_sdk_log/wm_printf call alone costs ~1.3KB of
 * stack (vsnprintf frames), and system/module init chains go deep. The OTA
 * cycle also runs inline here (reference structure): the HTTPS version check
 * is async (request runs on the kernel worker), but wm_sdk_https_read() needs
 * ~8 KB and the kernel's synchronous ranged-download helpers
 * (sdk_https_download_get_file_size/read_chunk) ~16 KB on the caller
 * (wm_sdk_https.h). 10 KB was confirmed to overflow with OTA enabled. */
#define WEWARE_MAIN_TASK_STACK   (1024 * 24)

/* Log-level threshold used by the WM_LOG_* macros (defined in wm_src lib) */
extern int g_wm_log_level;

/**
 * @brief  Exercise every logging path (USB/UART) once, tagged with the
 *         function name so each backend can be identified on the consoles.
 */
static void weware_log_check(void)
{
    /* --- Kernel loggers (AP UART) ------------------------------------- */
    RTI_LOG("RTI_LOG : Main APP");
    RTI_LOG1("RTI_LOG1 : Main APP");
    CAT_LOG("CAT_LOG : Main APP");
    sAPI_Debug("sAPI_Debug : Main APP\r\n");        /* alias of CAT_LOG */

    /* --- USB / debug console ------------------------------------------ */
    wm_printf("wm_printf : Main APP\r\n");

    /* --- SDK log API (USB VCOM via wm_printf) ---------------------------
     * wm_sdk_debug_print is raw ("%s" - no newline appended); the wm_sdk_log_*
     * levels print as "[INFO] ...\r\n" / "[WARN] ...\r\n" / "[ERROR] ...\r\n" */
    wm_sdk_debug_print("wm_sdk_debug_print : Main APP\r\n");
    wm_sdk_log_info("wm_sdk_log_info : Main APP\r\n");
    wm_sdk_log_warning("wm_sdk_log_warning : Main APP\r\n");
    wm_sdk_log_error("wm_sdk_log_error : Main APP\r\n");

    /* --- Level-filtered wrappers (forward to sAPI_Debug) ---------------- */
    WM_LOG_DEBUG("WM_LOG_DEBUG : Main APP");
    WM_LOG_INFO("WM_LOG_INFO : Main APP");
    WM_LOG_WARNING("WM_LOG_WARNING : Main APP");
    WM_LOG_ERROR("WM_LOG_ERROR : Main APP");
    WM_LOG_CRITICAL("WM_LOG_CRITICAL : Main APP");
}

/**
 * @brief  Bring up the system layer then all modules - mirrors the reference
 *         firmware's system_manager_init() + module_manager_init() flow.
 *         Per-module init order lives in the registry (module_config.c).
 */
static void weware_init(void)
{
    /* System layer: device utils, flash dirs, event manager, pre-boot
     * record, module config file (system_manager.c). */
    if (system_manager_init() != RESULT_SUCCESS)
        RTI_LOG("system_manager_init reported an error");
    else
        RTI_LOG("system_manager_init done");

    /* Initialize all registered modules (module_config.c order) */
    if (module_manager_init() != RESULT_SUCCESS)
        RTI_LOG("module_manager_init reported an error (required module failed)");
    else
        RTI_LOG("module_manager_init done");
}

/**
 * @brief  Application main task (WEMAIN). Owns all weware init and the
 *         supervision loop; runs on its own 8KB stack, never returns.
 */
static void weware_main_task(void *arg)
{
    (void)arg;

    weware_log_check();

    /* Initialize weware components */
    weware_init();

    while (1)
    {
        /* Uptime guard + ADC/vehicle-state poll + periodic status print */
        system_manager_loop_iteration();

        /* Task-stall watchdog: every module task now feeds
         * module_manager_update_uptime() at the top of its loop; a task
         * silent for MODULE_TASK_TIMEOUT_SEC triggers a deferred soft reset
         * (reset_handler persists state before the SoC reset). */
        if (module_manager_loop_iteration()) {
            wm_sdk_debug_print("[weware_main] Module task timeout detected - rebooting\r\n");
            (void)event_manager_broadcast(EVENT_RESET_SOFT, "SYSTEM", NULL, 0);
        }

        wm_sdk_task_sleep(1000);
    }
}

int appimg_enter(void *param)
{
    void *main_task;

    sAPI_Debug("weware application image enter, param 0x%x", param);

    wm_board_ID = WM_CURRENT_BOARD;
    wm_battery_ID = WM_CURRENT_BATTERY;
    wm_sleep_mode = WM_CURRENT_SLEEP_MODE;
    RTI_LOG("Weware Application Boot Start");
    if (wm_system_init() != WM_SDK_RESULT_SUCCESS)
        RTI_LOG("wm_system_init reported an error");
    RTI_LOG("WM WEWARE SYSTEM INIT DONE ----------------------");

    /* Unmute the USB VCOM log path (gf_debug flag). Redundant after
     * wm_system_init (wm_pre_boot_init already sets it) but kept as
     * explicit documentation of the logging gate. */
    wm_logger_mode(TRUE);

    /* Hand off to the application's own task and return: appimg_enter runs
     * on the kernel app_load task, whose small stack must not host the app.
     * The kernel logs "Customer APP exit code:0" once we return - that line
     * in the boot log is the health check for this contract. */
    main_task = wm_sdk_task_create(weware_main_task, NULL, "WEMAIN", NULL,
                                WEWARE_MAIN_TASK_STACK, TP_UI_TOP_THREAD);
    if (main_task == NULL) {
        RTI_LOG("FATAL: WEMAIN task create failed - application not started");
        return -1;
    }

    return 0;
}

void appimg_exit(void)
{
    sAPI_Debug("weware application image exit");
}
