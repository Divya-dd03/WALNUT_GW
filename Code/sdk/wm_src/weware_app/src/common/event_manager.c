/**
 * @file event_manager.c
 * @brief Event broadcast system implementation
 */

/*---------------------------------------------------------------
 * Standard Includes
 *--------------------------------------------------------------*/
 #include <string.h>
 #include <stdlib.h>
 
 /*---------------------------------------------------------------
  * Weware Platform Includes
  *--------------------------------------------------------------*/
 #include "common/event_manager.h"
 #include "common/utils.h"
 
 /*---------------------------------------------------------------
  * Log Configuration
  *--------------------------------------------------------------*/
 #define LOG_TAG "EVENT"
 #define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

 /*---------------------------------------------------------------
  * Log Configuration
  *--------------------------------------------------------------*/
 #define MAX_CALLBACKS_PER_EVENT 16
 
 /*---------------------------------------------------------------
  * Type Definitions
  *--------------------------------------------------------------*/
 
 typedef struct
 {
     EventCallback callback;
     void         *user_data;
     const char   *module_name;
     BOOL          active;
 } CallbackEntry;
 
 typedef struct
 {
     CallbackEntry callbacks[MAX_CALLBACKS_PER_EVENT];
     UINT32        count;
 } EventRegistry;
 
 /*---------------------------------------------------------------
  * Global State
  *--------------------------------------------------------------*/
 
 static struct
 {
     EventRegistry registries[EVENT_COUNT];
     BOOL          initialized;
 } g_event_manager = {0};
 
 /*---------------------------------------------------------------
  * Internal Helpers
  *--------------------------------------------------------------*/
 
 static inline BOOL event_manager_is_valid_event(EventType type)
 {
     return (type < EVENT_COUNT);
 }
 
 static inline void registry_clear(EventRegistry *r)
 {
     memset(r, 0, sizeof(*r));
 }
 
static BOOL registry_remove_index(EventRegistry *r, UINT32 idx)
{
    if (!r) {
        return FALSE;
    }
    
    /* Safety check: prevent underflow */
    if (idx >= r->count || r->count == 0) {
        LOG_ERROR("Invalid index %u for removal (count=%u)",
                  idx, r->count);
        return FALSE;
    }
 
     if (idx < r->count - 1) {
         r->callbacks[idx] = r->callbacks[r->count - 1];
     }
 
     memset(&r->callbacks[r->count - 1], 0, sizeof(CallbackEntry));
     r->count--;
     return TRUE;
}
 
 /*---------------------------------------------------------------
  * Public API
  *--------------------------------------------------------------*/
 
 Result event_manager_init(void)
 {
    if (g_event_manager.initialized) {
        LOG_DEBUG("Already initialized");
        return RESULT_ALREADY_INITIALIZED;
    }

    memset(&g_event_manager, 0, sizeof(g_event_manager));
    g_event_manager.initialized = TRUE;

    LOG_INFO("Initialized");
     return RESULT_SUCCESS;
 }
 
 Result event_manager_deinit(void)
 {
     if (!g_event_manager.initialized) {
         return RESULT_SUCCESS;
     }
 
     for (UINT32 i = 0; i < EVENT_COUNT; i++) {
         registry_clear(&g_event_manager.registries[i]);
     }
 
    g_event_manager.initialized = FALSE;
    LOG_INFO("Deinitialized");
    return RESULT_SUCCESS;
 }
 
Result event_manager_register(EventType event_type,
                              EventCallback callback,
                              void *user_data,
                              const char *module_name)
{
    if (!g_event_manager.initialized) {
        LOG_ERROR("Not initialized");
        return RESULT_ERROR;
    }
 
     if (!event_manager_is_valid_event(event_type)) {
         LOG_ERROR("Invalid event type %d (max=%d)",
                   event_type, EVENT_COUNT - 1);
         return RESULT_INVALID_PARAM;
     }
     
     if (!callback) {
         LOG_ERROR("NULL callback provided for event %d",
                   event_type);
         return RESULT_INVALID_PARAM;
     }
     
     if (!module_name || module_name[0] == '\0') {
         LOG_ERROR("NULL or empty module_name provided for event %d",
                   event_type);
         return RESULT_INVALID_PARAM;
     }
 
     EventRegistry *r = &g_event_manager.registries[event_type];
 
     /* Check for duplicate registration (e.g. module_manager re-init after reset timeout) */
     for (UINT32 i = 0; i < r->count; i++) {
        if (r->callbacks[i].active &&
            r->callbacks[i].callback == callback) {
            LOG_DEBUG("Callback already registered for event %d by '%s'",
                     event_type, module_name);
            return RESULT_ALREADY_INITIALIZED;
        }
     }
 
    if (r->count >= MAX_CALLBACKS_PER_EVENT) {
        LOG_ERROR("Max callbacks (%d) reached for event %d",
                  MAX_CALLBACKS_PER_EVENT, event_type);
        return RESULT_ERROR;
    }
 
     CallbackEntry *e = &r->callbacks[r->count++];
     e->callback    = callback;
     e->user_data   = user_data;
     e->module_name = module_name;
     e->active      = TRUE;
 
    /* Log registration at INFO level for production visibility */
    LOG_INFO("Module '%s' registered for event %d (total listeners=%u)",
             module_name, event_type, r->count);
 
     return RESULT_SUCCESS;
 }
 
Result event_manager_unregister(EventType event_type,
                                 EventCallback callback)
{
    if (!g_event_manager.initialized) {
        LOG_ERROR("Not initialized");
        return RESULT_ERROR;
    }
 
     if (!event_manager_is_valid_event(event_type)) {
         LOG_ERROR("Invalid event type %d (max=%d)",
                   event_type, EVENT_COUNT - 1);
         return RESULT_INVALID_PARAM;
     }
     
     if (!callback) {
         LOG_ERROR("NULL callback provided for unregister event %d",
                   event_type);
         return RESULT_INVALID_PARAM;
     }
 
     EventRegistry *r = &g_event_manager.registries[event_type];
 
     for (UINT32 i = 0; i < r->count; i++) {
         if (r->callbacks[i].active &&
             r->callbacks[i].callback == callback) {
 
            LOG_DEBUG("Module '%s' unregistered from event %d (remaining=%u)",
                      r->callbacks[i].module_name, event_type, r->count - 1);
            registry_remove_index(r, i);
            return RESULT_SUCCESS;
         }
     }
 
    LOG_WARN("Callback not found for event %d", event_type);
    return RESULT_ERROR;
 }
 
Result event_manager_unregister_module(const char *module_name)
{
    if (!g_event_manager.initialized) {
        LOG_ERROR("Not initialized");
        return RESULT_ERROR;
    }
    
    if (!module_name || module_name[0] == '\0') {
        LOG_ERROR("NULL or empty module_name provided for unregister_module");
        return RESULT_INVALID_PARAM;
    }
 
     UINT32 removed = 0;
 
     for (UINT32 e = 0; e < EVENT_COUNT; e++) {
         EventRegistry *r = &g_event_manager.registries[e];
 
         for (UINT32 i = 0; i < r->count; ) {
             if (r->callbacks[i].active &&
                 r->callbacks[i].module_name &&
                 strcmp(r->callbacks[i].module_name,
                        module_name) == 0) {
                 registry_remove_index(r, i);
                 removed++;
                 continue; /* re-check same index */
             }
             i++;
         }
     }
 
    if (removed > 0) {
        /* Log at INFO level for production visibility */
        LOG_INFO("Unregistered %u callback(s) for module '%s'",
                 removed, module_name);
    } else {
        /* Log warning if no callbacks were found to unregister */
        LOG_WARN("No callbacks found to unregister for module '%s'",
                 module_name);
    }
 
     return RESULT_SUCCESS;
 }
 
Result event_manager_broadcast(EventType event_type,
                               const char *source_module,
                               void *data,
                               size_t data_size)
{
    if (!g_event_manager.initialized) {
        LOG_ERROR("Not initialized");
        return RESULT_ERROR;
    }
 
     if (!event_manager_is_valid_event(event_type)) {
         LOG_ERROR("Invalid event type %d (max=%d)",
                   event_type, EVENT_COUNT - 1);
         return RESULT_INVALID_PARAM;
     }
     
     if (!source_module || source_module[0] == '\0') {
         LOG_ERROR("NULL or empty source_module provided for broadcast event %d",
                   event_type);
         return RESULT_INVALID_PARAM;
     }
 
    EventRegistry *r = &g_event_manager.registries[event_type];
    if (r->count == 0) {
        /* No listeners is normal, use DEBUG level to avoid spam in production */
        LOG_DEBUG("No listeners for event %d from '%s'",
                  event_type, source_module);
        return RESULT_SUCCESS;
    }
 
     EventData event = {
         .type          = event_type,
         .source_module = source_module,
         .data          = data,
         .data_size     = data_size
     };
 
    /* Log broadcast at INFO level for production visibility */
    LOG_INFO("Broadcasting event %d from '%s' to %u listener(s)",
             event_type, source_module, r->count);
 
     /* Intentional snapshot: callbacks may modify registry */
     UINT32 count = r->count;
     UINT32 called_count = 0;
     for (UINT32 i = 0; i < count; i++) {
         if (r->callbacks[i].active &&
             r->callbacks[i].callback) {
             /* Call callback - if it crashes, we'll see it in logs */
             r->callbacks[i].callback(&event,
                                      r->callbacks[i].user_data);
             called_count++;
         }
     }
     
     /* Log if not all callbacks were called (some may have been inactive) */
     if (called_count != count) {
         LOG_WARN("Only %u of %u callbacks called for event %d (some inactive)",
                  called_count, count, event_type);
     }
 
     return RESULT_SUCCESS;
 }
 
 int event_manager_get_listener_count(EventType event_type)
 {
     if (!g_event_manager.initialized) {
         LOG_ERROR("Not initialized");
         return -1;
     }
     
     if (!event_manager_is_valid_event(event_type)) {
         LOG_ERROR("Invalid event type %d (max=%d)",
                   event_type, EVENT_COUNT - 1);
         return -1;
     }
 
     return (int)g_event_manager.registries[event_type].count;
 }
 