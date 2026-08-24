/**
 * @file event_manager.h
 * @brief Event broadcast system for inter-module communication
 * 
 * This module provides a publish-subscribe pattern for modules to broadcast
 * state changes (like connection/disconnection) and for other modules to
 * react to these events.
 */

#ifndef WEWARE_EVENT_MANAGER_H
#define WEWARE_EVENT_MANAGER_H

/*---------------------------------------------------------------
 * Standard Includes
 *--------------------------------------------------------------*/
#include "common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------
 * Type Definitions
 *--------------------------------------------------------------*/

/**
 * @brief Event types that can be broadcast
 */
typedef enum
{
    EVENT_NETWORK_CONNECTED,           /**< Network module connected */
    EVENT_NETWORK_DISCONNECTED,       /**< Network module disconnected */
    EVENT_SIM_AVAILABLE,              /**< SIM card available/inserted */
    EVENT_SIM_UNAVAILABLE,            /**< SIM card unavailable/removed */
    EVENT_GPS_CONNECTED,              /**< GPS module connected */
    EVENT_GPS_DISCONNECTED,            /**< GPS module disconnected */
    EVENT_GPS_CONFIGURED,             /**< GPS module configured */
    EVENT_TCP_CONNECTED,              /**< TCP client connected */
    EVENT_TCP_DISCONNECTED,           /**< TCP client disconnected */
    EVENT_SMS_CONNECTED,              /**< SMS manager connected/ready */
    EVENT_SMS_DISCONNECTED,           /**< SMS manager disconnected/not ready */
    EVENT_SMS_RECEIVED,               /**< SMS received */
    EVENT_MODULE_INITIALIZED,         /**< Module initialized */
    EVENT_MODULE_ERROR,               /**< Module error occurred */
    
    /* Power events (broadcast by power manager on state change) */
    EVENT_CHARGE_CONNECTED,           /**< Charge connected (EV > threshold) */
    EVENT_CHARGE_DISCONNECTED,        /**< Charge disconnected */
    EVENT_IGN_ON,                     /**< Ignition on */
    EVENT_IGN_OFF,                    /**< Ignition off */
    EVENT_MOTION_ON,                  /**< Vehicle motion on */
    EVENT_MOTION_OFF,                 /**< Vehicle motion off */
    
    /* Reset events (broadcast by modules, handled by reset_handler) */
    EVENT_RESET_SOFT,                 /**< Request soft reset (software reset) */
    EVENT_RESET_HARD,                 /**< Request hard reset (power cycle) */
    
    /* URC-based events (broadcast by URC processor) */
    EVENT_URC_SMS,                    /**< SMS URC received */
    EVENT_URC_NETWORK_STATUS,        /**< Network status URC received (CREG, CGREG, CGATT, etc.) */
    EVENT_URC_SIM_STATUS,             /**< SIM status URC received (CPIN_READY, CPIN_REMOVED, etc.) */
    EVENT_URC_AT_RESPONSE,            /**< AT command response URC */
    
    EVENT_COUNT                       /**< Total number of event types (for validation) */
} EventType;

/**
 * @brief Event data structure passed to callbacks
 */
typedef struct
{
    EventType type;                   /**< Type of event */
    const char *source_module;        /**< Module that generated the event */
    void *data;                       /**< Optional event-specific data (NULL if not used) */
    size_t data_size;                 /**< Size of data in bytes (0 if data is NULL) */
} EventData;

/**
 * @brief Event callback function type
 * @param event Event data structure
 * @param user_data User-provided context data (passed during registration)
 */
typedef void (*EventCallback)(const EventData *event, void *user_data);

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/

/**
 * @brief Initialize event manager
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result event_manager_init(void);

/**
 * @brief Deinitialize event manager and unregister all callbacks
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result event_manager_deinit(void);

/**
 * @brief Register a callback for a specific event type
 * @param event_type Type of event to listen for
 * @param callback Callback function to call when event occurs
 * @param user_data User-provided context data (passed to callback, can be NULL)
 * @param module_name Name of the module registering (for logging/debugging)
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result event_manager_register(EventType event_type, EventCallback callback, 
                               void *user_data, const char *module_name);

/**
 * @brief Unregister a callback for a specific event type
 * @param event_type Type of event to stop listening for
 * @param callback Callback function to unregister (must match registered callback)
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result event_manager_unregister(EventType event_type, EventCallback callback);

/**
 * @brief Unregister all callbacks for a specific module
 * @param module_name Name of the module to unregister all callbacks for
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result event_manager_unregister_module(const char *module_name);

/**
 * @brief Broadcast an event to all registered listeners
 * @param event_type Type of event to broadcast
 * @param source_module Name of the module broadcasting the event
 * @param data Optional event-specific data (NULL if not used)
 * @param data_size Size of data in bytes (0 if data is NULL)
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result event_manager_broadcast(EventType event_type, const char *source_module,
                               void *data, size_t data_size);

/**
 * @brief Get the number of registered callbacks for an event type
 * @param event_type Type of event to query
 * @return Number of registered callbacks, or -1 on error
 */
int event_manager_get_listener_count(EventType event_type);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_EVENT_MANAGER_H */

