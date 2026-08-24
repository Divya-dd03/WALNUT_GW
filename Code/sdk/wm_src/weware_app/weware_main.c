/**
  ******************************************************************************
  * @file    weware_main.c
  * @author  WheelsEye
  * @brief   Application image entry point for the weware customer application.
  *          appimg_enter() is invoked by the platform after boot to bring up
  *          the system and start the weware application.
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
#include "sdk_wm.h"
#include "sdk_log.h"
#include "sdk_os.h"
/* Common infra (ports of the reference firmware's system layer) */
#include "common/utils.h"
#include "common/event_manager.h"
#include "module/module_manager.h"
#include "system/system_manager.h"

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
     * sdk_debug_print is raw ("%s" - no newline appended); the sdk_log_*
     * levels print as "[INFO] ...\r\n" / "[WARN] ...\r\n" / "[ERROR] ...\r\n" */
    sdk_debug_print("sdk_debug_print : Main APP\r\n");
    sdk_log_info("sdk_log_info : Main APP\r\n");
    sdk_log_warning("sdk_log_warning : Main APP\r\n");
    sdk_log_error("sdk_log_error : Main APP\r\n");

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

int appimg_enter(void *param)
{
    sAPI_Debug("weware application image enter, param 0x%x", param);

    wm_board_ID = WM_CURRENT_BOARD;
    wm_battery_ID = WM_CURRENT_BATTERY;
    wm_sleep_mode = WM_CURRENT_SLEEP_MODE;
    RTI_LOG("Weware Application Boot Start");
    if (wm_system_init() != SDK_RESULT_SUCCESS)
        RTI_LOG("wm_system_init reported an error");
    RTI_LOG("WM WEWARE SYSTEM INIT DONE ----------------------");

    /* Unmute the USB VCOM log path (gf_debug flag). Without this, wm_printf
     * and every sdk_log_* call return silently. sdk_log_init_uart() is just
     * a wrapper for the same call - its port/config args are ignored. */
    wm_logger_mode(TRUE);

    weware_log_check();

    /* Initialize weware components*/
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
            sdk_debug_print("[weware_main] Module task timeout detected - rebooting\r\n");
            (void)event_manager_broadcast(EVENT_RESET_SOFT, "SYSTEM", NULL, 0);
        }

        sdk_task_sleep(1000);
    }

    return 0;
}

void appimg_exit(void)
{
    sAPI_Debug("weware application image exit");
}
