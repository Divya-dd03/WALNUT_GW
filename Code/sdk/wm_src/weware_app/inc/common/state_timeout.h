/**
 * @file state_timeout.h
 * @brief Common state machine timeout management for all modules
 * 
 * This module provides a common timeout mechanism for state machines across
 * all modules. It supports:
 * - Per-state timeouts (each state can have its own timeout)
 * - Overall state machine timeout (global timeout for the entire state machine)
 * - Configurable timeout actions (reset, alert, transition to error, etc.)
 */

#ifndef WEWARE_STATE_TIMEOUT_H
#define WEWARE_STATE_TIMEOUT_H

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
 * @brief Timeout action types
 */
typedef enum
{
    STATE_TIMEOUT_ACTION_NONE = 0,        /**< No action - just log */
    STATE_TIMEOUT_ACTION_RESET_SOFT,      /**< Broadcast soft reset event */
    STATE_TIMEOUT_ACTION_RESET_HARD,      /**< Broadcast hard reset event */
    STATE_TIMEOUT_ACTION_CALLBACK         /**< Call callback function */
} StateTimeoutAction;

/**
 * @brief State timeout configuration entry
 */
typedef struct
{
    int state;                      /**< State value (from module's state enum) */
    UINT32 timeout_ms;             /**< Timeout in milliseconds (0 = no timeout) */
    StateTimeoutAction action;      /**< Action to take on timeout */
    BOOL reset_overall_on_entry;   /**< If TRUE, reset overall timeout when entering this state (for desired states) */
} StateTimeoutConfig;

/**
 * @brief Timeout callback function type
 * @param state State that timed out (for per-state timeout) or current state (for overall timeout)
 * @param elapsed_ms Elapsed time in milliseconds
 * @param is_overall_timeout TRUE if this is an overall timeout, FALSE if per-state timeout
 * @param user_data User data passed during registration
 * @return TRUE if callback handled the timeout (action won't be executed), FALSE to continue with default action
 */
typedef BOOL (*StateTimeoutCallback)(int state, UINT32 elapsed_ms, BOOL is_overall_timeout, void *user_data);

/**
 * @brief State machine timeout context
 */
typedef struct
{
    int current_state;                      /**< Current state value */
    UINT32 state_entry_ticks;              /**< Tick count when current state was entered */
    UINT32 overall_start_ticks;            /**< Tick count when state machine started */
    
    /* Per-state timeout configuration */
    const StateTimeoutConfig *state_timeouts;      /**< Array of per-state timeout configs */
    UINT32 state_timeout_count;                    /**< Number of entries in state_timeouts array */
    const StateTimeoutConfig *current_state_config; /**< Cached config for current state (NULL if not found) */
    
    /* Overall state machine timeout */
    UINT32 overall_timeout_ms;             /**< Overall timeout in milliseconds (0 = disabled) */
    StateTimeoutAction overall_action;      /**< Action on overall timeout */
    
    /* Timeout callbacks (optional) */
    StateTimeoutCallback state_timeout_callback;      /**< Callback for per-state timeouts */
    void *state_timeout_user_data;                    /**< User data for per-state timeout callback */
    StateTimeoutCallback overall_timeout_callback;    /**< Callback for overall timeout */
    void *overall_timeout_user_data;                  /**< User data for overall timeout callback */
    
    /* Statistics */
    UINT32 timeout_count;                   /**< Number of timeouts that occurred */
    int last_timeout_state;                  /**< Last state that timed out */
} StateTimeoutContext;

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/

/**
 * @brief Initialize state timeout context
 * @param ctx Timeout context to initialize (must not be NULL)
 * @param state_timeouts Array of per-state timeout configurations (can be NULL)
 * @param state_timeout_count Number of entries in state_timeouts array
 * @param overall_timeout_ms Overall state machine timeout in milliseconds (0 = disabled)
 * @param overall_action Action to take on overall timeout
 * @param state_timeout_callback Callback for per-state timeouts (can be NULL)
 * @param state_timeout_user_data User data for per-state timeout callback
 * @param overall_timeout_callback Callback for overall timeout (can be NULL)
 * @param overall_timeout_user_data User data for overall timeout callback
 * @return RESULT_SUCCESS on success, RESULT_INVALID_PARAM on error
 */
Result state_timeout_init(StateTimeoutContext *ctx,
                          const StateTimeoutConfig *state_timeouts,
                          UINT32 state_timeout_count,
                          UINT32 overall_timeout_ms,
                          StateTimeoutAction overall_action,
                          StateTimeoutCallback state_timeout_callback,
                          void *state_timeout_user_data,
                          StateTimeoutCallback overall_timeout_callback,
                          void *overall_timeout_user_data);

/**
 * @brief Update state timeout context when state changes
 * @param ctx Timeout context (must not be NULL)
 * @param new_state New state value
 * @note Call this whenever the state machine transitions to a new state
 * @note If the new state is marked as a desired state (reset_overall_on_entry=TRUE),
 *       the overall timeout will be reset
 */
void state_timeout_on_state_change(StateTimeoutContext *ctx, int new_state);

/**
 * @brief Reset overall timeout manually
 * @param ctx Timeout context (must not be NULL)
 * @note This can be called to reset the overall timeout timer without changing state
 */
void state_timeout_reset_overall(StateTimeoutContext *ctx);

/**
 * @brief Check for timeouts and handle them
 * @param ctx Timeout context (must not be NULL)
 * @param module_name Module name for logging (e.g., "Network", "GPS")
 * @return TRUE if timeout occurred and action was taken, FALSE otherwise
 * @note This should be called periodically in the state machine loop
 */
BOOL state_timeout_check(StateTimeoutContext *ctx, const char *module_name);

/**
 * @brief Common state machine loop helper: detect state change, update timeout context, and check timeouts
 * @param ctx Timeout context (must not be NULL)
 * @param current_state Current state value (from state machine)
 * @param last_state Pointer to variable storing last state (will be updated)
 * @param module_name Module name for logging (e.g., "Network", "GPS")
 * @return TRUE if timeout occurred and action was taken, FALSE otherwise
 * @note This function should be called at the start of each state machine loop iteration
 * @note It handles:
 *       - Detecting state changes
 *       - Logging state transitions
 *       - Updating timeout context on state change
 *       - Checking for timeouts
 * 
 * @example
 *   NetworkState last_state = NETWORK_STATE_INIT;
 *   while (TRUE) {
 *       state_timeout_process(&g_network.timeout_ctx, g_network.state, &last_state, "Network");
 *       // ... rest of state machine logic
 *   }
 */
BOOL state_timeout_process(StateTimeoutContext *ctx,
                           int current_state,
                           void *last_state,
                           const char *module_name);

/**
 * @brief Reset state timeout context (restart timers)
 * @param ctx Timeout context (must not be NULL)
 */
void state_timeout_reset(StateTimeoutContext *ctx);

/**
 * @brief Get elapsed time in current state (milliseconds)
 * @param ctx Timeout context (must not be NULL)
 * @return Elapsed time in milliseconds since entering current state
 */
UINT32 state_timeout_get_elapsed_ms(StateTimeoutContext *ctx);

/**
 * @brief Get elapsed time since state machine start (milliseconds)
 * @param ctx Timeout context (must not be NULL)
 * @return Elapsed time in milliseconds since state machine started
 */
UINT32 state_timeout_get_overall_elapsed_ms(StateTimeoutContext *ctx);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_STATE_TIMEOUT_H */

