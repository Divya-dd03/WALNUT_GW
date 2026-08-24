/**
 * @file module_manager.c
 * @brief Module management implementation for weware system
 */

#include "module/module_manager.h"
#include "module/module_config.h"
#include "module/urc/urc_processor.h"
#include "common/event_manager.h"
#include "common/utils.h"

/*---------------------------------------------------------------
 * Log Configuration
 *--------------------------------------------------------------*/
#define LOG_TAG          "MODULE"
#define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

/*---------------------------------------------------------------
 * Internal Helpers
 *--------------------------------------------------------------*/

static inline Module *module_get(ModuleId id)
{
    if (id >= MODULE_ID_COUNT || id >= g_module_count)
        return NULL;
    return g_modules[id];
}

static void module_log_connection(const Module *m, BOOL old_state)
{
    LOG_INFO("Module %s is now %s",
             m->config.name,
             m->status.connected ? "connected" : "disconnected");
    (void)old_state;
}

static void module_set_connected_internal(ModuleId id, BOOL connected)
{
    Module *m = module_get(id);
    if (!m)
        return;

    BOOL old = m->status.connected;
    m->status.connected = connected;

    if (old != connected)
        module_log_connection(m, old);
}

static Result module_persist_queue_if_needed(const Module *m,
                                             Queue *queue,
                                             const QueueConfig *cfg,
                                             const char *queue_kind)
{
    if (!m || !queue || !cfg || !cfg->persist_on_reboot)
        return RESULT_SUCCESS;

    Result r = queue_manager_persist(queue, cfg);
    if (result_is_error(r)) {
        LOG_WARN("Failed to persist %s queue for %s", queue_kind, m->config.name);
    }
    return r;
}

static void module_on_connection_event(const EventData *event,
                                       ModuleId module_id,
                                       EventType connected_event)
{
    if (!event)
        return;

    BOOL connected = (event->type == connected_event);
    module_set_connected_internal(module_id, connected);
}

static void on_network_event(const EventData *e, void *u)
{
    (void)u;
    module_on_connection_event(e, MODULE_ID_NETWORK, EVENT_NETWORK_CONNECTED);
}

static void on_gps_event(const EventData *e, void *u)
{
    (void)u;
    module_on_connection_event(e, MODULE_ID_GPS, EVENT_GPS_CONNECTED);
}

static void on_tcp_event(const EventData *e, void *u)
{
    (void)u;
    module_on_connection_event(e, MODULE_ID_TCP, EVENT_TCP_CONNECTED);
}

static void on_sms_event(const EventData *e, void *u)
{
    (void)u;
    module_on_connection_event(e, MODULE_ID_SMS, EVENT_SMS_CONNECTED);
}

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/

size_t module_message_payload_len(const ModuleMessage *msg)
{
    if (!msg)
        return 0;
    return (size_t)msg->data_len;
}

void module_manager_update_uptime(ModuleId module_id)
{
    Module *m = module_get(module_id);
    if (m)
        m->status.task_uptime_sec = utils_get_uptime_seconds();
}

BOOL module_manager_monitor_tasks(UINT32 timeout_sec)
{
    UINT32 now = utils_get_uptime_seconds();

    for (UINT32 i = 0; i < g_module_count; i++) {
        Module *m = g_modules[i];

        if (!m || !m->config.enabled || !m->config.has_task || !m->status.initialized)
            continue;

        if ((now - m->status.task_uptime_sec) > timeout_sec) {
            LOG_ERROR("Task stalled: %s", m->config.name);
            return TRUE;
        }
    }

    return FALSE;
}

BOOL module_manager_loop_iteration(void)
{
    return module_manager_monitor_tasks(MODULE_TASK_TIMEOUT_SEC);
}

Result module_manager_persist_reboot_data(void)
{
    Result overall = RESULT_SUCCESS;

    LOG_INFO("Persisting module reboot data");

    for (UINT32 i = 0; i < g_module_count; i++) {
        Module *m = g_modules[i];
        if (!m || !m->config.enabled)
            continue;

        if (result_is_error(module_persist_queue_if_needed(m,
                                                           m->config.msg_q,
                                                           &m->config.msg_q_config,
                                                           "message"))) {
            overall = RESULT_ERROR;
        }

        if (result_is_error(module_persist_queue_if_needed(m,
                                                           m->config.urc_q,
                                                           &m->config.urc_q_config,
                                                           "URC"))) {
            overall = RESULT_ERROR;
        }
    }

    if (result_is_error(overall))
        LOG_WARN("Module reboot data persistence had errors");
    else
        LOG_INFO("Module reboot data saved");

    return overall;
}

void module_manager_set_connected(ModuleId module_id, BOOL connected)
{
    module_set_connected_internal(module_id, connected);
}

BOOL module_manager_get_connected(ModuleId module_id)
{
    Module *m = module_get(module_id);
    return m ? m->status.connected : FALSE;
}

BOOL module_manager_is_initialized(ModuleId module_id)
{
    Module *m = module_get(module_id);
    return m ? m->status.initialized : FALSE;
}

Module *module_manager_get_module(ModuleId module_id)
{
    return module_get(module_id);
}

const ModuleConfig *module_manager_get_config(ModuleId module_id)
{
    Module *m = module_get(module_id);
    return m ? &m->config : NULL;
}

Result module_manager_deinit(void)
{
    LOG_INFO("Deinitializing all modules");

    for (UINT32 i = 0; i < g_module_count; i++) {
        Module *m = g_modules[i];

        if (!m || !m->status.initialized || !m->config.deinit_fn)
            continue;

        LOG_DEBUG("Deinitializing %s", m->config.name);
        Result res = m->config.deinit_fn();
        if (result_is_error(res))
            LOG_WARN("Module %s deinit returned error", m->config.name);

        m->status.initialized = FALSE;
        m->status.connected = FALSE;
    }

    LOG_INFO("All modules deinitialized");
    return RESULT_SUCCESS;
}

/*---------------------------------------------------------------
 * Init helpers (bottom-up entry)
 *--------------------------------------------------------------*/

static void module_manager_init_status(void)
{
    UINT32 boot_uptime = utils_get_uptime_seconds();

    for (UINT32 i = 0; i < g_module_count; i++) {
        Module *m = g_modules[i];
        if (!m)
            continue;

        m->status.initialized     = FALSE;
        m->status.connected       = FALSE;
        m->status.task_uptime_sec = boot_uptime;
        m->config.msg_q           = NULL;
        m->config.urc_q           = NULL;
    }
}

Result module_manager_register_events(void)
{
    struct {
        EventType type;
        EventCallback fn;
    } events[] = {
        { EVENT_NETWORK_CONNECTED,    on_network_event },
        { EVENT_NETWORK_DISCONNECTED, on_network_event },
        { EVENT_GPS_CONNECTED,        on_gps_event     },
        { EVENT_GPS_DISCONNECTED,     on_gps_event     },
        { EVENT_TCP_CONNECTED,        on_tcp_event     },
        { EVENT_TCP_DISCONNECTED,     on_tcp_event     },
        { EVENT_SMS_CONNECTED,        on_sms_event     },
        { EVENT_SMS_DISCONNECTED,     on_sms_event     }
    };

    for (UINT32 i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
        Result r = event_manager_register(events[i].type,
                                          events[i].fn,
                                          NULL,
                                          "Module Manager");
        if (r == RESULT_SUCCESS || r == RESULT_ALREADY_INITIALIZED)
            continue;
        LOG_ERROR("Failed to register connection event");
        return r;
    }

    LOG_INFO("Connection events registered");
    return RESULT_SUCCESS;
}

Result module_manager_init(void)
{
    module_manager_init_status();

    if (module_manager_register_events() != RESULT_SUCCESS)
        LOG_WARN("Event registration failed, continuing");

    for (UINT32 round = 0; round < MODULE_INIT_RETRY_COUNT; round++) {
        UINT32 initialized = 0;

        if (round > 0) {
            LOG_WARN("Retrying module init (round %u)", round);
            utils_sleep_ms(MODULE_INIT_RETRY_INTERVAL_MS);
        }

        for (UINT32 i = 0; i < g_module_count; i++) {
            Module *m = g_modules[i];

            if (!m || !m->config.enabled || m->status.initialized || !m->config.init_fn) {
                initialized++;
                continue;
            }

            Result res = m->config.init_fn();
            if (!result_is_error(res)) {
                m->status.initialized = TRUE;
                m->status.last_error  = 0;

                if (m->config.has_task)
                    m->status.task_uptime_sec = utils_get_uptime_seconds();

                LOG_INFO("Module ready: %s", m->config.name);
                initialized++;
            } else {
                m->status.retries++;
                m->status.last_error = res;
                LOG_ERROR("%s init failed", m->config.name);

                if (m->config.deinit_fn)
                    m->config.deinit_fn();
            }
        }

        if (initialized == g_module_count)
            break;
    }

    for (UINT32 i = 0; i < g_module_count; i++) {
        Module *m = g_modules[i];
        if (m && m->config.required && !m->status.initialized) {
            LOG_ERROR("Required module failed: %s", m->config.name);
            return RESULT_ERROR;
        }
    }

    return RESULT_SUCCESS;
}
