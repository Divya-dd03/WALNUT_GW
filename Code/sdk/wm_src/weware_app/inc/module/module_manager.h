/**
 * @file module_manager.h
 * @brief Module management for weware system
 *
 * Provides functions to manage module lifecycle, status, and monitoring.
 */

#ifndef WEWARE_MODULE_MANAGER_H
#define WEWARE_MODULE_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/types.h"
#include "common/queue_manager.h"

/* Forward declaration to avoid circular dependency */
typedef struct Queue Queue;

/**
 * @brief Module identifier enum
 * @note This enum must match the order of modules in g_modules[] array
 */
typedef enum
{
    MODULE_ID_LOG = 0,       /**< Log Manager (async sink; init first in g_modules[]) */
    MODULE_ID_UART,          /**< UART Manager */
    MODULE_ID_CMD,           /**< Command Manager */
    MODULE_ID_URC,           /**< URC Processor */
    MODULE_ID_SIM,           /**< SIM Manager */
    MODULE_ID_NETWORK,       /**< Network Manager */
    MODULE_ID_TCP,           /**< TCP Client */
    MODULE_ID_GPS,           /**< GPS Manager */
    MODULE_ID_SMS,           /**< SMS Manager */
    MODULE_ID_BLE,           /**< BLE Manager */
    MODULE_ID_SYSTEM,        /**< System config (vehicle state settings; no task) */
    MODULE_ID_FILE_TRANSFER,
    MODULE_ID_COUNT          /**< Total number of module slots (includes virtual IDs) */
} ModuleId;

/** Maximum size of @c message[]; payload byte length is always @c data_len (set by producers). */
#define MODULE_MESSAGE_INLINE_SIZE 256

/**
 * @brief Common inter-module message structure
 * @note Used by UART, SMS, TCP modules for message routing.
 *       Payload is either in message[] (use_dynamic_buffer FALSE) or at dynamic_buffer (use_dynamic_buffer TRUE).
 *       @c data_len is always the payload byte length (inline @c message[] or @c dynamic_buffer); must be set by producers.
 */
typedef struct {
    ModuleId source_module;            /**< Source module ID (e.g., MODULE_ID_UART) */
    ModuleId destination_module;       /**< Destination module ID (e.g., MODULE_ID_SMS, MODULE_ID_TCP) */
    char address[64];                  /**< Address/connection info (e.g., phone number, IP:port) */
    char message[MODULE_MESSAGE_INLINE_SIZE];  /**< Inline payload when use_dynamic_buffer is FALSE */
    void *dynamic_buffer;              /**< Payload when use_dynamic_buffer is TRUE; caller allocates, consumer frees */
    UINT32 data_len;                   /**< Byte length: dynamic_buffer if use_dynamic_buffer; else message[] payload */
    BOOL use_dynamic_buffer;           /**< If TRUE, payload at dynamic_buffer (length in data_len); else message[] (length in data_len) */
    BOOL is_raw;                       /**< TX only: if TRUE, UART sends the payload bytes verbatim (binary frame), no "SRC,ADDR,MSG#" ASCII wrapping */
    BOOL payload_is_binary;            /**< RX routing: if TRUE, message[]/dynamic_buffer holds raw binary (length in data_len), not a NUL-terminated string */
} ModuleMessage;

/**
 * @brief Get pointer to message payload (inline or dynamic)
 * @param msg ModuleMessage (must not be NULL)
 * @return Pointer to payload (message[] or dynamic_buffer); NULL only if use_dynamic_buffer and dynamic_buffer is NULL
 */
#define module_message_payload_ptr(msg) \
    ((const char *)((msg)->use_dynamic_buffer ? (msg)->dynamic_buffer : (msg)->message))

/**
 * @brief Get length of message payload in bytes
 * @param msg ModuleMessage (must not be NULL)
 * @return @c data_len (payload bytes; zero means empty).
 */
size_t module_message_payload_len(const ModuleMessage *msg);

/**
 * @brief Function pointer type for module initialization
 */
typedef Result (*ModuleInitFn)(void);

/**
 * @brief Function pointer type for module deinitialization
 */
typedef Result (*ModuleDeinitFn)(void);

/**
 * @brief Function pointer type for module config get_defaults
 * @param config Pointer to module-specific config structure (will be cast appropriately)
 */
typedef void (*ModuleConfigGetDefaultsFn)(void *config);

/**
 * @brief Function pointer type for module config validate
 * @param config Pointer to module-specific config structure (will be cast appropriately)
 * @return RESULT_SUCCESS if valid, RESULT_INVALID_PARAM if invalid
 */
typedef Result (*ModuleConfigValidateFn)(const void *config);

/**
 * @brief Module configuration (user-defined settings)
 * @note This structure contains all configuration that is set by the user/developer
 */
typedef struct
{
    ModuleId module_id;                                  /**< Module identifier (enum) */
    const char *name;                                    /**< Module name (for logging/debugging) */
    BOOL enabled;                                        /**< Whether module is enabled */
    BOOL required;                                       /**< Whether module is required for system operation */
    BOOL continue_on_fail;                               /**< Whether to continue initialization if this module fails */
    BOOL has_task;                                       /**< Whether module has a background task */

    /* Module function pointers */
    ModuleInitFn init_fn;                                /**< Module initialization function */
    ModuleDeinitFn deinit_fn;                            /**< Module deinitialization function */
    
    /* Config management function pointers */
    ModuleConfigGetDefaultsFn config_get_defaults_fn;   /**< Function to get module config defaults (NULL if not applicable) */
    ModuleConfigValidateFn config_validate_fn;           /**< Function to validate module config (NULL if not applicable) */
    void *config_ptr;                                    /**< Pointer to module's config structure (NULL if not applicable) */
    size_t config_size;                                  /**< Size of module's config structure in bytes (0 if not applicable) */
    BOOL config_stored;                                  /**< Whether to store this module's config in storage file (TRUE) or keep in memory only (FALSE) */
    
    /* Queue configuration and reference */
    QueueConfig msg_q_config;                            /**< Message queue configuration (element_size=0 means no queue) */
    Queue *msg_q;                                         /**< Created message queue reference (NULL if not created) */
    QueueConfig urc_q_config;                            /**< URC queue: element_size should be sizeof(sdk_msg_t); capacity 0 = disabled */
    Queue *urc_q;                                         /**< URC queue (NULL if not created or not configured) */
} ModuleConfig;

/**
 * @brief Module runtime status (system-managed)
 * @note This structure contains runtime status that is automatically managed by the system
 */
typedef struct
{
    BOOL initialized;                                    /**< Whether module is initialized (auto-set to FALSE on boot) */
    BOOL connected;                                      /**< Whether module is connected (auto-set to FALSE on boot) */
    UINT32 task_uptime_sec;                              /**< Task uptime in seconds (auto-set to boot time) */
    int retries;                                         /**< Number of retry attempts */
    int last_error;                                      /**< Last error code */
} ModuleStatus;

/**
 * @brief Combined module structure (config + status)
 * @note This structure combines both configuration and runtime status for convenience
 */
typedef struct
{
    ModuleConfig config;                                  /**< Module configuration (user-defined) */
    ModuleStatus status;                                  /**< Module runtime status (system-managed) */
} Module;

/**
 * @brief Update module task uptime
 * @param module_id Module identifier
 */
void module_manager_update_uptime(ModuleId module_id);

/**
 * @brief Initialize all modules (uses MODULE_INIT_RETRY_COUNT and MODULE_INIT_RETRY_INTERVAL_MS from module_config.h)
 * @return RESULT_SUCCESS if all required modules initialized, RESULT_ERROR otherwise
 */
Result module_manager_init(void);

/**
 * @brief Monitor module tasks for timeouts
 * @param timeout_sec Timeout in seconds
 * @return TRUE if any task timed out, FALSE otherwise
 */
BOOL module_manager_monitor_tasks(UINT32 timeout_sec);

/**
 * @brief Periodic loop iteration for module manager (uses MODULE_TASK_TIMEOUT_SEC from module_config.h)
 * @return TRUE if any module task timed out (caller should reboot), FALSE otherwise
 */
BOOL module_manager_loop_iteration(void);

/**
 * @brief Set module connection status
 * @param module_id Module identifier
 * @param connected Connection status (TRUE = connected, FALSE = disconnected)
 */
void module_manager_set_connected(ModuleId module_id, BOOL connected);

/**
 * @brief Get module connection status
 * @param module_id Module identifier
 * @return TRUE if connected, FALSE otherwise
 */
BOOL module_manager_get_connected(ModuleId module_id);

/**
 * @brief Get module initialization status
 * @param module_id Module identifier
 * @return TRUE if initialized, FALSE otherwise
 */
BOOL module_manager_is_initialized(ModuleId module_id);

/**
 * @brief Get module by ID (direct access - O(1))
 * @param module_id Module identifier
 * @return Pointer to module, or NULL if invalid ID
 */
Module *module_manager_get_module(ModuleId module_id);

/**
 * @brief Get module configuration by ID (direct access - O(1))
 * @param module_id Module identifier
 * @return Pointer to module configuration, or NULL if invalid ID or module not found
 */
const ModuleConfig *module_manager_get_config(ModuleId module_id);

/**
 * @brief Register module_manager for connection status events
 * 
 * Registers event handlers to automatically update module connection status
 * when network, GPS, or TCP connection events are broadcast.
 * 
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result module_manager_register_events(void);

/**
 * @brief Persist reboot-safe module state (currently queue contents) without deinitializing tasks/modules.
 * @return RESULT_SUCCESS if all eligible module data was persisted, RESULT_ERROR otherwise
 */
Result module_manager_persist_reboot_data(void);

/**
 * @brief Deinitialize all modules
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result module_manager_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_MODULE_MANAGER_H */

