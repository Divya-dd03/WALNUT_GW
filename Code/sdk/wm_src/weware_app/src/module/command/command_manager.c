/**
 * @file command_manager.c
 * @brief Command manager implementation for weware platform - handles command processing, parsing, and management
 */

/* Includes ------------------------------------------------------------------*/
#include "module/command/command_manager.h"
#include "module/command/command_handler.h"
#include "module/command/command_config.h"
#include "system/system_config.h"
#include "common/task_stats.h"
#include "common/utils.h"
#include "module/module_manager.h"
#include "common/queue_manager.h"
#ifndef UART_UNAVAILABLE
#include "module/uart/uart_manager.h"
#endif /* UART_UNAVAILABLE */
#include "module/gps/gps_config.h"
#include "module/network/network_config.h"
#include "module/tcp/tcp.h"

#include "sdk_platform.h"
#include "functionality/sdk_functionality_os.h"

#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "ctype.h"
#include "strings.h"
#include <stddef.h>

#include "wm_sdk_log.h"

/*---------------------------------------------------------------
 * Log Configuration
 *--------------------------------------------------------------*/
#define LOG_TAG "CMD"
#define LOG_MODULE_LEVEL LOG_LEVEL_DEBUG
#include "module/log/log.h"

/*---------------------------------------------------------------
 * External Declarations
 *--------------------------------------------------------------*/
extern Module *g_modules[];

/*---------------------------------------------------------------
 * Forward Declarations
 *--------------------------------------------------------------*/
static void cmd_set_runtime_defaults(void);

/* Forward declaration for MOD reboot command handler */
Result cmd_mod_reboot(const char* args_string, char* response_buffer, size_t buffer_size);

/* Forward declaration for GET-DEV-INFO command handler */
Result cmd_get_dev_info(const char* args_string, char* response_buffer, size_t buffer_size);

/* Forward declaration for GET-DEVICE-STATUS command handler */
Result cmd_get_dev_status(const char* args_string, char* response_buffer, size_t buffer_size);

/* Forward declaration for DELETE-FOLDER command handler */
Result cmd_delete_folder(const char* args_string, char* response_buffer, size_t buffer_size);

/* Forward declaration for FACTORY-RESET command handler */
Result cmd_factory_reset(const char* args_string, char* response_buffer, size_t buffer_size);

/* Forward declaration for HARD-RESET (immediate SoC reset) */
Result cmd_hard_reset(const char* args_string, char* response_buffer, size_t buffer_size);

/* Forward declaration for DIGOUT relay command */
Result cmd_digout(const char* args_string, char* response_buffer, size_t buffer_size);

Result cmd_set_min_csq(const char* args_string, char* response_buffer, size_t buffer_size);

/* Forward declaration for FORCED-OTA command handler */
Result cmd_force_ota(const char* args_string, char* response_buffer, size_t buffer_size);

/* Forward declaration for GET-OTA-STATUS command handler */
Result cmd_get_ota_status(const char* args_string, char* response_buffer, size_t buffer_size);

/* TEMPORARY (debug): manual GNSS power control - remove with the two table entries */
Result cmd_set_gps_on(const char* args_string, char* response_buffer, size_t buffer_size);
Result cmd_set_gps_off(const char* args_string, char* response_buffer, size_t buffer_size);

/* ============================================================================
 * Private Constants and Macros
 * ============================================================================ */

/* ============================================================================
 * Globals (DO NOT MAKE STATIC)
 * Runtime State (MUST BE GLOBAL – ABI DEPENDENCY)
 * ============================================================================ */
CommandConfig g_command_config = {0};  /* Command module configuration storage */
command_manager_runtime_t g_command_manager = {
    .task_ref = NULL,
    .task_stack = {0},
    .initialized = FALSE,
    .module = NULL,
    .config = &g_command_config,
    .stats = {0},
    .task_stats = {0},
    .task_interval_ms = 0            /* Will be set by cmd_set_runtime_defaults() */
};

/* ============================================================================
 * Global Variables - Command Table
 * ============================================================================ */
const cmd_handler_entry_t g_cmd_table[] = {   // Command handler table - add new commands here
    {CMD_SET_GPS_CONFIG, "SET-GPS-CONFIG", TRUE, "Set GPS configuration", (module_func_t)gps_config_set},
    {CMD_GET_GPS_CONFIG, "GET-GPS-CONFIG", FALSE, "Get GPS configuration", (module_func_t)gps_config_get_string},
    {CMD_SET_NETWORK_CONFIG, "SET-NETWORK-CONFIG", TRUE, "Set Network configuration", (module_func_t)network_config_set},
    {CMD_GET_NETWORK_CONFIG, "GET-NETWORK-CONFIG", FALSE, "Get Network configuration", (module_func_t)network_config_get_string},
    {CMD_SET_TCP_CONFIG, "SET-TCP-CONFIG", TRUE, "Set TCP configuration", (module_func_t)weware_tcp_config_set},
    {CMD_GET_TCP_CONFIG, "GET-TCP-CONFIG", FALSE, "Get TCP configuration", (module_func_t)weware_tcp_config_get_string},
    {CMD_SET_COMMAND_CONFIG, "SET-COMMAND-CONFIG", TRUE, "Set Command configuration", (module_func_t)command_config_set},
    {CMD_GET_COMMAND_CONFIG, "GET-COMMAND-CONFIG", TRUE, "Get Command configuration", (module_func_t)command_config_get_string},
    {CMD_SET_SYSTEM_CONFIG, "SET-SYSTEM-CONFIG", TRUE, "Set System (ign/motion) configuration", (module_func_t)system_config_set},
    {CMD_GET_SYSTEM_CONFIG, "GET-SYSTEM-CONFIG", FALSE, "Get System (ign/motion) configuration", (module_func_t)system_config_get_string},
    {CMD_REBOOT, "REBOOT", TRUE, "Reboot module (soft reset)", (module_func_t)cmd_mod_reboot},
    {CMD_HARD_RESET, "HARD-RESET", TRUE, "Immediate SoC reset (no deinit, no pre-boot save)", (module_func_t)cmd_hard_reset},
    {CMD_GET_DEV_INFO, "GET-DEV-INFO", FALSE, "Get device information", (module_func_t)cmd_get_dev_info},
    {CMD_GET_DEV_STATUS, "GET-DEV-STATUS", FALSE, "Get device status", (module_func_t)cmd_get_dev_status},
    {CMD_DELETE_FOLDER, "DELETE-FOLDER", TRUE, "Delete all files in folder (args: folder name under flash root)", (module_func_t)cmd_delete_folder},
    {CMD_FACTORY_RESET, "FACTORY-RESET", TRUE, "Clear C:/config,queue,fota,preboot files then soft reboot", (module_func_t)cmd_factory_reset},
    {CMD_DIGOUT, "DIGOUT", TRUE, "Relay digout true=LOW/false=HIGH (GPS check required for true)", (module_func_t)cmd_digout},
    {CMD_SET_MIN_CSQ, "SET-MIN-CSQ", TRUE, "Min CSQ for A-GPS/OTA HTTPS (0-31, 0=disabled)", (module_func_t)cmd_set_min_csq},
    {CMD_FORCED_OTA, "FORCED-OTA", TRUE, "Force OTA cycle (SIMCOM,STM,peri) bypassing prereq + cooldown gates", (module_func_t)cmd_force_ota},
    {CMD_GET_OTA_STATUS, "GET-OTA-STATUS", TRUE, "Report last OTA outcome/reason (STM+SIMCOM) for field debug", (module_func_t)cmd_get_ota_status},
    {CMD_PING_STM, "PING-STM", TRUE, "Probe STM (get-device-info, 5s x3); replies STM OK/FAIL to sender", NULL},
    /* TEMPORARY (debug): manual GNSS power control. Lets the receiver be power-cycled
     * from software instead of the bench button, to exercise config re-application.
     * Remove these two rows, the two enums and gps_ops_set_power_enabled() together. */
    {CMD_SET_GPS_ON, "SET-GPS-ON", TRUE, "TEMP: power GNSS receiver on and re-apply config", (module_func_t)cmd_set_gps_on},
    {CMD_SET_GPS_OFF, "SET-GPS-OFF", TRUE, "TEMP: power GNSS receiver off (parse-fail reboot suppressed)", (module_func_t)cmd_set_gps_off},
};
const UINT32 g_cmd_table_size = sizeof(g_cmd_table) / sizeof(g_cmd_table[0]);  // Number of commands in table

/* ============================================================================
 * Private Function Declarations
 * ============================================================================ */
static void cmd_task_entry(void *arg);
static void cmd_update_task_stats(void);
static Result cmd_process_command(const ModuleMessage* request, char* response_buffer, size_t buffer_size);

/* ============================================================================
 * Public API
 * ============================================================================ */

Result command_manager_accept_request(const ModuleMessage* request)
{
    if (!g_command_manager.initialized || !g_command_manager.module || !request || !g_command_manager.module->config.msg_q) {
        return RESULT_ERROR;
    }
    
    Result result = queue_push(g_command_manager.module->config.msg_q, &g_command_manager.module->config.msg_q_config, request);
    return (result == RESULT_SUCCESS) ? RESULT_SUCCESS : RESULT_BUSY;
}


/* ============================================================================
 * Private Function Implementation
 * ============================================================================ */

/**
 * @brief Set runtime variables to default values
 */
static void cmd_set_runtime_defaults(void)
{
    g_command_manager.task_interval_ms = 100;           /* Default: 100 ms */
}

/* ============================================================================
 * Task Processing and Response Routing
 * ============================================================================ */

/**
 * @brief Update command manager task statistics
 * @note Updates stack usage and memory statistics from task info
 */
static void cmd_update_task_stats(void)
{
    (void)task_stats_update_periodic(g_command_manager.task_ref,
                                     "Command",
                                     MODULE_ID_CMD,
                                     &g_command_manager.task_stats,
                                     0);
}


/**
 * @brief Process a single command using state machine
 * @param request - Module message containing the command
 * @param response_buffer - Response buffer to fill
 * @param buffer_size - Size of response buffer
 * @return Result - Processing result
 */
static Result cmd_process_command(const ModuleMessage* request, char* response_buffer, size_t buffer_size)
{
    if (!request || !response_buffer || buffer_size == 0) {
        return RESULT_ERROR;
    }
    
    /* Validate source module */
    if (request->source_module == 0 || request->source_module >= MODULE_ID_COUNT) {
        return RESULT_ERROR;
    }
    
    /* Initialize response buffer */
    response_buffer[0] = '\0';
    
    /* Command processing context */
    struct {
        char cmd[64];
        char args[256];
        cmd_enum_t cmd_enum;
        const cmd_handler_entry_t* entry;
    } cmd_ctx = {0};
    
    Result state_result;
    Result final_result = RESULT_ERROR;
    
    memset(&cmd_ctx, 0, sizeof(cmd_ctx));
    g_command_manager.stats.total_received++;
    
    const char *payload = module_message_payload_ptr(request);
    if (!payload || module_message_payload_len(request) == 0) {
        snprintf(response_buffer, buffer_size, "ERROR: Empty command payload");
        cmd_state_handle_update_stats(RESULT_ERROR);
        return RESULT_ERROR;
    }
    
    /* Waterfall processing - all steps must be processed, return on error */
    
    /* Step 1: Check STM prefix */
    state_result = cmd_state_handle_check_stm_prefix(request->source_module, payload, request->address, 
                                                      response_buffer, buffer_size);
    if (state_result == RESULT_SUCCESS) {
        /* STM: command routed to UART, complete */
        cmd_state_handle_update_stats(RESULT_SUCCESS);
        return RESULT_SUCCESS;
    } else if (state_result == RESULT_ERROR) {
        /* STM: command failed, complete with error */
        cmd_state_handle_update_stats(RESULT_ERROR);
        return RESULT_ERROR;
    }
    /* Not STM: prefix, continue to parse */
    
    /* Step 2: Parse command */
    state_result = cmd_state_handle_parse_command(payload, cmd_ctx.cmd, cmd_ctx.args, 
                                                  response_buffer, buffer_size);
    if (state_result != RESULT_SUCCESS) {
        cmd_state_handle_update_stats(RESULT_INVALID_PARAM);
        return state_result;
    }
    
    /* Step 3: Find command enum */
    state_result = cmd_state_handle_find_command(cmd_ctx.cmd, &cmd_ctx.cmd_enum, response_buffer, buffer_size);
    if (state_result != RESULT_SUCCESS) {
        cmd_state_handle_update_stats(RESULT_INVALID_PARAM);
        return state_result;
    }
    
    /* Step 4: Get command entry */
    state_result = cmd_state_handle_get_entry(cmd_ctx.cmd_enum, &cmd_ctx.entry, response_buffer, buffer_size);
    if (state_result != RESULT_SUCCESS) {
        cmd_state_handle_update_stats(RESULT_ERROR);
        return state_result;
    }
    
    /* Step 5: Authenticate */
    state_result = cmd_state_handle_authenticate(cmd_ctx.cmd, cmd_ctx.args, cmd_ctx.entry, 
                                                 response_buffer, buffer_size);
    if (state_result != RESULT_SUCCESS) {
        cmd_state_handle_update_stats(RESULT_ERROR);
        return state_result;
    }
    
    /* Step 5.5: PING-STM is asynchronous and needs the requester (source+address),
     * which the generic execute handler does not receive. Arm the UART ping test here
     * and reply with an immediate ack; the OK/FAIL verdict follows from the UART task. */
    if (cmd_ctx.cmd_enum == CMD_PING_STM) {
        #ifdef UART_UNAVAILABLE
        LOG_INFO("PING-STM received from module %u, address %s: UART unavailable",
                     (unsigned)request->source_module, request->address);
        snprintf(response_buffer, buffer_size, "ERROR: PING-STM unavailable (UART module not present)");
        #else
        Result pr = uart_manager_start_ping_test(request->source_module, request->address);
        if (pr == RESULT_SUCCESS) {
            snprintf(response_buffer, buffer_size, "PING-STM started (result to follow, up to 15s)");
        } else if (pr == RESULT_BUSY) {
            snprintf(response_buffer, buffer_size, "PING-STM busy, try again shortly");
        } else {
            snprintf(response_buffer, buffer_size, "ERROR: PING-STM could not start");
        }
        #endif /* UART_UNAVAILABLE */
        cmd_state_handle_update_stats(RESULT_SUCCESS);
        return RESULT_SUCCESS;
    }

    /* Step 6: Execute command - pass args directly from parsed string */
    final_result = cmd_state_handle_execute(cmd_ctx.entry, cmd_ctx.cmd_enum, cmd_ctx.args,
                                           response_buffer, buffer_size);
    
    /* Step 7: Update stats */
    cmd_state_handle_update_stats(final_result);
    
    return final_result;
}

/**
 * @brief Command manager task entry - processes messages from queue
 * @param arg - Task argument (unused)
 */
static void cmd_task_entry(void *arg)
{
    (void)arg;
    
    while (1) {
        module_manager_update_uptime(MODULE_ID_CMD);
        cmd_update_task_stats();
        
        if (!g_command_manager.module || !g_command_manager.module->config.msg_q) {
            utils_sleep_ms(g_command_manager.task_interval_ms);
            continue;
        }
        
        /* Step 1: Get command from queue */
        ModuleMessage request;
        memset(&request, 0, sizeof(request));
        Result pop_result = queue_pop(g_command_manager.module->config.msg_q,
                                      &g_command_manager.module->config.msg_q_config,
                                      &request,
                                      1,
                                      NULL);
        
        /* If no command received, continue to delay */
        if (pop_result != RESULT_SUCCESS) {
            utils_sleep_ms(g_command_manager.task_interval_ms);
            continue;
        }
        
        /* Validate request - if not correct, continue */
        const char *payload = module_message_payload_ptr(&request);
        if (request.destination_module != MODULE_ID_CMD || 
            !payload || module_message_payload_len(&request) == 0) {
            if (request.use_dynamic_buffer && request.dynamic_buffer) {
                free(request.dynamic_buffer);
            }
            utils_sleep_ms(g_command_manager.task_interval_ms);
            continue;
        }
        
        /* All checks passed - Step 2: Process command */
        char response_buffer[CMD_HANDLER_MAX_RESPONSE_LENGTH] = {0};
        cmd_process_command(&request, response_buffer, sizeof(response_buffer));
        if (request.use_dynamic_buffer && request.dynamic_buffer) {
            free(request.dynamic_buffer);
            request.dynamic_buffer = NULL;
        }
        
        /* Step 3: Route response (always route, even if empty, to confirm command was processed) */
        utils_route_response_to_module(request.source_module, request.address, response_buffer, MODULE_ID_CMD);
        
        /* Step 4: Delay and loop back to step 1 */
        utils_sleep_ms(g_command_manager.task_interval_ms);
    }
}

Result command_manager_deinit(void)
{
    if (!g_command_manager.initialized)
        return RESULT_SUCCESS;

    if (g_command_manager.task_ref) {
        wm_sdk_task_delete(g_command_manager.task_ref);
        g_command_manager.task_ref = NULL;
    }

    if (g_command_manager.module && g_command_manager.module->config.msg_q) {
        queue_manager_destroy(g_command_manager.module->config.msg_q,
                              &g_command_manager.module->config.msg_q_config);
        g_command_manager.module->config.msg_q = NULL;
    }

    cmd_set_runtime_defaults();
    memset(&g_command_manager.stats, 0, sizeof(g_command_manager.stats));
    memset(&g_command_manager.task_stats, 0, sizeof(g_command_manager.task_stats));
    g_command_manager.module = NULL;
    g_command_manager.initialized = FALSE;
    LOG_INFO("Command manager stopped");
    return RESULT_SUCCESS;
}

/* ============================================================================
 * Init (last — bottom-up entry)
 * ============================================================================ */

Result command_manager_init(void)
{
    if (g_command_manager.initialized)
        return RESULT_SUCCESS;

    g_command_manager.module = g_modules[MODULE_ID_CMD];
    if (!g_command_manager.module) {
        LOG_ERROR("Command manager module not found");
        return RESULT_ERROR;
    }

    const ModuleConfig *module_config = module_manager_get_config(MODULE_ID_CMD);
    if (module_config && module_config->config_ptr)
        g_command_manager.config = (CommandConfig *)module_config->config_ptr;
    else {
        command_config_get_defaults(&g_command_config);
        g_command_manager.config = &g_command_config;
    }

    cmd_set_runtime_defaults();

    if (queue_manager_create(&g_command_manager.module->config.msg_q_config,
                             &g_command_manager.module->config.msg_q) != RESULT_SUCCESS) {
        LOG_ERROR("Command queue create failed");
        return RESULT_ERROR;
    }

    memset(&g_command_manager.stats, 0, sizeof(g_command_manager.stats));
    memset(&g_command_manager.task_stats, 0, sizeof(g_command_manager.task_stats));

    g_command_manager.task_ref = wm_sdk_task_create(cmd_task_entry,
                                                 NULL,
                                                 "cmdTask",
                                                 g_command_manager.task_stack,
                                                 sizeof(g_command_manager.task_stack),
                                                 5);
    if (!g_command_manager.task_ref) {
        LOG_ERROR("Command task create failed");
        queue_manager_destroy(g_command_manager.module->config.msg_q,
                              &g_command_manager.module->config.msg_q_config);
        g_command_manager.module->config.msg_q = NULL;
        return RESULT_ERROR;
    }

    g_command_manager.initialized = TRUE;
    LOG_INFO("Command manager ready");
    return RESULT_SUCCESS;
}
