/**
 * @file reset_handler.c
 * @brief Deferred soft/hard SoC reset with pre-boot persistence.
 */

#include "system/reset/reset_handler.h"
#include "common/event_manager.h"
#include "common/utils.h"
#include "system/time_utils.h"
#include "module/module_manager.h"
#include "system/reset/pre_boot_handler.h"
#include "functionality/sdk_functionality_system.h"

#include <string.h>

#define LOG_TAG          "RESET"
#define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

#define RESET_DEFER_DELAY_MS       200U
#define RESET_SOFT_STABILIZE_MS    500U
#define RESET_HARD_STABILIZE_MS    10000U
#define RESET_SOC_FAIL_TIMEOUT_MS  20000U
#define RESET_SOC_FAIL_POLL_MS     500U
#define RESET_SOURCE_BUF_LEN       (32U + 1U)

static const char k_reg_module[] = "Reset Handler";

typedef struct {
    char   source_name[RESET_SOURCE_BUF_LEN];
    UINT32 scheduled_start_ms;
    UINT8  pending;
    UINT8  executing;
    UINT8  type;
} DeferredResetState;

static DeferredResetState   g_deferred;
ResetHandlerLastSwResetInfo g_reset_handler_last_sw_reset;

static const char *deferred_source(void)
{
    return (g_deferred.source_name[0] != '\0') ? g_deferred.source_name : "SYSTEM";
}

static void persist_and_reset(ResetType kind, UINT32 stabilize_ms)
{
    UINT32 utc = 0;

    LOG_INFO("RESET: %s source=%s", reset_type_name(kind), deferred_source());
    time_utils_get_time(TIME_TYPE_UTC_UNIX, NULL, NULL, NULL, NULL, NULL, NULL, &utc);

    if (pre_boot_handler_save_before_soc_reset(kind, utc, deferred_source()) != RESULT_SUCCESS)
        LOG_WARN("RESET: pre-boot save failed");

    if (module_manager_persist_reboot_data() != RESULT_SUCCESS)
        LOG_WARN("RESET: module reboot-data persist failed");

    utils_sleep_ms(stabilize_ms);
    SDK_SYSTEM_RESET();
    LOG_ERROR("RESET: SDK_SYSTEM_RESET returned (unexpected)");
}

static void schedule_reset(ResetType type, const char *source_module)
{
    if (type != RESET_TYPE_SOFT && type != RESET_TYPE_HARD) {
        LOG_ERROR("RESET: invalid type %d", (int)type);
        return;
    }
    if (g_deferred.pending || g_deferred.executing) {
        LOG_WARN("RESET: ignored %s (in progress)", reset_type_name(type));
        return;
    }

    g_deferred.pending            = 1u;
    g_deferred.type               = (UINT8)type;
    g_deferred.scheduled_start_ms = utils_monotonic_ms_now();
    if (source_module && source_module[0] != '\0')
        utils_strncpy_safe(g_deferred.source_name, source_module, sizeof(g_deferred.source_name));
    else
        g_deferred.source_name[0] = '\0';

    LOG_INFO("RESET: scheduled %s in %u ms (source=%s)",
             reset_type_name(type), (unsigned)RESET_DEFER_DELAY_MS, deferred_source());
}

static void on_reset_event(const EventData *event, void *user_data)
{
    ResetType type;

    (void)user_data;
    if (!event)
        return;

    if (event->type == EVENT_RESET_SOFT)
        type = RESET_TYPE_SOFT;
    else if (event->type == EVENT_RESET_HARD)
        type = RESET_TYPE_HARD;
    else
        return;

    schedule_reset(type, event->source_module);
}

static Result register_reset_events(void)
{
    static const EventType events[] = { EVENT_RESET_SOFT, EVENT_RESET_HARD };

    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
        Result r = event_manager_register(events[i], on_reset_event, NULL, k_reg_module);
        if (r != RESULT_SUCCESS && r != RESULT_ALREADY_INITIALIZED)
            return r;
    }
    LOG_INFO("RESET: registered (defer %u ms)", (unsigned)RESET_DEFER_DELAY_MS);
    return RESULT_SUCCESS;
}

Result reset_handler_init(void)
{
    post_boot_handler_get_prior_reset_snapshot(&g_reset_handler_last_sw_reset);
    return register_reset_events();
}

Result reset_handler_request_reset(int reset_type)
{
    EventType ev;

    switch (reset_type) {
    case RESET_TYPE_SOFT: ev = EVENT_RESET_SOFT; break;
    case RESET_TYPE_HARD: ev = EVENT_RESET_HARD; break;
    default:
        return RESULT_INVALID_PARAM;
    }
    return event_manager_broadcast(ev, "reset_handler_request_reset", NULL, 0);
}

BOOL reset_handler_process_deferred_reset(void)
{
    ResetType type;
    UINT32 stabilize_ms;
    UINT32 fail_start;

    if (!g_deferred.pending)
        return FALSE;
    if (utils_monotonic_ms_elapsed(g_deferred.scheduled_start_ms) < RESET_DEFER_DELAY_MS)
        return FALSE;

    type = (ResetType)g_deferred.type;
    if (type != RESET_TYPE_SOFT && type != RESET_TYPE_HARD) {
        g_deferred.pending = 0u;
        return FALSE;
    }

    g_deferred.pending   = 0u;
    g_deferred.executing = 1u;
    stabilize_ms = (type == RESET_TYPE_SOFT) ? RESET_SOFT_STABILIZE_MS : RESET_HARD_STABILIZE_MS;
    persist_and_reset(type, stabilize_ms);

    fail_start = utils_monotonic_ms_now();
    for (;;) {
        if (utils_monotonic_ms_elapsed(fail_start) >= RESET_SOC_FAIL_TIMEOUT_MS) {
            g_deferred.executing = 0u;
            return TRUE;
        }
        utils_sleep_ms(RESET_SOC_FAIL_POLL_MS);
    }
}
