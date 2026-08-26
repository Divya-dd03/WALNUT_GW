/**
 * @file command_handler.h
 * @brief Command handler function declarations for weware platform
 * 
 * This header contains command parsing functions and state handler function declarations.
 * For command types, structures, and enums, see command_manager.h
 */

#ifndef WEWARE_COMMAND_HANDLER_H
#define WEWARE_COMMAND_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "module/command/command_manager.h"

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
BOOL cmd_parse_command(const char* text, char* cmd, char* args);

/**
 * @brief Find command enum from command name
 * @param name - Command name (e.g., "SET-GPS-CONFIG")
 * @return cmd_enum_t - Command enum or CMD_UNKNOWN if not found
 */
cmd_enum_t cmd_find_command_enum(const char* name);

/**
 * @brief Get command handler entry from command enum
 * @param cmd_enum - Command enum
 * @return const cmd_handler_entry_t* - Pointer to entry or NULL if not found
 */
const cmd_handler_entry_t* cmd_get_command_entry(cmd_enum_t cmd_enum);

/**
 * @brief Authenticate command execution
 * @param cmd - Command name
 * @param args - Command arguments (should contain password for protected commands)
 * @return BOOL - TRUE if authenticated, FALSE otherwise
 */
BOOL cmd_authenticate(const char* cmd, const char* args);

/* ============================================================================
 * Generic Command Handler
 * ============================================================================ */

/**
 * @brief Generic command handler - calls module functions with common validation
 * @param cmd_enum - Command enum type
 * @param args_string - Command arguments as comma-separated string (for SET) or NULL (for GET)
 * @param response_buffer - Response buffer to fill
 * @param buffer_size - Size of response buffer
 * @return Result - Result
 */
Result cmd_handler_generic(cmd_enum_t cmd_enum, const char* args_string, char* response_buffer, size_t buffer_size);

/* ============================================================================
 * Command Processing State Handlers
 * ============================================================================ */

/**
 * @brief Handle CHECK_STM_PREFIX state - check if command is STM: prefix and route to UART
 * @param source_module - Source module ID
 * @param command_text - Command text
 * @param source_address - Source address
 * @param response_buffer - Response buffer for error messages
 * @param buffer_size - Size of response buffer
 * @return Result - RESULT_SUCCESS if routed, RESULT_ERROR if failed, RESULT_BUSY if not STM: prefix
 */
Result cmd_state_handle_check_stm_prefix(ModuleId source_module, const char* command_text,
                                        const char* source_address, char* response_buffer, size_t buffer_size);

/**
 * @brief Handle PARSE_COMMAND state - parse command text
 * @param command_text - Command text
 * @param cmd - Output command name buffer
 * @param args - Output arguments buffer
 * @param response_buffer - Response buffer for error messages
 * @param buffer_size - Size of response buffer
 * @return Result
 */
Result cmd_state_handle_parse_command(const char* command_text, char* cmd, char* args,
                                     char* response_buffer, size_t buffer_size);

/**
 * @brief Handle FIND_COMMAND state - find command enum from name
 * @param cmd - Command name
 * @param cmd_enum - Output command enum
 * @param response_buffer - Response buffer for error messages
 * @param buffer_size - Size of response buffer
 * @return Result
 */
Result cmd_state_handle_find_command(const char* cmd, cmd_enum_t* cmd_enum, char* response_buffer, size_t buffer_size);

/**
 * @brief Handle GET_ENTRY state - get command entry from enum
 * @param cmd_enum - Command enum
 * @param entry - Output command entry pointer
 * @param response_buffer - Response buffer for error messages
 * @param buffer_size - Size of response buffer
 * @return Result
 */
Result cmd_state_handle_get_entry(cmd_enum_t cmd_enum, const cmd_handler_entry_t** entry,
                                 char* response_buffer, size_t buffer_size);

/**
 * @brief Handle AUTHENTICATE state - authenticate command if required
 * @param cmd - Command name
 * @param args - Command arguments
 * @param entry - Command entry
 * @param response_buffer - Response buffer for error messages
 * @param buffer_size - Size of response buffer
 * @return Result
 */
Result cmd_state_handle_authenticate(const char* cmd, char* args,
                                    const cmd_handler_entry_t* entry, char* response_buffer, size_t buffer_size);

/**
 * @brief Handle EXECUTE state - execute command handler
 * @param entry - Command entry
 * @param cmd_enum - Command enum
 * @param args_string - Command arguments as string
 * @param response_buffer - Response buffer to fill
 * @param buffer_size - Size of response buffer
 * @return Result
 */
Result cmd_state_handle_execute(const cmd_handler_entry_t* entry, cmd_enum_t cmd_enum,
                               const char* args_string, char* response_buffer, size_t buffer_size);

/**
 * @brief Handle UPDATE_STATS state - update command statistics
 * @param result - Command result
 * @return Result (always success)
 */
Result cmd_state_handle_update_stats(Result result);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_COMMAND_HANDLER_H */
