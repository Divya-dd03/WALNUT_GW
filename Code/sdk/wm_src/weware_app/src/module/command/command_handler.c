/**
 * @file command_handler.c
 * @brief Command handler implementations for weware platform
 * 
 * This file contains command parsing functions and state handler functions
 * for the command processing state machine.
 */

#include "module/command/command_handler.h"
#include "module/command/command_manager.h"
#include "module/command/command_config.h"
#include "weware_version.h"
#include "module/module_manager.h"
#ifndef UART_UNAVAILABLE
#include "module/uart/uart_manager.h"
#endif /* UART_UNAVAILABLE */
#include "module/gps/gps_config.h"
#include "module/gps/gps_manager.h"
#include "system/system_manager.h"
#include "system/ota/ota_manager.h"
#include "system/reset/post_boot_handler.h"
#include "module/gps/gps_ops.h"
#include "module/tcp/tcp.h"
#include "module/tcp/tcp_ops.h"
#include "module/network/network.h"
#include "module/sim/sim.h"
#include "system/device_utils.h"
#include "system/time_utils.h"
#include "config/config.h"
#include "system/storage/file_system.h"
#include "system/storage/flash_paths.h"
#ifndef DIGOUT_UNAVAILABLE
#include "system/gpio/digout_manager.h"
#endif /* DIGOUT_UNAVAILABLE */
#include "common/utils.h"
#include "common/event_manager.h"

#include "sdk_platform.h"

#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "ctype.h"
#include "strings.h"
#include <stddef.h>

#include "sdk_log.h"

/*---------------------------------------------------------------
 * Log Configuration
 *--------------------------------------------------------------*/
#define LOG_TAG "CMD_HANDLER"
/** INFO for command audit (e.g. DELETE-FOLDER, reboot); tune down via LOG_GLOBAL_LEVEL if needed */
#define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

/* ============================================================================
 * Private Constants and Macros
 * ============================================================================ */
#define MOD_PREFIX                "MOD:"
#define MOD_PREFIX_LEN           (sizeof(MOD_PREFIX) - 1)
#define STM_PREFIX                "STM:"
#define STM_PREFIX_LEN           (sizeof(STM_PREFIX) - 1)

/* ============================================================================
 * External Declarations
 * ============================================================================ */
extern const cmd_handler_entry_t g_cmd_table[];
extern const UINT32 g_cmd_table_size;
extern CommandConfig g_command_config;
extern command_manager_runtime_t g_command_manager;

/* ============================================================================
 * Command Parsing Functions
 * ============================================================================ */

/**
 * @brief Parse command text into command name and arguments
 * @param text - Command text (e.g., "MOD:SET-GPS-CONFIG i-on:10,i-off:10")
 * @param cmd - Output buffer for command name
 * @param args - Output buffer for arguments
 * @return BOOL - TRUE if parsing successful, FALSE otherwise
 */
BOOL cmd_parse_command(const char* text, char* cmd, char* args)
{
    if (!text || !cmd || !args) {
        return FALSE;
    }
    
    /* Use static buffer to avoid 512-byte stack allocation */
    /* Note: This function may be called from multiple threads, but command parsing
       is typically serialized, so static buffer is acceptable */
    static char temp[512];
    size_t text_len = strlen(text);
    if (text_len >= sizeof(temp)) {
        text_len = sizeof(temp) - 1;
    }
    utils_strncpy_safe(temp, text, sizeof(temp));
    
    /* Trim whitespace */
    char* start = utils_trim_whitespace(temp);
    
    /* Check prefix - must start with "MOD:" */
    if (strncasecmp(start, MOD_PREFIX, MOD_PREFIX_LEN) != 0) {
        return FALSE;
    }
    
    /* Extract command and arguments */
    char* cmd_start = start + MOD_PREFIX_LEN;
    while (isspace((unsigned char)*cmd_start)) cmd_start++;
    
    char* space = strchr(cmd_start, ' ');
    if (space) {
        *space = '\0';
        /* Use safe strncpy with bounds checking to prevent buffer overflow */
        /* cmd buffer is typically 64 bytes, args is typically 256 bytes */
        utils_strncpy_safe(cmd, cmd_start, 64);
        utils_strncpy_safe(args, space + 1, 256);
    } else {
        utils_strncpy_safe(cmd, cmd_start, 64);
        args[0] = '\0';
    }
    
    /* Convert command to uppercase */
    for (char* p = cmd; *p; p++) {
        *p = (char)toupper((unsigned char)*p);
    }
    
    return strlen(cmd) > 0;
}

/**
 * @brief Find command enum from command name
 * @param name - Command name (e.g., "STATUS")
 * @return cmd_enum_t - Command enum or CMD_UNKNOWN if not found
 */
cmd_enum_t cmd_find_command_enum(const char* name)
{
    if (!name) {
        return CMD_UNKNOWN;
    }
    
    for (UINT32 i = 0; i < g_cmd_table_size; i++) {
        if (strcasecmp(g_cmd_table[i].name, name) == 0) {
            return g_cmd_table[i].cmd_enum;
        }
    }
    
    return CMD_UNKNOWN;
}

/**
 * @brief Get command handler entry from command enum
 * @param cmd_enum - Command enum
 * @return const cmd_handler_entry_t* - Pointer to entry or NULL if not found
 */
const cmd_handler_entry_t* cmd_get_command_entry(cmd_enum_t cmd_enum)
{
    if (cmd_enum == CMD_UNKNOWN || cmd_enum >= CMD_MAX) {
        return NULL;
    }
    
    for (UINT32 i = 0; i < g_cmd_table_size; i++) {
        if (g_cmd_table[i].cmd_enum == cmd_enum) {
            return &g_cmd_table[i];
        }
    }
    
    return NULL;
}

/**
 * @brief Authenticate command execution
 * @param cmd - Command name
 * @param args - Command arguments (should contain password for protected commands)
 * @return BOOL - TRUE if authenticated, FALSE otherwise
 */
BOOL cmd_authenticate(const char* cmd, const char* args)
{
    CommandConfig *cfg = command_config_get_storage();
    if (!cfg || !args || strlen(args) == 0) {
        return FALSE;
    }
    
    char args_copy[256];
    utils_strncpy_safe(args_copy, args, sizeof(args_copy));
    
    char* token = strtok(args_copy, " \t");
    if (token && strcmp(token, cfg->auth_password) == 0) {
        return TRUE;
    }
    
    return FALSE;
}

/* ============================================================================
 * Generic Command Handler
 * ============================================================================ */

/**
 * @brief Generic command handler - calls module functions with common validation
 * @param cmd - Command structure
 * @param response - Response structure
 * @return Result - Result
 */
Result cmd_handler_generic(cmd_enum_t cmd_enum, const char* args_string, char* response_buffer, size_t buffer_size)
{
    if (!response_buffer || buffer_size == 0)
    {
        return RESULT_ERROR;
    }
    
    /* Initialize response buffer */
    response_buffer[0] = '\0';
    
    /* Get command entry to access module function */
    const cmd_handler_entry_t* entry = cmd_get_command_entry(cmd_enum);
    if (!entry || !entry->module_func)
    {
        snprintf(response_buffer, buffer_size, 
                "ERROR: No module function mapped for command");
        return RESULT_ERROR;
    }
    
    Result module_result = RESULT_ERROR;
    
    /* Determine if this is a SET or GET command from the command name */
    BOOL is_set_command = (strncmp(entry->name, "SET-", 4) == 0);
    
    if (is_set_command)
    {
        /* SET command - use args_string directly */
        if (!args_string)
        {
            snprintf(response_buffer, buffer_size, 
                    "ERROR: Missing arguments for SET command");
            return RESULT_ERROR;
        }
        
        /* Call module SET function - cast to appropriate function signature */
        typedef Result (*set_func_t)(const char*);
        set_func_t set_func = (set_func_t)entry->module_func;
        module_result = set_func(args_string);
        
        if (module_result == RESULT_SUCCESS)
        {
            snprintf(response_buffer, buffer_size, "OK");
        }
        else
        {
            snprintf(response_buffer, buffer_size, 
                    "ERROR: Module function returned error");
        }
    }
    else
    {
        /* GET command - call module GET function with response buffer */
        typedef Result (*get_func_t)(char*, size_t);
        get_func_t get_func = (get_func_t)entry->module_func;
        module_result = get_func(response_buffer, buffer_size);
        
        if (module_result != RESULT_SUCCESS)
        {
            snprintf(response_buffer, buffer_size, 
                    "ERROR: Module function returned error");
        }
    }
    
    /* Detect missing NUL within buffer (strlen would be undefined past end) */
    {
        size_t n;
        for (n = 0; n < buffer_size && response_buffer[n] != '\0'; n++) { }
        if (n >= buffer_size) {
            sdk_log_warning("Command response too long or unterminated");
            return RESULT_ERROR;
        }
    }
    
    return module_result;
}

/* ============================================================================
 * Command Processing State Handlers
 * ============================================================================ */

/**
 * @brief Handle CHECK_STM_PREFIX state - check if command is STM: prefix and route to UART
 * @param source_type - Source type
 * @param command_text - Command text
 * @param source_address - Source address
 * @param response - Response structure
 * @return Result - RESULT_SUCCESS if routed, RESULT_ERROR if failed, RESULT_BUSY if not STM: prefix
 */
Result cmd_state_handle_check_stm_prefix(ModuleId source_module, const char* command_text,
                                        const char* source_address, char* response_buffer, size_t buffer_size)
{
    if (!command_text) {
        return RESULT_BUSY;
    }

    /* Check if command starts with "STM:" - route to UART */
    if (strncasecmp(command_text, STM_PREFIX, STM_PREFIX_LEN) == 0) {
#ifdef UART_UNAVAILABLE
        (void)source_module;
        (void)source_address;
        if (response_buffer && buffer_size > 0) {
            snprintf(response_buffer, buffer_size,
                     "ERROR: STM commands unavailable (UART module not present)");
        }
        return RESULT_ERROR;
#else
        const ModuleConfig *uart_config = module_manager_get_config(MODULE_ID_UART);
        if (uart_config && uart_config->enabled && module_manager_is_initialized(MODULE_ID_UART)) {
            ModuleMessage uart_req = {0};
            uart_req.source_module = source_module;  /* Use module ID directly */
            uart_req.destination_module = MODULE_ID_UART;
            
            if (source_address) {
                utils_strncpy_safe(uart_req.address, source_address, sizeof(uart_req.address));
            }
            
            const char* cmd_without_prefix = command_text + STM_PREFIX_LEN;
            while (isspace((unsigned char)*cmd_without_prefix)) {
                cmd_without_prefix++;
            }
            
            {
                int n = utils_strncpy_safe(uart_req.message, cmd_without_prefix, sizeof(uart_req.message));
                uart_req.data_len = (n >= 0) ? (UINT32)n : 0U;
            }
            
            if (uart_manager_send_request(&uart_req) == RESULT_SUCCESS) {
                return RESULT_SUCCESS;
            }
        }
        return RESULT_ERROR;
#endif /* UART_UNAVAILABLE */
    }
    
    /* Not STM: prefix, continue processing */
    return RESULT_BUSY;
}

/**
 * @brief Handle PARSE_COMMAND state - parse command text
 * @param command_text - Command text
 * @param cmd - Output command name buffer
 * @param args - Output arguments buffer
 * @param response - Response structure
 * @return Result
 */
Result cmd_state_handle_parse_command(const char* command_text, char* cmd, char* args,
                                     char* response_buffer, size_t buffer_size)
{
    if (!command_text || !cmd || !args || !response_buffer || buffer_size == 0) {
        return RESULT_INVALID_PARAM;
    }
    if (!cmd_parse_command(command_text, cmd, args)) {
        g_command_manager.stats.invalid_cmd_count++;
        snprintf(response_buffer, buffer_size, 
                "ERROR: Invalid format. Use 'MOD:<COMMAND>' for commands or 'STM:...' for UART commands.");
        return RESULT_INVALID_PARAM;
    }
    return RESULT_SUCCESS;
}

/**
 * @brief Handle FIND_COMMAND state - find command enum from name
 * @param cmd - Command name
 * @param cmd_enum - Output command enum
 * @param response - Response structure
 * @return Result
 */
Result cmd_state_handle_find_command(const char* cmd, cmd_enum_t* cmd_enum, char* response_buffer, size_t buffer_size)
{
    if (!cmd || !cmd_enum || !response_buffer || buffer_size == 0) {
        return RESULT_INVALID_PARAM;
    }
    *cmd_enum = cmd_find_command_enum(cmd);
    if (*cmd_enum == CMD_UNKNOWN) {
        g_command_manager.stats.invalid_cmd_count++;
        snprintf(response_buffer, buffer_size, 
                "ERROR: Unknown command '%s'", cmd);
        return RESULT_INVALID_PARAM;
    }
    return RESULT_SUCCESS;
}

/**
 * @brief Handle GET_ENTRY state - get command entry from enum
 * @param cmd_enum - Command enum
 * @param entry - Output command entry pointer
 * @param response - Response structure
 * @return Result
 */
Result cmd_state_handle_get_entry(cmd_enum_t cmd_enum, const cmd_handler_entry_t** entry,
                                 char* response_buffer, size_t buffer_size)
{
    if (!entry || !response_buffer || buffer_size == 0) {
        return RESULT_ERROR;
    }
    *entry = cmd_get_command_entry(cmd_enum);
    if (!*entry) {
        g_command_manager.stats.error_count++;
        snprintf(response_buffer, buffer_size, 
                "ERROR: Command handler error");
        return RESULT_ERROR;
    }
    return RESULT_SUCCESS;
}

/**
 * @brief Handle AUTHENTICATE state - authenticate command if required
 * @param cmd - Command name
 * @param args - Command arguments
 * @param entry - Command entry
 * @param response - Response structure
 * @return Result
 */
Result cmd_state_handle_authenticate(const char* cmd, char* args,
                                    const cmd_handler_entry_t* entry, char* response_buffer, size_t buffer_size)
{
    if (!entry || !response_buffer || buffer_size == 0) {
        return RESULT_ERROR;
    }
    /* Check if authentication is enabled in config */
    CommandConfig *cfg = command_config_get_storage();
    if (!cfg) {
        cfg = &g_command_config;
        command_config_get_defaults(cfg);
    }
    
    /* SET-COMMAND-CONFIG and GET-COMMAND-CONFIG always require authentication (contains sensitive password info) */
    BOOL always_require_auth = (cmd && (strcmp(cmd, "SET-COMMAND-CONFIG") == 0 || strcmp(cmd, "GET-COMMAND-CONFIG") == 0));
    
    /* If auth is disabled globally and command doesn't always require auth, skip authentication */
    if (!cfg->auth_enable && !always_require_auth) {
        return RESULT_SUCCESS;
    }
    
    /* If command requires auth (either via entry flag or always_require_auth), authenticate */
    if ((entry->requires_auth || always_require_auth)) {
        if (!cmd_authenticate(cmd, args)) {
            g_command_manager.stats.auth_failures++;
            snprintf(response_buffer, buffer_size, 
                    "ERROR: Authentication required for '%s'", cmd);
            return RESULT_ERROR;
        }
        
        /* Remove password from args after successful authentication */
        if (args && strlen(args) > 0) {
            char args_copy[256];
            utils_strncpy_safe(args_copy, args, sizeof(args_copy));
            
            /* Find password in args */
            char* password_start = args_copy;
            while (*password_start == ' ' || *password_start == '\t') password_start++;
            
            size_t password_len = strlen(cfg->auth_password);
            if (strncmp(password_start, cfg->auth_password, password_len) == 0) {
                /* Password found, find where it ends */
                char* after_password = password_start + password_len;
                while (*after_password == ' ' || *after_password == '\t') after_password++;
                
                /* Copy remaining args back to original buffer */
                if (*after_password != '\0') {
                    utils_strncpy_safe(args, after_password, 256);
                } else {
                    args[0] = '\0';
                }
            }
        }
    }
    return RESULT_SUCCESS;
}

/**
 * @brief Handle EXECUTE state - execute command handler
 * @param entry - Command entry
 * @param cmd_enum - Command enum
 * @param args_string - Command arguments as string (space-separated, will be converted to comma-separated for SET)
 * @param response - Response structure
 * @return Result
 */
Result cmd_state_handle_execute(const cmd_handler_entry_t* entry, cmd_enum_t cmd_enum,
                               const char* args_string, char* response_buffer, size_t buffer_size)
{
    if (!entry || !response_buffer || buffer_size == 0) {
        return RESULT_ERROR;
    }
    
    /* Special handling for REBOOT, GET-DEV-INFO and GET-DEV-STATUS commands */
    if (cmd_enum == CMD_REBOOT || cmd_enum == CMD_HARD_RESET || cmd_enum == CMD_GET_DEV_INFO ||
        cmd_enum == CMD_GET_DEV_STATUS || cmd_enum == CMD_DELETE_FOLDER || cmd_enum == CMD_FACTORY_RESET ||
        cmd_enum == CMD_DIGOUT || cmd_enum == CMD_SET_MIN_CSQ ||
        cmd_enum == CMD_FORCED_OTA || cmd_enum == CMD_GET_OTA_STATUS) {
        typedef Result (*special_func_t)(const char*, char*, size_t);
        special_func_t special_func = (special_func_t)entry->module_func;
        return special_func(args_string, response_buffer, buffer_size);
    }
    
    /* Use generic handler with module function */
    if (entry->module_func) {
        /* For SET commands, convert space-separated args to comma-separated */
        char processed_args[512] = {0};
        BOOL is_set_command = (strncmp(entry->name, "SET-", 4) == 0);
        
        if (is_set_command && args_string && strlen(args_string) > 0) {
            /* Convert space/tab-separated args to comma-separated */
            char args_copy[256];
            utils_strncpy_safe(args_copy, args_string, sizeof(args_copy));
            
            int offset = 0;
            char* token = strtok(args_copy, " \t");
            while (token && offset < (int)sizeof(processed_args) - 1) {
                int len = snprintf(processed_args + offset, sizeof(processed_args) - offset,
                             "%s%s", (offset > 0) ? "," : "", token);
                if (len < 0 || len >= (int)(sizeof(processed_args) - offset)) {
                    break;
                }
                offset += len;
                token = strtok(NULL, " \t");
            }
        }
        
        Result result = cmd_handler_generic(cmd_enum, 
                                           (is_set_command && processed_args[0] != '\0') ? processed_args : args_string,
                                           response_buffer, buffer_size);
        return result;
    }
    
    /* No module function available */
    snprintf(response_buffer, buffer_size, "ERROR: No module function for command");
    return RESULT_ERROR;
}

/**
 * @brief Handle UPDATE_STATS state - update command statistics
 * @param result - Command result
 * @return Result (always success)
 */
Result cmd_state_handle_update_stats(Result result)
{
    g_command_manager.stats.total_processed++;
    if (result != RESULT_SUCCESS) {
        g_command_manager.stats.error_count++;
    }
    return RESULT_SUCCESS;
}

/* ============================================================================
 * DELETE-FOLDER — delete all files in a folder (non-recursive; subdirs skipped)
 * ============================================================================ */

#define CMD_DELETE_FOLDER_PATH_MAX   320

/** Single path segment under @c FLASH_ROOT (no drive, no slashes). */
static BOOL cmd_delete_folder_name_valid(const char *name)
{
    if (!name || name[0] == '\0') {
        return FALSE;
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return FALSE;
    }
    if (strlen(name) >= 64U) {
        return FALSE;
    }
    if (strstr(name, "..") != NULL) {
        return FALSE;
    }
    if (strchr(name, '/') != NULL || strchr(name, '\\') != NULL) {
        return FALSE;
    }
    return TRUE;
}

/** Full path must stay under flash root (see @c flash_paths.h). */
static BOOL cmd_delete_folder_resolved_ok(const char *path)
{
    if (!path || path[0] == '\0') {
        return FALSE;
    }
    if (strstr(path, "..") != NULL) {
        return FALSE;
    }
    size_t rl = strlen(FLASH_ROOT);
    if (strncasecmp(path, FLASH_ROOT, rl) != 0) {
        return FALSE;
    }
    if (path[rl] == '\0') {
        return FALSE;
    }
    return TRUE;
}

/**
 * @brief Delete every file in a folder (not subdirectories).
 *
 * Args (after auth): folder name only (e.g. @c queue → @c C:/queue/), same layout as @c flash_paths.h.
 */
Result cmd_delete_folder(const char *args_string, char *response_buffer, size_t buffer_size)
{
    if (!response_buffer || buffer_size == 0) {
        sdk_log_error("DELETE-FOLDER: invalid response buffer (buf=%p size=%u)",
                  (void *)response_buffer, (unsigned)buffer_size);
        return RESULT_ERROR;
    }
    response_buffer[0] = '\0';

    if (!args_string) {
        sdk_log_warning("DELETE-FOLDER: missing args after auth");
        snprintf(response_buffer, buffer_size, "ERROR: Missing folder name");
        return RESULT_INVALID_PARAM;
    }

    char work[256];
    utils_strncpy_safe(work, args_string, sizeof(work));
    char *args_trim = utils_trim_whitespace(work);
    if (args_trim[0] == '\0') {
        sdk_log_warning("DELETE-FOLDER: empty args string");
        snprintf(response_buffer, buffer_size, "ERROR: Missing folder name");
        return RESULT_INVALID_PARAM;
    }

    char extra[8];
    char name[80];
    if (sscanf(args_trim, "%79s %7s", name, extra) != 1) {
        sdk_log_warning("DELETE-FOLDER: expected one folder name, args='%s'", args_trim);
        snprintf(response_buffer, buffer_size,
                 "ERROR: Pass one folder name only");
        return RESULT_INVALID_PARAM;
    }

    if (!cmd_delete_folder_name_valid(name)) {
        sdk_log_warning("DELETE-FOLDER: invalid folder name '%s'", name);
        snprintf(response_buffer, buffer_size,
                 "ERROR: Invalid folder name (no path or ..)");
        return RESULT_INVALID_PARAM;
    }

    char folder[CMD_DELETE_FOLDER_PATH_MAX];
    if (snprintf(folder, sizeof(folder), "%s%s/", FLASH_ROOT, name) >= (int)sizeof(folder)) {
        sdk_log_warning("DELETE-FOLDER: resolved path truncated (name='%s' root='%s')",
                 name, FLASH_ROOT);
        snprintf(response_buffer, buffer_size, "ERROR: Path too long");
        return RESULT_INVALID_PARAM;
    }

    if (!cmd_delete_folder_resolved_ok(folder)) {
        sdk_log_warning("DELETE-FOLDER: resolved path not under flash root (path='%s')", folder);
        snprintf(response_buffer, buffer_size, "ERROR: Invalid resolved path");
        return RESULT_INVALID_PARAM;
    }

    sdk_log_info("Deleting folder %s", name);

    UINT32 total_deleted = 0;
    UINT32 total_failed  = 0;
    Result pr = file_system_delete_all_files_in_directory(folder, &total_deleted, &total_failed);

    if (pr == RESULT_NOT_SUPPORTED) {
        sdk_log_error("DELETE-FOLDER: list_dir not supported path='%s'", folder);
        snprintf(response_buffer, buffer_size,
                 "ERROR: list_dir not supported on this platform");
        return RESULT_ERROR;
    }
    if (pr != RESULT_SUCCESS) {
        sdk_log_error("DELETE-FOLDER: purge failed path='%s' result=%d", folder, (int)pr);
        snprintf(response_buffer, buffer_size,
                 "ERROR: delete folder failed for '%s'", folder);
        return RESULT_ERROR;
    }

    if (total_failed > 0U) {
        sdk_log_info("Folder %s purge done: %u deleted, %u failed",
             name, (unsigned)total_deleted, (unsigned)total_failed);
        snprintf(response_buffer, buffer_size, "OK: deleted %u fail %u folder=%s",
                 (unsigned)total_deleted, (unsigned)total_failed, name);
        return RESULT_SUCCESS;
    }
    sdk_log_info("Folder %s purge done: %u files deleted", name, (unsigned)total_deleted);
    snprintf(response_buffer, buffer_size, "OK: deleted %u files folder=%s",
             (unsigned)total_deleted, name);
    return RESULT_SUCCESS;
}

/* ============================================================================
 * FACTORY-RESET — wipe C:/ user area then soft reboot
 * ============================================================================ */

/**
 * @brief Clear files in @c C:/config, @c queue, @c fota, @c preboot (see @c file_system_wipe_user_flash_c), then soft reboot.
 *
 * Args after auth are ignored.
 */
Result cmd_factory_reset(const char *args_string, char *response_buffer, size_t buffer_size)
{
    (void)args_string;

    if (!response_buffer || buffer_size == 0) {
        sdk_log_error("FACTORY-RESET: invalid response buffer");
        return RESULT_ERROR;
    }
    response_buffer[0] = '\0';

    sdk_log_warning("Factory reset started");

    Result w = file_system_wipe_user_flash_c();
    if (w == RESULT_NOT_SUPPORTED) {
        sdk_log_error("FACTORY-RESET: wipe not supported on this platform");
        snprintf(response_buffer, buffer_size,
                 "ERROR: wipe not supported (no reboot)");
        return RESULT_ERROR;
    }
    if (w != RESULT_SUCCESS) {
        sdk_log_error("FACTORY-RESET: wipe failed result=%d (no reboot)", (int)w);
        snprintf(response_buffer, buffer_size,
                 "ERROR: wipe failed (no reboot)");
        return RESULT_ERROR;
    }

    snprintf(response_buffer, buffer_size, "OK: factory folders cleared; rebooting");
    sdk_log_warning("Factory reset complete, rebooting");
    (void)event_manager_broadcast(EVENT_RESET_SOFT, "FACTORY-RESET", NULL, 0);
    return RESULT_SUCCESS;
}

/* ============================================================================
 * MOD Reboot Command Handler
 * ============================================================================ */

/**
 * @brief Handle MOD reboot command - broadcasts soft reset event
 * @param args_string - Command arguments (unused)
 * @param response_buffer - Response buffer to fill
 * @param buffer_size - Size of response buffer
 * @return Result - RESULT_SUCCESS on success
 */
Result cmd_mod_reboot(const char* args_string, char* response_buffer, size_t buffer_size)
{
    (void)args_string;  /* Unused */
    
    if (!response_buffer || buffer_size == 0)
    {
        return RESULT_ERROR;
    }
    
    sdk_log_info("Soft reboot requested");
    
    /* Broadcast soft reset event */
    event_manager_broadcast(EVENT_RESET_SOFT, "Command Manager", NULL, 0);
    
    /* Set response message */
    snprintf(response_buffer, buffer_size, "OK: Soft reset event broadcast");
    
    return RESULT_SUCCESS;
}

/* ============================================================================
 * HARD-RESET — immediate SoC reset (bypasses deferred path, deinit, pre-boot save)
 * ============================================================================ */

/**
 * @brief Request immediate @c SDK_SYSTEM_RESET(); no pre-boot save, no @c module_manager_deinit.
 * @note Does not format a command reply; buffer is cleared only (link will drop on reset).
 */
Result cmd_hard_reset(const char *args_string, char *response_buffer, size_t buffer_size)
{
    (void)args_string;

    if (response_buffer && buffer_size > 0) {
        response_buffer[0] = '\0';
    }

    sdk_log_warning("Hard reset requested");
    SDK_SYSTEM_RESET();
    return RESULT_ERROR;
}

/* ============================================================================
 * FORCED-OTA — trigger an OTA cycle bypassing prerequisite + cooldown gates
 * ============================================================================ */

/**
 * @brief Arm a one-shot forced OTA cycle (password-protected).
 * @note Bypasses network/GPS/motion/cooldown gates. The server version check still
 *       runs, so an actual update only happens if the server has a different image.
 *       Walks SIMCOM -> ST -> peripheral on the next periodic OTA tick.
 */
Result cmd_force_ota(const char *args_string, char *response_buffer, size_t buffer_size)
{
    (void)args_string;  /* password already consumed during authentication */

    if (!response_buffer || buffer_size == 0)
    {
        return RESULT_ERROR;
    }

    sdk_log_warning("FORCED-OTA requested — bypassing OTA prerequisite + cooldown gates");

    if (ota_manager_force_update() == RESULT_BUSY)
    {
        snprintf(response_buffer, buffer_size,
                 "OTA already in progress; not restarted");
        return RESULT_SUCCESS;
    }

    snprintf(response_buffer, buffer_size,
             "OK: FORCED-OTA armed (SIMCOM,STM,peripheral; gates bypassed)");
    return RESULT_SUCCESS;
}

/* ============================================================================
 * GET-OTA-STATUS — report last OTA outcome/reason for field debug (no logs)
 * ============================================================================ */

/**
 * @brief Return the last OTA outcome (STM + SIMCOM + learned ST fw version).
 * @note Read-only; not authenticated. Reply fits within the SMS limit.
 */
Result cmd_get_ota_status(const char *args_string, char *response_buffer, size_t buffer_size)
{
    (void)args_string;

    if (!response_buffer || buffer_size == 0)
    {
        return RESULT_ERROR;
    }

    #ifndef UART_UNAVAILABLE
    ota_manager_get_status_string(response_buffer, (int)buffer_size);
    #endif /* UART_UNAVAILABLE */
    return RESULT_SUCCESS;
}

/* ============================================================================
 * GET-DEV-INFO / GET-DEV-STATUS response helpers
 * ============================================================================ */

/** Compact Google Maps link (fits 160-byte command response with other fields). */
static void cmd_format_google_maps_url(char *buf, size_t buf_size, double lat, double lon, BOOL valid)
{
    if (!buf || buf_size == 0)
        return;
    if (!valid) {
        utils_strncpy_safe(buf, "https://www.google.com/maps?q=0,0", buf_size);
        return;
    }
    snprintf(buf, buf_size, "https://www.google.com/maps?q=%.5f,%.5f", lat, lon);
    buf[buf_size - 1] = '\0';
}

static const char *cmd_tcp_state_short(TcpState state)
{
    switch (state) {
        case TCP_STATE_INIT: return "I";
        case TCP_STATE_WAIT_NETWORK: return "WN";
        case TCP_STATE_SOCKET_CREATING: return "SC";
        case TCP_STATE_SOCKET_CREATED: return "SD";
        case TCP_STATE_CONNECTING: return "CG";
        case TCP_STATE_CONNECTED: return "CN";
        case TCP_STATE_SENDING_LOGIN: return "SL";
        case TCP_STATE_WAIT_ACK_LOGIN: return "WL";
        case TCP_STATE_ACK_RECEIVED: return "AR";
        case TCP_STATE_SENDING_DATA: return "TX";
        case TCP_STATE_WAIT_ACK_DATA: return "WA";
        case TCP_STATE_RECEIVING: return "RX";
        case TCP_STATE_CLOSED: return "CL";
        case TCP_STATE_RECONNECT_DELAY: return "RD";
        case TCP_STATE_ERROR: return "ER";
        default: return "?";
    }
}

static const char *cmd_network_state_short(NetworkState state)
{
    switch (state) {
        case NETWORK_STATE_INIT: return "I";
        case NETWORK_STATE_CHECK_SIM: return "CS";
        case NETWORK_STATE_SIM_INSERT: return "SI";
        case NETWORK_STATE_SIM_REMOVE: return "SR";
        case NETWORK_STATE_CHECK_CTZU: return "CZ";
        case NETWORK_STATE_SET_CTZU: return "SZ";
        case NETWORK_STATE_CHECK_REGISTRATION: return "CR";
        case NETWORK_STATE_CHECK_GPRS_REGISTRATION: return "GR";
        case NETWORK_STATE_CHECK_LTE_ATTACHMENT: return "LT";
        case NETWORK_STATE_SETUP_PDP: return "SP";
        case NETWORK_STATE_ACTIVATE_PDP: return "AP";
        case NETWORK_STATE_GET_IP: return "IP";
        case NETWORK_STATE_CONNECTED: return "CN";
        case NETWORK_STATE_DISCONNECTED: return "DC";
        case NETWORK_STATE_ERROR: return "ER";
        case NETWORK_STATE_RESTART_CFUN: return "RC";
        default: return "?";
    }
}

/* ============================================================================
 * Get Device Info Command Handler
 * ============================================================================ */

/**
 * @brief Handle GET-DEV-INFO command - returns device information
 * @param args_string - Command arguments (unused)
 * @param response_buffer - Response buffer to fill
 * @param buffer_size - Size of response buffer
 * @return Result - RESULT_SUCCESS on success
 */
Result cmd_get_dev_info(const char* args_string, char* response_buffer, size_t buffer_size)
{
    (void)args_string;  /* Unused */
    
    if (!response_buffer || buffer_size == 0)
    {
        return RESULT_ERROR;
    }
    
    /* Initialize response buffer */
    response_buffer[0] = '\0';
    
    /* Get IMEI */
    char imei[32] = "N/A";
    if (device_utils_get_imei(imei))
    {
        imei[15] = '\0';  /* Ensure null-terminated */
    }

    /* Get SIM ICCID */
    char iccid[32] = "N/A";
    if (weware_sim_get_sim_number(iccid) == RESULT_SUCCESS)
    {
        iccid[31] = '\0';  /* Ensure null-terminated */
    }
    
    /* Get firmware and hardware versions */
    const char *fw_version = FIRMWARE_VERSION;  /* Default from version.h */
    const char *hw_version = HARDWARE_VERSION;   /* Default from version.h */

    /* Get reboot module and reason from cached reset state (short form) */
    ResetType reset_type = RESET_TYPE_NONE;
    UINT32 total_resets = 0;
    char reboot_module[33] = {0};
    post_boot_handler_load_reset_state(&reset_type, NULL, reboot_module, &total_resets);
    
    /* Short form: S=Soft, H=Hard, C=CFUN, N=None */
    char reset_type_short = 'N';
    switch (reset_type) {
        case RESET_TYPE_SOFT: reset_type_short = 'S'; break;
        case RESET_TYPE_HARD: reset_type_short = 'H'; break;
        case RESET_TYPE_CFUN: reset_type_short = 'C'; break;
        default: reset_type_short = 'N'; break;
    }
    
    /* Short module name (first 4 chars or "N/A") */
    char module_short[5] = "N/A";
    if (reboot_module[0] != '\0') {
        utils_strncpy_safe(module_short, reboot_module, sizeof(module_short));
        module_short[4] = '\0';  /* Ensure null-terminated */
    }
    
    const char *reset_reason_str = post_boot_handler_get_soc_reset_reason_string();
    if (!reset_reason_str || reset_reason_str[0] == '\0')
        reset_reason_str = "?";

    const PowerInfo *power_info = system_manager_get_power_info();
    float ev_v = 0.0f;
    float iw_v = 0.0f;
    int bp_pct = 0;
    if (power_info != NULL) {
        ev_v = power_info->external_voltage;
        iw_v = power_info->input_wire_voltage;
        bp_pct = (int)power_info->battery_percentage;
    }

    char reset_reason_short[9];
    utils_strncpy_safe(reset_reason_short, reset_reason_str, sizeof(reset_reason_short));

    /* STM firmware version (learned from the STM device-info); "?" if not known yet.
     * device_utils_get_st_firmware_version() writes up to ST_FW_VERSION_BUFFER_SIZE (32). */
    char stf_ver[32] = {0};
    if (!device_utils_get_st_firmware_version(stf_ver))
        utils_strncpy_safe(stf_ver, "?", sizeof(stf_ver));

    /* GET-DEV-INFO (short keys, device fields only). stf capped at 10 chars to stay
     * within the ~160-char SMS limit. */
    int len = snprintf(response_buffer, buffer_size,
                      "i:%s,c:%s,fw:%s,hw:%s,rt:%c,rm:%s,rr:%s,ev:%.1f,iw:%.1f,bp:%d,do:%d,sr:%lu,stf:%.10s",
                      imei,
                      iccid,
                      fw_version,
                      hw_version,
                      reset_type_short,
                      module_short,
                      reset_reason_short,
                      (double)ev_v,
                      (double)iw_v,
                      bp_pct,
#ifdef DIGOUT_UNAVAILABLE
                      0, /* digout manager not ported */
#else
                      digout_manager_get_on() ? 1 : 0,
#endif /* DIGOUT_UNAVAILABLE */
                      (unsigned long)total_resets,
                      stf_ver);
    
    if (len < 0 || (size_t)len >= buffer_size)
    {
        sdk_log_error("Device info response too long");
        return RESULT_ERROR;
    }
    
    return RESULT_SUCCESS;
}

/* ============================================================================
 * Get Device status Command Handler
 * ============================================================================ */

/**
 * @brief Handle GET-DEV-STATUS command - returns device status information 
 * @param args_string 
 * @param response_buffer 
 * @param buffer_size 
 * @return Result RESULT_SUCCESS on success
 */
Result cmd_get_dev_status(const char* args_string, char* response_buffer, size_t buffer_size)
{
    (void)args_string;  /* Unused */
    
    if (!response_buffer || buffer_size == 0)
    {
        return RESULT_ERROR;
    }
    
    /* Initialize response buffer */
    response_buffer[0] = '\0';

    extern gps_manager_runtime_t g_gps;

    const GpsPacket *gps = &g_gps.current_gps_data;
    const GpsPacket *last_valid = &g_gps.last_valid_gps_data;
    BOOL pos_ok = gps->fix_valid_real ? TRUE : FALSE;
    BOOL last_pos_ok = (last_valid->utc_time != 0 &&
                        gps_validate_coordinates(last_valid->latitude_deg,
                                                 last_valid->longitude_deg));
    char map_url[56];

    cmd_format_google_maps_url(map_url, sizeof(map_url),
                               last_valid->latitude_deg, last_valid->longitude_deg,
                               last_pos_ok);

    UINT8 gsm_strength = 99u;
    NetworkRadioSnapshot radio;
    if (weware_network_get_radio_snapshot(&radio) && radio.valid)
        gsm_strength = (UINT8)radio.signal_strength;

    UINT32 uptime_sec = utils_get_uptime_seconds();

    char timestamp_str[16] = "0";
    UINT64 rtc_timestamp = time_utils_get_rtc_timestamp_yyyymmddhhmmss();
    if (rtc_timestamp > 0)
        snprintf(timestamp_str, sizeof(timestamp_str), "%llu", (unsigned long long)rtc_timestamp);
    else
        snprintf(timestamp_str, sizeof(timestamp_str), "%lu", (unsigned long)uptime_sec);

    const PowerInfo *power = system_manager_get_power_info();
    int ign_on = (power && power->ignition_on) ? 1 : 0;
    int mot_on = (power && power->motion_on) ? 1 : 0;

    /* GET-DEV-STATUS (short keys; map from last valid loc; sa from current sample) */
    int len = snprintf(response_buffer, buffer_size,
                      "g:%c,t:%s,n:%s,up:%lu,sa:%u,tm:%s,GS:%u,ign:%d,mot:%d,m:%s",
                      pos_ok ? 'F' : 'N',
                      cmd_tcp_state_short(weware_tcp_get_state()),
                      cmd_network_state_short(weware_network_get_state()),
                      (unsigned long)uptime_sec,
                      (unsigned)gps->sats_in_use,
                      timestamp_str,
                      (unsigned)gsm_strength,
                      ign_on,
                      mot_on,
                      map_url);
    
    if (len < 0 || (size_t)len >= buffer_size)
    {
        sdk_log_error("Device status response too long");
        return RESULT_ERROR;
    }

    return RESULT_SUCCESS;
}

/* ============================================================================
 * DIGOUT — relay GPIO (true = LOW, false = HIGH); persisted in Command config
 * ============================================================================ */

#ifdef DIGOUT_UNAVAILABLE
/**
 * @brief Handle DIGOUT command — digout manager not ported on this platform
 */
Result cmd_digout(const char *args_string, char *response_buffer, size_t buffer_size)
{
    (void)args_string;
    if (!response_buffer || buffer_size == 0)
        return RESULT_ERROR;
    snprintf(response_buffer, buffer_size, "ERROR: DIGOUT unavailable (digout manager not present)");
    return RESULT_ERROR;
}
#else
static BOOL cmd_digout_parse_bool(const char *args_trim, BOOL *out_on)
{
    const char *value = args_trim;

    if (!args_trim || !out_on)
        return FALSE;

    if (strncasecmp(args_trim, "digout:", 7) == 0)
        value = args_trim + 7;

    if (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0) {
        *out_on = TRUE;
        return TRUE;
    }
    if (strcasecmp(value, "false") == 0 || strcmp(value, "0") == 0) {
        *out_on = FALSE;
        return TRUE;
    }
    return FALSE;
}

/**
 * @brief Handle DIGOUT command — args: digout:true|false or true|false
 */
Result cmd_digout(const char *args_string, char *response_buffer, size_t buffer_size)
{
    if (!response_buffer || buffer_size == 0)
        return RESULT_ERROR;

    response_buffer[0] = '\0';

    if (!args_string || args_string[0] == '\0') {
        snprintf(response_buffer, buffer_size, "ERROR: Missing digout argument (true/false)");
        return RESULT_INVALID_PARAM;
    }

    char work[64];
    utils_strncpy_safe(work, args_string, sizeof(work));
    char *args_trim = utils_trim_whitespace(work);
    if (args_trim[0] == '\0') {
        snprintf(response_buffer, buffer_size, "ERROR: Missing digout argument (true/false)");
        return RESULT_INVALID_PARAM;
    }

    BOOL on = FALSE;
    if (!cmd_digout_parse_bool(args_trim, &on)) {
        snprintf(response_buffer, buffer_size, "ERROR: Invalid digout value (use true or false)");
        return RESULT_INVALID_PARAM;
    }

    Result r = digout_manager_set(on);
    if (r == RESULT_INVALID_PARAM) {
        snprintf(response_buffer, buffer_size,
                 "ERROR: GPS fix/coordinates/speed check failed (digout:true requires valid fix and speed <= %.1f km/h)",
                 (double)DIGOUT_MAX_SPEED_KMH);
        return RESULT_INVALID_PARAM;
    }
    if (r != RESULT_SUCCESS) {
        snprintf(response_buffer, buffer_size, "ERROR: Failed to set digout");
        return RESULT_ERROR;
    }

    snprintf(response_buffer, buffer_size, "OK: digout:%s", on ? "true" : "false");
    return RESULT_SUCCESS;
}
#endif /* DIGOUT_UNAVAILABLE */

/**
 * @brief SET-MIN-CSQ — args: 0-31 (0 disables CSQ gate for A-GPS and OTA HTTPS)
 */
Result cmd_set_min_csq(const char *args_string, char *response_buffer, size_t buffer_size)
{
    if (!response_buffer || buffer_size == 0)
        return RESULT_ERROR;

    response_buffer[0] = '\0';

    if (!args_string || args_string[0] == '\0') {
        snprintf(response_buffer, buffer_size, "ERROR: Missing CSQ value (0-31)");
        return RESULT_INVALID_PARAM;
    }

    char work[32];
    utils_strncpy_safe(work, args_string, sizeof(work));
    char *trim = utils_trim_whitespace(work);
    char *end = NULL;
    long v = strtol(trim, &end, 10);
    if (trim[0] == '\0' || end == trim || (end && *end != '\0') || v < 0 || v > 31) {
        snprintf(response_buffer, buffer_size, "ERROR: Invalid CSQ (use 0-31, 0=disabled)");
        return RESULT_INVALID_PARAM;
    }

    CommandConfig *cfg = command_config_get_storage();
    if (!cfg) {
        snprintf(response_buffer, buffer_size, "ERROR: Command config unavailable");
        return RESULT_ERROR;
    }

    cfg->min_csq_threshold = (UINT8)v;
    config_get_current();
    if (config_save_to_file(NULL) != RESULT_SUCCESS) {
        snprintf(response_buffer, buffer_size, "ERROR: Failed to save config");
        return RESULT_ERROR;
    }

    snprintf(response_buffer, buffer_size, "OK: min-csq:%u", (unsigned)cfg->min_csq_threshold);
    return RESULT_SUCCESS;
}
