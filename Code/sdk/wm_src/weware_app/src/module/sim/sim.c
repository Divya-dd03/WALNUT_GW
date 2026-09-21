/**
  ******************************************************************************
  * @file    sim.c
  * @author  WheelsEye
  * @brief   SIM module implementation for the weware application.
  *          SIM presence polled + debounced by a task. Two detect sources:
  *          - Walnut modem via wm_sdk_sim_get_status() (active)
  *          - SIM-detect GPIO, SIMCom hardware (on hold, SIM_DETECT_VIA_GPIO)
  *          PIN/ICCID helpers. No modem SIM URC or hotswap.
  ******************************************************************************
  */

// sdk
#include "wm_global.h"
#include "wm_sdk_os.h"
#include "wm_sdk_gpio.h"
#include "wm_sdk_sim.h"
#include "wm_sdk_log.h"

// app
#include "sim/sim.h"
#include "module/module_manager.h"
#include "module/network/network.h"  /* radio-restart gate, see detect task */
#include "common/event_manager.h"
#include "common/task_stats.h"

#define LOG_TAG "SIM"
#define LOG_MODULE_LEVEL LOG_LEVEL_DEBUG
#include "module/log/log.h"

/*---------------------------------------------------------------
 * Configuration
 *--------------------------------------------------------------*/
/* Detect source: 1 = SIM-detect GPIO (SIMCom hardware, on hold),
 *                0 = Walnut modem via wm_sdk_sim_get_status().        */
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
        LOG_DEBUG("SIM state %s already notified - skip duplicate",
                     sim_available ? "AVAILABLE" : "UNAVAILABLE");
        return;
    }

    g_sim.last_notified_state = sim_available;

    /* Reference sim_notify_sim_status_change: broadcast to all listeners
     * (network restarts its radio on EVENT_SIM_AVAILABLE). The single-slot
     * callback is kept as a walnut extra for non-event consumers. */
    LOG_INFO("SIM broadcasting status: %s",
                 sim_available ? "EVENT_SIM_AVAILABLE" : "EVENT_SIM_UNAVAILABLE");
    event_manager_broadcast(sim_available ? EVENT_SIM_AVAILABLE : EVENT_SIM_UNAVAILABLE,
                            "SIM Manager", NULL, 0);
    if (g_sim_status_cb)
        g_sim_status_cb(sim_available);
}

#if SIM_DETECT_VIA_GPIO
/* SIMCom hardware: SIM presence from the SIM-detect GPIO. */
static wm_SdkResult sim_sample_presence(bool *inserted)
{
    UINT32 level = SIM_GPIO_LEVEL_LOW;
    wm_SdkResult result = wm_sdk_gpio_get_level(SIM_DETECT_GPIO_PIN, &level);
    if (result != WM_SDK_RESULT_SUCCESS)
        return result;

    LOG_DEBUG("SIM Level=%u", (unsigned)level);
    *inserted = (level == SIM_DETECT_INSERTED_LEVEL);
    return WM_SDK_RESULT_SUCCESS;
}
#else
/* Walnut modem: SIM presence from the modem SIM status API.
 * PRESENT (PIN locked) and READY count as inserted; ABSENT and
 * ERROR count as unavailable. */
static wm_SdkResult sim_sample_presence(bool *inserted)
{
    wm_SdkSimStatus status = WM_SDK_SIM_ERROR;
    wm_SdkResult result = weware_sim_query_modem_status(&status);
    if (result != WM_SDK_RESULT_SUCCESS)
        return result;

    *inserted = (status == WM_SDK_SIM_PRESENT) || (status == WM_SDK_SIM_READY);
    return WM_SDK_RESULT_SUCCESS;
}
#endif /* SIM_DETECT_VIA_GPIO */

static void sim_detect_task_entry(void *arg)
{
    (void)arg;

    LOG_INFO("SIM detect task started");

    for (;;) {
        /* Task-stall watchdog feed (module_manager_monitor_tasks) */
        module_manager_update_uptime(MODULE_ID_SIM);
        (void)task_stats_update_periodic(g_sim_detect_task, "SIM", MODULE_ID_SIM,
                                         &g_sim_task_stats, 0);

        /*
         * Skip sampling while the network module is cycling the radio.
         *
         * The reference reads SIM presence from a hardware SIM-detect GPIO
         * (pin 117, a tray switch), so AT+CFUN=0 cannot affect it and its
         * "SIM present" signal is stable from boot. Walnut asks the MODEM
         * instead (wm_sdk_sim_get_status), and with the radio down that answers
         * WM_SDK_SIM_ABSENT. RESTART_CFUN holds the radio off for
         * NETWORK_CFUN_OFF_SETTLE_MS + NETWORK_CFUN_ON_SETTLE_MS = 5 s, while
         * this task polls at 1 s and needs 3 matches - so every radio restart
         * used to fake a SIM removal: EVENT_SIM_UNAVAILABLE to every listener,
         * and for SMS a cleared config latch plus a full reconfigure (delete-
         * all included) issued exactly when the radio is down and those AT
         * commands fail. Holding the sample here restores the reference's
         * behaviour without needing the detect GPIO.
         */
        if (weware_network_get_state() == NETWORK_STATE_RESTART_CFUN) {
            wm_sdk_task_sleep(SIM_DETECT_POLL_MS);
            continue;
        }

        bool inserted = false;
        if (sim_sample_presence(&inserted) == WM_SDK_RESULT_SUCCESS) {
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

        wm_sdk_task_sleep(SIM_DETECT_POLL_MS);
    }
}

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/

wm_SdkResult weware_sim_init(void)
{
    LOG_INFO("Initializing SIM module");

    if (g_sim.initialized) {
        LOG_WARN("SIM module already initialized");
        return WM_SDK_RESULT_SUCCESS;
    }

#if SIM_DETECT_VIA_GPIO
    if (wm_sdk_gpio_set_direction(SIM_DETECT_GPIO_PIN, SIM_GPIO_DIRECTION_INPUT) == WM_SDK_RESULT_SUCCESS)
        LOG_DEBUG("SIM detect GPIO %u configured as input", (unsigned)SIM_DETECT_GPIO_PIN);
    else
        LOG_ERROR("Failed to configure SIM detect GPIO %u", (unsigned)SIM_DETECT_GPIO_PIN);
#endif

    if (g_sim_detect_task == NULL) {
        g_sim_detect_task = wm_sdk_task_create(sim_detect_task_entry, NULL, "SIMDET",
                                            NULL, SIM_DETECT_TASK_STACK,
                                            TP_TIMED_ACTIVITY);
        if (g_sim_detect_task == NULL) {
            LOG_ERROR("Failed to create SIM detect task");
            return WM_SDK_RESULT_ERROR;
        }
    }

    g_sim.initialized = true;
    return WM_SDK_RESULT_SUCCESS;
}

wm_SdkResult weware_sim_deinit(void)
{
    if (!g_sim.initialized)
        return WM_SDK_RESULT_SUCCESS;

    if (g_sim_detect_task != NULL) {
        wm_sdk_task_delete(g_sim_detect_task);
        g_sim_detect_task = NULL;
    }

    g_sim.debounce_matches    = 0;
    g_sim.debounce_inserted   = false;
    g_sim.sim_available       = false;
    g_sim.last_notified_state = false;
    g_sim.initialized         = false;

    return WM_SDK_RESULT_SUCCESS;
}

wm_SdkSimStatus weware_sim_read_status(void)
{
    wm_SdkSimStatus status = WM_SDK_SIM_ERROR;
    if (weware_sim_query_modem_status(&status) != WM_SDK_RESULT_SUCCESS)
        return WM_SDK_SIM_ERROR;
    return status;
}

wm_SdkResult weware_sim_query_modem_status(wm_SdkSimStatus *status)
{
    /* One-shot: a persistently failing status API (modem not answering)
     * logs once, re-arms once it reads OK. */
    static bool s_status_err_logged = false;

    if (!status)
        return WM_SDK_RESULT_INVALID_PARAM;

    wm_SdkResult result = wm_sdk_sim_get_status(status);

    if (result == WM_SDK_RESULT_NOT_SUPPORTED) {
        LOG_WARN("SIM status API not supported");
        return WM_SDK_RESULT_NOT_SUPPORTED;
    }

    if (result == WM_SDK_RESULT_SUCCESS) {
        s_status_err_logged = false;
        LOG_INFO("SIM modem status=%d", (int)*status);
        UINT8 out = -1;
        if (wm_sdk_sim_get_pin_status(&out) == WM_SDK_RESULT_SUCCESS)
            LOG_INFO("wm_sdk_sim_get_pin_status: %d", out);
        else LOG_ERROR("wm_sdk_sim_get_pin_status: %d", out);
        return WM_SDK_RESULT_SUCCESS;
    }

    if (!s_status_err_logged) {
        LOG_ERROR("SIM status API failed (result=%d)", (int)result);
        s_status_err_logged = true;
    }
    return result;
}

wm_SdkResult weware_sim_check_sim_ready(bool *api_error_out)
{
    /* One-shot: this is polled during registration; a persistently unreadable
     * pin-status (SIM/modem not answering) logs once, re-arms once it reads OK. */
    static bool s_pin_status_err_logged = false;

    if (api_error_out) *api_error_out = false;

    UINT8     cpin   = 0;
    wm_SdkResult result = wm_sdk_sim_get_pin_status(&cpin); // sim detect pin status

    if (result == WM_SDK_RESULT_NOT_SUPPORTED)
        return WM_SDK_RESULT_NOT_SUPPORTED;

    if (result == WM_SDK_RESULT_SUCCESS) {
        s_pin_status_err_logged = false;
        if (cpin == 0) {
            LOG_DEBUG("SIM ready (cpin=%u)", (unsigned)cpin);
            return WM_SDK_RESULT_SUCCESS;
        }
        LOG_DEBUG("SIM not ready (cpin=%u)", (unsigned)cpin);
        return WM_SDK_RESULT_ERROR;
    }

    if (!s_pin_status_err_logged) {
        LOG_ERROR("SIM pin status API failed (result=%d, cpin=%u)",
                     (int)result, (unsigned)cpin);
        s_pin_status_err_logged = true;
    }
    if (api_error_out) *api_error_out = true;
    return WM_SDK_RESULT_ERROR;
}

bool weware_sim_get_sim_status(void)
{
    return g_sim.sim_available;
}

void weware_sim_set_sim_available(bool sim_available)
{
    if (g_sim.sim_available == sim_available)
        return;

    LOG_DEBUG("SIM status: %s -> %s",
                 g_sim.sim_available ? "AVAILABLE" : "UNAVAILABLE",
                 sim_available       ? "AVAILABLE" : "UNAVAILABLE");

    /* SIM pulled at runtime (tamper/fault). Edge-only. */
    if (g_sim.sim_available && !sim_available)
        LOG_ERROR("SIM removed at runtime");

    g_sim.sim_available = sim_available;
    /* Reference: SIM presence IS the module's connected state. */
    module_manager_set_connected(MODULE_ID_SIM, sim_available ? TRUE : FALSE);
    sim_notify_sim_status_change(sim_available);
}

wm_SdkResult weware_sim_get_sim_number(char *iccid)
{
    if (!iccid)
        return WM_SDK_RESULT_INVALID_PARAM;

    wm_SdkResult result = wm_sdk_sim_get_iccid(iccid, WEWARE_SIM_ICCID_BUFFER_SIZE);
    if (result == WM_SDK_RESULT_NOT_SUPPORTED) {
        iccid[0] = '\0';
        LOG_WARN("ICCID retrieval not supported");
        return WM_SDK_RESULT_NOT_SUPPORTED;
    }
    if (result == WM_SDK_RESULT_SUCCESS) {
        LOG_DEBUG("SIM ICCID: %s", iccid);
        return WM_SDK_RESULT_SUCCESS;
    }

    iccid[0] = '\0';
    LOG_ERROR("Failed to get SIM ICCID");
    return WM_SDK_RESULT_ERROR;
}

void weware_sim_register_status_callback(weware_sim_status_cb_t callback)
{
    g_sim_status_cb = callback;
}
