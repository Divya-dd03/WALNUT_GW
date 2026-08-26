/**
 * @file command_manager.h
 * @brief Command manager for weware platform - handles command processing, parsing, and management
 * 
 * This module provides command processing functionality including:
 * - Command types, structures, and enums
 * - Command parsing and validation
 * - Authentication
 * - Command execution coordination
 * - Statistics tracking
 */

#ifndef WEWARE_COMMAND_MANAGER_H
#define WEWARE_COMMAND_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/types.h"
#include "common/task_stats.h"
#include "module/module_manager.h"  // For ModuleMessage
#include "module/command/command_config.h"  // For CommandConfig
#include "sdk_platform.h"

/* Command Handler Configuration */
#define CMD_HANDLER_MAX_LENGTH           160
#define CMD_HANDLER_MAX_ARGS             10
#define CMD_HANDLER_MAX_RESPONSE_LENGTH  160

/* Command Enum - Add new commands here */
typedef enum {
    CMD_UNKNOWN = 0,
    CMD_SET_GPS_CONFIG,
    CMD_GET_GPS_CONFIG,
    CMD_SET_NETWORK_CONFIG,
    CMD_GET_NETWORK_CONFIG,
    CMD_SET_TCP_CONFIG,
    CMD_GET_TCP_CONFIG,
    CMD_SET_COMMAND_CONFIG,
    CMD_GET_COMMAND_CONFIG,
    CMD_SET_SYSTEM_CONFIG,
    CMD_GET_SYSTEM_CONFIG,
    CMD_REBOOT,
    CMD_HARD_RESET,
    CMD_GET_DEV_INFO,
    CMD_GET_DEV_STATUS,
    CMD_DELETE_FOLDER,
    CMD_FACTORY_RESET,
    CMD_DIGOUT,
    CMD_SET_MIN_CSQ,
    CMD_FORCED_OTA,
    CMD_GET_OTA_STATUS,
    CMD_PING_STM,
    CMD_MAX
} cmd_enum_t;


/* Module Function Types - Generic function pointer for module commands */
typedef Result (*module_func_t)(void* arg1, void* arg2);

/* Command Handler Statistics */
typedef struct {
    /* Command Processing Statistics */
    UINT32 total_received;        /**< Total number of commands received */
    UINT32 total_processed;       /**< Total number of commands successfully processed */
    UINT32 error_count;          /**< Total number of processing errors */
    UINT32 invalid_cmd_count;   /**< Total number of invalid commands */
    UINT32 auth_failures;        /**< Total number of authentication failures */
} cmd_handler_stats_t;

/**
 * @brief Command manager runtime state structure
 * @note This structure contains all runtime state for the command manager
 */
typedef struct
{
    sdk_task_ref_t task_ref;             /**< Command task reference */
    UINT8 task_stack[8192];               /**< Task stack */
    BOOL initialized;                     /**< Initialization flag */
    Module *module;                       /**< Pointer to module structure */
    CommandConfig *config;                /**< Pointer to command configuration */
    cmd_handler_stats_t stats;            /**< Command handler statistics */
    TaskStats task_stats;                 /**< Task statistics */
    
    /* Runtime variables */
    UINT32 task_interval_ms;               /**< Task sleep interval (ms) */
} command_manager_runtime_t;

/* Command Handler Table Entry */
typedef struct {
    cmd_enum_t cmd_enum;
    const char* name;
    BOOL requires_auth;
    const char* description;
    module_func_t module_func;       /* Module function pointer (for generic handler) - can be different types */
} cmd_handler_entry_t;

/* Public API Functions */

/**
 * @brief Initialize command manager
 * @return Status - Success/failure status
 */
Result command_manager_init(void);

/**
 * @brief Deinitialize command manager
 */
Result command_manager_deinit(void);

/**
 * @brief Accept a command request via ModuleMessage (queued for processing)
 * @param request - ModuleMessage containing command request
 * @return RESULT_SUCCESS if queued successfully, RESULT_ERROR on failure
 * @note The request will be queued and processed by the command manager task
 */
Result command_manager_accept_request(const ModuleMessage* request);

/* Global command table - accessible directly */
extern const cmd_handler_entry_t g_cmd_table[];
extern const UINT32 g_cmd_table_size;

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_COMMAND_MANAGER_H */
