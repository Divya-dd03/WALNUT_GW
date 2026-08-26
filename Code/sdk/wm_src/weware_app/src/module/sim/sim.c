/**
  ******************************************************************************
  * @file    sim.c
  * @author  WheelsEye
  * @brief   SIM module implementation for the weware application.
  *          SIM presence polled + debounced by a task. Two detect sources:
  *          - Walnut modem via sdk_sim_get_status() (active)
  *          - SIM-detect GPIO, SIMCom hardware (on hold, SIM_DETECT_VIA_GPIO)
  *          PIN/ICCID helpers. No modem SIM URC or hotswap.
  ******************************************************************************
  */

// sdk
#include "wm_global.h"
#include "sdk_os.h"
#include "sdk_gpio.h"
#include "sdk_sim.h"
#include "sdk_log.h"

// app
#include "sim/sim.h"
#include "module/module_manager.h"
#include "common/event_manager.h"
#include "common/task_stats.h"

/*---------------------------------------------------------------
 * Configuration
 *--------------------------------------------------------------*/
/* Detect source: 1 = SIM-detect GPIO (SIMCom hardware, on hold),
 *                0 = Walnut modem via sdk_sim_get_status().        */
#ifndef SIM_DETECT_VIA_GPIO
#define SIM_DETECT_VIA_GPIO          (0U)
#endif

#define SIM_DETECT_POLL_MS           (1000U)
#define SIM_DETECT_DEBOUNCE_MATCHES  (3U)
#define SIM_DETECT_TASK_STACK        (4096U)

#if SIM_DETECT_VIA_GPIO
#define SIM_DETECT_GPIO_PIN          (117U)
#define SIM_GPIO_DIRECTION_INPUT     (0U)
#define SIM_GPIO_LEVEL_LOW           (0U)

/* GPIO level read when SIM tray is inserted — LOW if hardware pulls low on insert. */
#ifndef SIM_DETECT_INSERTED_LEVEL
#define SIM_DETECT_INSERTED_LEVEL SIM_GPIO_LEVEL_LOW
#endif
#endif /* SIM_DETECT_VIA_GPIO */

/*---------------------------------------------------------------
 * Static State
 *--------------------------------------------------------------*/
typedef struct {
    bool     initialized;
    bool     sim_available;
    bool     last_notified_state;
    bool     debounce_inserted;
    UINT32   debounce_matches;
} sim_runtime_t;

static sim_runtime_t g_sim = {
    .initialized         = false,
    .sim_available       = false,
    .last_notified_state = false,
    .debounce_inserted   = false,
    .debounce_matches    = 0,
};

static void                  *g_sim_detect_task = NULL;
/* Stack usage sampling for the SIM detect task (walnut addition; the
 * reference SIM manager does not sample stats). */
static TaskStats              g_sim_task_stats;
static weware_sim_status_cb_t g_sim_status_cb   = NULL;

/*---------------------------------------------------------------
 * Internal Helpers
 *--------------------------------------------------------------*/

static void sim_notify_sim_status_change(bool sim_available)
{
    if (g_sim.last_notified_state == sim_available) {
        sdk_debug_print("SIM state %s already notified - skip duplicate\r\n",
                     sim_available ? "AVAILABLE" : "UNAVAILABLE");
        return;
    }

    g_sim.last_notified_state = sim_available;

    /* Reference sim_notify_sim_status_change: broadcast to all listeners
     * (network restarts its radio on EVENT_SIM_AVAILABLE). The single-slot
     * callback is kept as a walnut extra for non-event consumers. */
    sdk_log_info("SIM broadcasting status: %s",
                 sim_available ? "EVENT_SIM_AVAILABLE" : "EVENT_SIM_UNAVAILABLE");
    event_manager_broadcast(sim_available ? EVENT_SIM_AVAILABLE : EVENT_SIM_UNAVAILABLE,
                            "SIM Manager", NULL, 0);
    if (g_sim_status_cb)
        g_sim_status_cb(sim_available);
}

#if SIM_DETECT_VIA_GPIO
/* SIMCom hardware: SIM presence from the SIM-detect GPIO. */
static SdkResult sim_sample_presence(bool *inserted)
{
    UINT32 level = SIM_GPIO_LEVEL_LOW;
    SdkResult result = sdk_gpio_get_level(SIM_DETECT_GPIO_PIN, &level);
    if (result != SDK_RESULT_SUCCESS)
        return result;

    sdk_debug_print("SIM Level=%u\r\n", (unsigned)level);
    *inserted = (level == SIM_DETECT_INSERTED_LEVEL);
    return SDK_RESULT_SUCCESS;
}
#else
/* Walnut modem: SIM presence from the modem SIM status API.
 * PRESENT (PIN locked) and READY count as inserted; ABSENT and
 * ERROR count as unavailable. */
static SdkResult sim_sample_presence(bool *inserted)
{
    SdkSimStatus status = SDK_SIM_ERROR;
    SdkResult result = weware_sim_query_modem_status(&status);
    if (result != SDK_RESULT_SUCCESS)
        return result;

    *inserted = (status == SDK_SIM_PRESENT) || (status == SDK_SIM_READY);
    return SDK_RESULT_SUCCESS;
}
#endif /* SIM_DETECT_VIA_GPIO */

static void sim_detect_task_entry(void *arg)
{
    (void)arg;

    sdk_log_info("SIM detect task started\r\n");

    for (;;) {
        /* Task-stall watchdog feed (module_manager_monitor_tasks) */
        module_manager_update_uptime(MODULE_ID_SIM);
        (void)task_stats_update_periodic(g_sim_detect_task, "SIM", MODULE_ID_SIM,
                                         &g_sim_task_stats, 0);

        bool inserted = false;
        if (sim_sample_presence(&inserted) == SDK_RESULT_SUCCESS) {
            if (inserted == g_sim.debounce_inserted) {
                if (g_sim.debounce_matches < SIM_DETECT_DEBOUNCE_MATCHES)
                    g_sim.debounce_matches++;
            } else {
                g_sim.debounce_inserted = inserted;
                g_sim.debounce_matches  = 1U;
            }

            if (g_sim.debounce_matches >= SIM_DETECT_DEBOUNCE_MATCHES)
                weware_sim_set_sim_available(g_sim.debounce_inserted);
        }

        sdk_task_sleep(SIM_DETECT_POLL_MS);
    }
}

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/

SdkResult weware_sim_init(void)
{
    sdk_log_info("Initializing SIM module\r\n");

    if (g_sim.initialized) {
        sdk_log_warning("SIM module already initialized\r\n");
        return SDK_RESULT_SUCCESS;
    }

#if SIM_DETECT_VIA_GPIO
    if (sdk_gpio_set_direction(SIM_DETECT_GPIO_PIN, SIM_GPIO_DIRECTION_INPUT) == SDK_RESULT_SUCCESS)
        sdk_debug_print("SIM detect GPIO %u configured as input\r\n", (unsigned)SIM_DETECT_GPIO_PIN);
    else
        sdk_log_error("Failed to configure SIM detect GPIO %u\r\n", (unsigned)SIM_DETECT_GPIO_PIN);
#endif

    if (g_sim_detect_task == NULL) {
        g_sim_detect_task = sdk_task_create(sim_detect_task_entry, NULL, "SIMDET",
                                            NULL, SIM_DETECT_TASK_STACK,
                                            TP_TIMED_ACTIVITY);
        if (g_sim_detect_task == NULL) {
            sdk_log_error("Failed to create SIM detect task\r\n");
            return SDK_RESULT_ERROR;
        }
    }

    g_sim.initialized = true;
    return SDK_RESULT_SUCCESS;
}

SdkResult weware_sim_deinit(void)
{
    if (!g_sim.initialized)
        return SDK_RESULT_SUCCESS;

    if (g_sim_detect_task != NULL) {
        sdk_task_delete(g_sim_detect_task);
        g_sim_detect_task = NULL;
    }

    g_sim.debounce_matches    = 0;
    g_sim.debounce_inserted   = false;
    g_sim.sim_available       = false;
    g_sim.last_notified_state = false;
    g_sim.initialized         = false;

    return SDK_RESULT_SUCCESS;
}

SdkSimStatus weware_sim_read_status(void)
{
    SdkSimStatus status = SDK_SIM_ERROR;
    if (weware_sim_query_modem_status(&status) != SDK_RESULT_SUCCESS)
        return SDK_SIM_ERROR;
    return status;
}

SdkResult weware_sim_query_modem_status(SdkSimStatus *status)
{
    /* One-shot: a persistently failing status API (modem not answering)
     * logs once, re-arms once it reads OK. */
    static bool s_status_err_logged = false;

    if (!status)
        return SDK_RESULT_INVALID_PARAM;

    SdkResult result = sdk_sim_get_status(status);

    if (result == SDK_RESULT_NOT_SUPPORTED) {
        sdk_log_warning("SIM status API not supported\r\n");
        return SDK_RESULT_NOT_SUPPORTED;
    }

    if (result == SDK_RESULT_SUCCESS) {
        s_status_err_logged = false;
        sdk_log_info("SIM modem status=%d\r\n", (int)*status);
        return SDK_RESULT_SUCCESS;
    }

    if (!s_status_err_logged) {
        sdk_log_error("SIM status API failed (result=%d)\r\n", (int)result);
        s_status_err_logged = true;
    }
    return result;
}

SdkResult weware_sim_check_sim_ready(bool *api_error_out)
{
    /* One-shot: this is polled during registration; a persistently unreadable
     * pin-status (SIM/modem not answering) logs once, re-arms once it reads OK. */
    static bool s_pin_status_err_logged = false;

    if (api_error_out) *api_error_out = false;

    UINT8     cpin   = 0;
    SdkResult result = sdk_sim_get_pin_status(&cpin);

    if (result == SDK_RESULT_NOT_SUPPORTED)
        return SDK_RESULT_NOT_SUPPORTED;

    if (result == SDK_RESULT_SUCCESS) {
        s_pin_status_err_logged = false;
        if (cpin == 0) {
            sdk_debug_print("SIM ready (cpin=%u)\r\n", (unsigned)cpin);
            return SDK_RESULT_SUCCESS;
        }
        sdk_debug_print("SIM not ready (cpin=%u)\r\n", (unsigned)cpin);
        return SDK_RESULT_ERROR;
    }

    if (!s_pin_status_err_logged) {
        sdk_log_error("SIM pin status API failed (result=%d, cpin=%u)\r\n",
                     (int)result, (unsigned)cpin);
        s_pin_status_err_logged = true;
    }
    if (api_error_out) *api_error_out = true;
    return SDK_RESULT_ERROR;
}

bool weware_sim_get_sim_status(void)
{
    return g_sim.sim_available;
}

void weware_sim_set_sim_available(bool sim_available)
{
    if (g_sim.sim_available == sim_available)
        return;

    sdk_debug_print("SIM status: %s -> %s\r\n",
                 g_sim.sim_available ? "AVAILABLE" : "UNAVAILABLE",
                 sim_available       ? "AVAILABLE" : "UNAVAILABLE");

    /* SIM pulled at runtime (tamper/fault). Edge-only. */
    if (g_sim.sim_available && !sim_available)
        sdk_log_error("SIM removed at runtime\r\n");

    g_sim.sim_available = sim_available;
    /* Reference: SIM presence IS the module's connected state. */
    module_manager_set_connected(MODULE_ID_SIM, sim_available ? TRUE : FALSE);
    sim_notify_sim_status_change(sim_available);
}

SdkResult weware_sim_get_sim_number(char *iccid)
{
    if (!iccid)
        return SDK_RESULT_INVALID_PARAM;

    SdkResult result = sdk_sim_get_iccid(iccid, WEWARE_SIM_ICCID_BUFFER_SIZE);
    if (result == SDK_RESULT_NOT_SUPPORTED) {
        iccid[0] = '\0';
        sdk_log_warning("ICCID retrieval not supported\r\n");
        return SDK_RESULT_NOT_SUPPORTED;
    }
    if (result == SDK_RESULT_SUCCESS) {
        sdk_debug_print("SIM ICCID: %s\r\n", iccid);
        return SDK_RESULT_SUCCESS;
    }

    iccid[0] = '\0';
    sdk_log_error("Failed to get SIM ICCID\r\n");
    return SDK_RESULT_ERROR;
}

void weware_sim_register_status_callback(weware_sim_status_cb_t callback)
{
    g_sim_status_cb = callback;
}
