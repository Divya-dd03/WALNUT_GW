/**
 * @file state_timeout.c
 * @brief Common state machine timeout management implementation
 */

#include "common/state_timeout.h"
#include "common/utils.h"
#include "common/event_manager.h"

/* SDK Platform Abstraction Layer */
#include "sdk_platform.h"
#include "sdk_platform.h"

#include <string.h>

 /*---------------------------------------------------------------
  * Log Configuration
  *--------------------------------------------------------------*/
 #define LOG_TAG "STATE_TIMEOUT"
 #define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"


static const StateTimeoutConfig *find_state_config(const StateTimeoutContext *ctx, int state)
{
    if (!ctx || !ctx->state_timeouts || ctx->state_timeout_count == 0)
        return NULL;
    
    for (UINT32 i = 0; i < ctx->state_timeout_count; i++)
    {
        if (ctx->state_timeouts[i].state == state)
            return &ctx->state_timeouts[i];
    }
    return NULL;
}

static BOOL is_desired_state(const StateTimeoutContext *ctx)
{
    return ctx && ctx->current_state_config && ctx->current_state_config->reset_overall_on_entry;
}

static void reset_overall_timeout(StateTimeoutContext *ctx)
{
    if (!ctx)
        return;
    ctx->overall_start_ticks = SDK_GET_TICKS();
}

static void handle_timeout_action(StateTimeoutContext *ctx, const char *module_name, 
                                  StateTimeoutAction action, int state, UINT32 elapsed_ms, BOOL is_overall)
{
    if (!ctx || !module_name)
    {
        LOG_ERROR("Action failed: invalid context or module name");
        return;
    }
    
    switch (action)
    {
    case STATE_TIMEOUT_ACTION_NONE:
        LOG_WARN("[%s] Timeout: state=%d, elapsed=%u ms (no action)",
                 module_name, state, elapsed_ms);
        break;
        
    case STATE_TIMEOUT_ACTION_RESET_SOFT:
        LOG_ERROR("[%s] Timeout: Soft reset (state=%d, elapsed=%u ms)",
                  module_name, state, elapsed_ms);
        event_manager_broadcast(EVENT_RESET_SOFT, module_name, NULL, 0);
        break;
        
    case STATE_TIMEOUT_ACTION_RESET_HARD:
        LOG_ERROR("[%s] Timeout: Hard reset (state=%d, elapsed=%u ms)",
                  module_name, state, elapsed_ms);
        event_manager_broadcast(EVENT_RESET_HARD, module_name, NULL, 0);
        break;
        
    case STATE_TIMEOUT_ACTION_CALLBACK:
        if (is_overall && ctx->overall_timeout_callback)
        {
            BOOL callback_handled = ctx->overall_timeout_callback(state, elapsed_ms, TRUE, ctx->overall_timeout_user_data);
            LOG_DEBUG("[%s] Overall timeout callback executed (state=%d, elapsed=%u ms, handled=%d)",
                     module_name, state, elapsed_ms, callback_handled ? 1 : 0);
            /* If callback handled the timeout, reset state entry ticks to prevent repeated triggers */
            if (callback_handled)
            {
                ctx->state_entry_ticks = SDK_GET_TICKS();
            }
        }
        else if (!is_overall && ctx->state_timeout_callback)
        {
            BOOL callback_handled = ctx->state_timeout_callback(state, elapsed_ms, FALSE, ctx->state_timeout_user_data);
            LOG_DEBUG("[%s] State timeout callback executed (state=%d, elapsed=%u ms, handled=%d)",
                     module_name, state, elapsed_ms, callback_handled ? 1 : 0);
            /* If callback handled the timeout, reset state entry ticks to prevent repeated triggers */
            if (callback_handled)
            {
                ctx->state_entry_ticks = SDK_GET_TICKS();
            }
        }
        else
        {
            LOG_WARN("[%s] Timeout: Callback action but no callback registered (state=%d, elapsed=%u ms, is_overall=%d)",
                     module_name, state, elapsed_ms, is_overall ? 1 : 0);
        }
        break;
        
    default:
        LOG_WARN("[%s] Timeout: Unknown action %d (state=%d, elapsed=%u ms)",
                 module_name, action, state, elapsed_ms);
        break;
    }
}

static BOOL check_state_timeout(StateTimeoutContext *ctx, const char *module_name)
{
    if (!ctx || !module_name)
        return FALSE;
    
    if (ctx->current_state < 0 || !ctx->current_state_config)
        return FALSE;
    
    if (ctx->current_state_config->timeout_ms == 0)
        return FALSE;
    
    /* Calculate elapsed time since entering current state (timer resets on state change) */
    UINT32 elapsed_ms = utils_elapsed_ms_since(ctx->state_entry_ticks);
    if (elapsed_ms < ctx->current_state_config->timeout_ms)
    {
        LOG_DEBUG("[%s] State timeout check: state=%d, elapsed=%u ms, timeout=%u ms (not timed out)",
                 module_name, ctx->current_state, elapsed_ms, ctx->current_state_config->timeout_ms);
        return FALSE;  /* Not timed out yet - state must remain same for full timeout duration */
    }
    
    /* State has remained unchanged for the specified timeout duration - trigger action */
    LOG_WARN("[%s] State timeout: state=%d, elapsed=%u ms, timeout=%u ms",
             module_name, ctx->current_state, elapsed_ms, ctx->current_state_config->timeout_ms);
    
    ctx->timeout_count++;
    ctx->last_timeout_state = ctx->current_state;
    handle_timeout_action(ctx, module_name, ctx->current_state_config->action, ctx->current_state, elapsed_ms, FALSE);
    return TRUE;
}

static BOOL check_overall_timeout(StateTimeoutContext *ctx, const char *module_name)
{
    if (!ctx || !module_name)
        return FALSE;
    
    if (ctx->overall_timeout_ms == 0)
        return FALSE;
    
    if (is_desired_state(ctx))
    {
        reset_overall_timeout(ctx);
        LOG_DEBUG("[%s] Overall timeout reset: in desired state (state=%d)",
                 module_name, ctx->current_state);
        return FALSE;
    }
    
    UINT32 elapsed_ms = utils_elapsed_ms_since(ctx->overall_start_ticks);
    if (elapsed_ms < ctx->overall_timeout_ms)
    {
        LOG_DEBUG("[%s] Overall timeout check: elapsed=%u ms, timeout=%u ms, state=%d (not timed out)",
                 module_name, elapsed_ms, ctx->overall_timeout_ms, ctx->current_state);
        return FALSE;
    }
    
    LOG_ERROR("[%s] Overall timeout: elapsed=%u ms, timeout=%u ms, state=%d",
              module_name, elapsed_ms, ctx->overall_timeout_ms, ctx->current_state);
    
    ctx->timeout_count++;
    handle_timeout_action(ctx, module_name, ctx->overall_action, ctx->current_state, elapsed_ms, TRUE);
    reset_overall_timeout(ctx);
    return TRUE;
}

Result state_timeout_init(StateTimeoutContext *ctx,
                          const StateTimeoutConfig *state_timeouts,
                          UINT32 state_timeout_count,
                          UINT32 overall_timeout_ms,
                          StateTimeoutAction overall_action,
                          StateTimeoutCallback state_timeout_callback,
                          void *state_timeout_user_data,
                          StateTimeoutCallback overall_timeout_callback,
                          void *overall_timeout_user_data)
{
    if (!ctx)
    {
        LOG_ERROR("Init failed: invalid context");
        return RESULT_INVALID_PARAM;
    }
    
    /* Validate state_timeouts array if count > 0 */
    if (state_timeout_count > 0 && !state_timeouts)
    {
        LOG_ERROR("Init failed: state_timeouts is NULL but count=%u", state_timeout_count);
        return RESULT_INVALID_PARAM;
    }
    
    /* Validate overall_action if overall_timeout_ms > 0 */
    if (overall_timeout_ms > 0 && overall_action >= STATE_TIMEOUT_ACTION_CALLBACK + 1)
    {
        LOG_ERROR("Init failed: invalid overall_action=%d", overall_action);
        return RESULT_INVALID_PARAM;
    }
    
    memset(ctx, 0, sizeof(StateTimeoutContext));
    
    ctx->state_timeouts = state_timeouts;
    ctx->state_timeout_count = state_timeout_count;
    ctx->overall_timeout_ms = overall_timeout_ms;
    ctx->overall_action = overall_action;
    ctx->current_state = -1;
    ctx->last_timeout_state = -1;
    ctx->current_state_config = NULL;
    ctx->state_timeout_callback = state_timeout_callback;
    ctx->state_timeout_user_data = state_timeout_user_data;
    ctx->overall_timeout_callback = overall_timeout_callback;
    ctx->overall_timeout_user_data = overall_timeout_user_data;
    ctx->state_entry_ticks = SDK_GET_TICKS();
    ctx->overall_start_ticks = SDK_GET_TICKS();
    
    LOG_DEBUG("Initialized: state_count=%u, overall_timeout=%u ms, overall_action=%d",
             state_timeout_count, overall_timeout_ms, overall_action);
    return RESULT_SUCCESS;
}

void state_timeout_on_state_change(StateTimeoutContext *ctx, int new_state)
{
    if (!ctx)
    {
        LOG_ERROR("On_state_change failed: invalid context");
        return;
    }
    
    if (ctx->current_state == new_state)
    {
        LOG_DEBUG("On_state_change: state unchanged (%d)", new_state);
        return;
    }
    
    LOG_DEBUG("State changed %d -> %d (timeout=%u ms)",
             ctx->current_state, new_state,
             find_state_config(ctx, new_state) ? find_state_config(ctx, new_state)->timeout_ms : 0);
    
    /* State changed - reset per-state timeout timer */
    ctx->current_state = new_state;
    ctx->state_entry_ticks = SDK_GET_TICKS();  /* Reset timer when entering new state */
    ctx->current_state_config = find_state_config(ctx, new_state);
    
    /* Reset overall timeout if entering a desired state */
    if (is_desired_state(ctx))
    {
        reset_overall_timeout(ctx);
        LOG_INFO("Overall timeout reset (entered desired state %d)", new_state);
    }
}

BOOL state_timeout_check(StateTimeoutContext *ctx, const char *module_name)
{
    if (!ctx || !module_name)
        return FALSE;
    
    if (check_state_timeout(ctx, module_name))
        return TRUE;
    
    if (check_overall_timeout(ctx, module_name))
        return TRUE;
    
    return FALSE;
}

void state_timeout_reset(StateTimeoutContext *ctx)
{
    if (!ctx)
        return;
    
    ctx->state_entry_ticks = SDK_GET_TICKS();
    ctx->overall_start_ticks = SDK_GET_TICKS();
}

void state_timeout_reset_overall(StateTimeoutContext *ctx)
{
    if (ctx)
        reset_overall_timeout(ctx);
}

UINT32 state_timeout_get_elapsed_ms(StateTimeoutContext *ctx)
{
    if (!ctx)
    {
        LOG_ERROR("Get_elapsed_ms failed: invalid context");
        return 0;
    }
    return utils_elapsed_ms_since(ctx->state_entry_ticks);
}

UINT32 state_timeout_get_overall_elapsed_ms(StateTimeoutContext *ctx)
{
    if (!ctx)
    {
        LOG_ERROR("Get_overall_elapsed_ms failed: invalid context");
        return 0;
    }
    return utils_elapsed_ms_since(ctx->overall_start_ticks);
}

BOOL state_timeout_process(StateTimeoutContext *ctx,
                           int current_state,
                           void *last_state,
                           const char *module_name)
{
    if (!ctx || !last_state || !module_name)
    {
        LOG_ERROR("Process failed: invalid parameters");
        return FALSE;
    }
    
    /* Validate that last_state points to valid memory (basic check) */
    int *last_state_ptr = (int *)last_state;
    if (!last_state_ptr)
    {
        LOG_ERROR("[%s] Process failed: invalid last_state pointer", module_name);
        return FALSE;
    }
    
    /* Validate state value to prevent crashes from corrupted state */
    /* States should be non-negative and reasonable (max 1000 to catch obvious corruption) */
    if (current_state < -1 || current_state > 1000)
    {
        LOG_ERROR("[%s] Invalid state value: %d (possible corruption)", module_name, current_state);
        return FALSE;
    }
    
    /* Read last_state once and store locally to avoid race conditions */
    int saved_last_state = *last_state_ptr;
    
    /* Validate saved_last_state as well */
    if (saved_last_state < -1 || saved_last_state > 1000)
    {
        LOG_ERROR("[%s] Invalid last_state value: %d (possible corruption), resetting", module_name, saved_last_state);
        saved_last_state = current_state;  /* Reset to current state */
        *last_state_ptr = current_state;
    }
    
    BOOL state_changed = (current_state != saved_last_state);
    
    if (state_changed)
    {
        /* State transition detected - reset per-state timeout timer */
        /* Use saved_last_state to avoid reading from memory again (race condition protection) */
        LOG_INFO("[%s] STATE %d -> %d", module_name, saved_last_state, current_state);
        *last_state_ptr = current_state;
        state_timeout_on_state_change(ctx, current_state);  /* Resets state_entry_ticks - timer restarts */
    }
    
    /* Reset overall timeout if in desired state with no per-state timeout */
    if (!state_changed && is_desired_state(ctx))
    {
        if (ctx->current_state_config && ctx->current_state_config->timeout_ms == 0)
        {
            reset_overall_timeout(ctx);
            LOG_DEBUG("[%s] Overall timeout reset (desired state, no per-state timeout)",
                     module_name);
            return FALSE;
        }
    }
    
    /* Check for timeouts - only triggers if state remained same for full timeout duration */
    return state_timeout_check(ctx, module_name);
}
