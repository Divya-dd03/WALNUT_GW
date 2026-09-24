/**
  ******************************************************************************
  * @file    network.c
  * @author  WheelsEye
  * @brief   Network module implementation for the weware application.
  *          State-machine driven bring-up with automatic reconnection:
  *          INIT -> CHECK_SIM -> SIM_INSERT -> CHECK_CTZU [-> SET_CTZU ->
  *          RESTART_CFUN] -> CHECK_REGISTRATION -> CHECK_GPRS_REGISTRATION ->
  *          CHECK_LTE_ATTACHMENT -> SETUP_PDP -> ACTIVATE_PDP -> GET_IP ->
  *          CONNECTED -> DISCONNECTED -> INIT.
  *          Per-state timeouts escalate to RESTART_CFUN; a not-connected
  *          overall timeout soft-resets the system. Network URCs are fanned
  *          out by the URC processor into this module's urc_q (queue_manager)
  *          and drained by the state-machine task.
  ******************************************************************************
  */

#include <string.h>

// sdk
#include "wm_global.h"
#include "wm_sdk_os.h"
#include "wm_sdk_network.h"
#include "wm_sdk_wm.h"
#include "wm_sdk_system.h"
#include "wm_sdk_log.h"

// app
#include "network/network.h"
#include "sim/sim.h"
#include "module/network/network_config.h"
#include "module/module_manager.h"
#include "common/queue_manager.h"
#include "common/event_manager.h"
#include "common/task_stats.h"

#define LOG_TAG "NETWORK"
#define LOG_MODULE_LEVEL LOG_LEVEL_DEBUG
#include "module/log/log.h"

/*---------------------------------------------------------------
 * Configuration
 *--------------------------------------------------------------*/
#ifndef WEWARE_NETWORK_PDP_TYPE
#define WEWARE_NETWORK_PDP_TYPE          "IP"
#endif
/* APN and PDP CID come from g_network_config (persisted NetworkConfig). */
#define WEWARE_NETWORK_CTZU_VALUE        (1U)     /* auto time-zone update on  */

/* Connected AND CSQ >= this => stable. 0 disables the signal check. */
#ifndef WEWARE_NETWORK_MIN_CSQ
#define WEWARE_NETWORK_MIN_CSQ           (20U)
#endif

#define NETWORK_TASK_STACK               (8192U)
#define NETWORK_TASK_INTERVAL_MS         (100U)
/* URC queue capacity lives in the registry (module_config.c NET_URC_Q) */

/* CFUN restart pacing */
#define NETWORK_CFUN_OFF_SETTLE_MS       (2000U)  /* dwell at CFUN=0           */
#define NETWORK_CFUN_ON_SETTLE_MS        (3000U)  /* radio settle after CFUN=1 */

/* Connected state (reference values): grace after entering CONNECTED before
 * the first health check, then the 4-way (CREG+CGREG+CGATT+IP) check interval.
 * URCs remain the fast disconnect path. */
#define NETWORK_CONNECTED_STABILIZE_MS   (10000U)
#define NETWORK_HEALTH_CHECK_INTERVAL_MS (65000U)

/* Refresh the radio snapshot (CSQ/serving cell) this often while connected */
#define NETWORK_RADIO_REFRESH_MS         (30000U)

/* Per-state timeouts (0 = no timeout), reference values. Timeout escalates to
 * RESTART_CFUN, except RESTART_CFUN itself which falls back to INIT. The
 * reference table has no SIM_INSERT / DISCONNECTED rows (no state timeout
 * there; the overall timeout still covers them). */
#define NETWORK_TIMEOUT_CHECK_SIM_MS     (60000U)
#define NETWORK_TIMEOUT_CHECK_CTZU_MS    (60000U)
#define NETWORK_TIMEOUT_SET_CTZU_MS      (30000U)
#define NETWORK_TIMEOUT_CHECK_REG_MS     (150000U)
#define NETWORK_TIMEOUT_CHECK_GPRS_MS    (60000U)
#define NETWORK_TIMEOUT_CHECK_LTE_MS     (60000U)
#define NETWORK_TIMEOUT_SETUP_PDP_MS     (30000U)
#define NETWORK_TIMEOUT_ACTIVATE_PDP_MS  (30000U)
#define NETWORK_TIMEOUT_GET_IP_MS        (30000U)
#define NETWORK_TIMEOUT_RESTART_CFUN_MS  (60000U)

/* Not connected for this long (outside CHECK_SIM) => soft reset. */
#define NETWORK_OVERALL_TIMEOUT_MS       (600000U)

/*---------------------------------------------------------------
 * Internal Types
 *--------------------------------------------------------------*/
/* State handlers return Result (reference network_ops): RESULT_SUCCESS =
 * transition, RESULT_ERROR = error path, anything else (RESULT_BUSY) = stay.
 * Dispatched by the NW_HANDLE_STATE() macro below (reference
 * network_manager.c). */

/* Sub-steps of the non-blocking CFUN restart sequence */
typedef enum {
    CFUN_STEP_IDLE = 0,       /* nothing done yet: send CFUN=0        */
    CFUN_STEP_WAIT_OFF,       /* dwell at CFUN=0, then send CFUN=1    */
    CFUN_STEP_WAIT_ON,        /* radio settling after CFUN=1          */
} CfunStep;

typedef struct {
    bool                 initialized;
    NetworkState         state;
    bool                 connected;
    bool                 stable_network;
    NetworkRadioSnapshot radio;

    /* PDP type is a walnut extra (reference hardcodes "IP"); APN/user/pass/cid
     * live in g_network_config (reference NetworkConfig, persisted). */
    char                 pdp_type[16];

    /* timing (wm_sdk_get_ticks() ms, wrap-safe via unsigned subtraction) */
    UINT32               state_entry_tick;
    UINT32               not_connected_since;   /* overall-timeout anchor */
    UINT32               last_radio_refresh_tick;

    CfunStep             cfun_step;
    UINT32               cfun_step_tick;
} network_runtime_t;

/*---------------------------------------------------------------
 * Static State
 *--------------------------------------------------------------*/
/* Configuration storage (reference: defined in network_manager.c). Filled by
 * config_load_from_file before init; defaults applied in init when empty. */
NetworkConfig g_network_config;

static network_runtime_t g_network = {
    .initialized = false,
    .state       = NETWORK_STATE_INIT,
    .connected   = false,
    .pdp_type    = WEWARE_NETWORK_PDP_TYPE,
};

static void *g_network_task   = NULL;
/* Stack usage sampling for the network task (reference: g_network.task_stats) */
static TaskStats g_network_task_stats;
static weware_network_status_cb_t g_network_status_cb = NULL;

/* Per-state timeout table (0 = no timeout for that state) */
static const struct {
    NetworkState state;
    UINT32       timeout_ms;
} g_network_state_timeouts[] = {
    {NETWORK_STATE_CHECK_SIM,               NETWORK_TIMEOUT_CHECK_SIM_MS},
    {NETWORK_STATE_CHECK_CTZU,              NETWORK_TIMEOUT_CHECK_CTZU_MS},
    {NETWORK_STATE_SET_CTZU,                NETWORK_TIMEOUT_SET_CTZU_MS},
    {NETWORK_STATE_CHECK_REGISTRATION,      NETWORK_TIMEOUT_CHECK_REG_MS},
    {NETWORK_STATE_CHECK_GPRS_REGISTRATION, NETWORK_TIMEOUT_CHECK_GPRS_MS},
    {NETWORK_STATE_CHECK_LTE_ATTACHMENT,    NETWORK_TIMEOUT_CHECK_LTE_MS},
    {NETWORK_STATE_SETUP_PDP,               NETWORK_TIMEOUT_SETUP_PDP_MS},
    {NETWORK_STATE_ACTIVATE_PDP,            NETWORK_TIMEOUT_ACTIVATE_PDP_MS},
    {NETWORK_STATE_GET_IP,                  NETWORK_TIMEOUT_GET_IP_MS},
    {NETWORK_STATE_RESTART_CFUN,            NETWORK_TIMEOUT_RESTART_CFUN_MS},
};

/*---------------------------------------------------------------
 * Helpers
 *--------------------------------------------------------------*/
const char *weware_network_state_to_string(NetworkState state)
{
    switch (state) {
        case NETWORK_STATE_INIT:                    return "INIT";
        case NETWORK_STATE_CHECK_SIM:               return "CHECK_SIM";
        case NETWORK_STATE_SIM_INSERT:              return "SIM_INSERT";
        case NETWORK_STATE_SIM_REMOVE:              return "SIM_REMOVE";
        case NETWORK_STATE_CHECK_CTZU:              return "CHECK_CTZU";
        case NETWORK_STATE_SET_CTZU:                return "SET_CTZU";
        case NETWORK_STATE_CHECK_REGISTRATION:      return "CHECK_REGISTRATION";
        case NETWORK_STATE_CHECK_GPRS_REGISTRATION: return "CHECK_GPRS_REGISTRATION";
        case NETWORK_STATE_CHECK_LTE_ATTACHMENT:    return "CHECK_LTE_ATTACHMENT";
        case NETWORK_STATE_SETUP_PDP:               return "SETUP_PDP";
        case NETWORK_STATE_ACTIVATE_PDP:            return "ACTIVATE_PDP";
        case NETWORK_STATE_GET_IP:                  return "GET_IP";
        case NETWORK_STATE_CONNECTED:               return "CONNECTED";
        case NETWORK_STATE_DISCONNECTED:            return "DISCONNECTED";
        case NETWORK_STATE_ERROR:                   return "ERROR";
        case NETWORK_STATE_RESTART_CFUN:            return "RESTART_CFUN";
        default:                                    return "UNKNOWN";
    }
}

/**
 * While in RESTART_CFUN, only INIT / ERROR (or staying put) may replace the
 * state, so an async URC or SIM event cannot abort a half-done CFUN cycle.
 */
static void network_set_state(NetworkState new_state)
{
    if (g_network.state == NETWORK_STATE_RESTART_CFUN &&
        new_state != NETWORK_STATE_RESTART_CFUN &&
        new_state != NETWORK_STATE_INIT &&
        new_state != NETWORK_STATE_ERROR) {
        LOG_DEBUG("NET state %s blocked (RESTART_CFUN active)",
                        weware_network_state_to_string(new_state));
        return;
    }
    g_network.state = new_state;
}

#define NW_SET_STATE(new_state) network_set_state(new_state)

/* Map a handler outcome onto the per-state success/error/busy transitions
 * (reference network_manager.c NW_HANDLE_STATE, verbatim) */
#define NW_HANDLE_STATE(result, on_success, on_error, on_busy) \
    do { \
        Result _nw_r = (result); \
        if (_nw_r == RESULT_SUCCESS) \
            network_set_state(on_success); \
        else if (_nw_r == RESULT_ERROR) \
            network_set_state(on_error); \
        else \
            network_set_state(on_busy); \
    } while (0)

/*---------------------------------------------------------------
 * Radio snapshot / stable-network
 *--------------------------------------------------------------*/
static void network_refresh_radio_info(bool force)
{
    UINT32 now = wm_sdk_get_ticks();

    if (!force) {
        if (!g_network.connected)
            return;                      /* CSQ/CPSI hit the modem - connected only */
        if (g_network.last_radio_refresh_tick != 0U &&
            (now - g_network.last_radio_refresh_tick) < NETWORK_RADIO_REFRESH_MS)
            return;
    }
    g_network.last_radio_refresh_tick = (now == 0U) ? 1U : now;

    wm_SdkNetworkGpsRadioInfo info;
    memset(&info, 0, sizeof(info));
    if (wm_sdk_network_get_gps_radio_info(&info) != WM_SDK_RESULT_SUCCESS || !info.valid) {
        g_network.radio.valid = false;
        return;
    }

    g_network.radio.signal_strength = (info.csq > 31) ? 0 : info.csq;
    g_network.radio.mcc             = info.mcc;
    g_network.radio.mnc             = info.mnc;
    g_network.radio.lac             = info.lac;
    g_network.radio.cell_id         = info.cell_id;
    g_network.radio.valid           = true;
}

/** Set stable_network from the cached snapshot (call after a connect refresh). */
static void network_apply_stable_from_radio(void)
{
    if (!g_network.connected) {
        g_network.stable_network = false;
        return;
    }
#if (WEWARE_NETWORK_MIN_CSQ == 0U)
    g_network.stable_network = true;
#else
    g_network.stable_network =
        (g_network.radio.valid &&
         g_network.radio.signal_strength >= (int)WEWARE_NETWORK_MIN_CSQ);

    /* Connect-edge only, so this cannot spam the log. Distinguish
     * no-signal/unknown (CSQ 0) from weak-but-present. */
    if (g_network.radio.valid && !g_network.stable_network) {
        if (g_network.radio.signal_strength == 0)
            LOG_ERROR("NET no signal (csq=0, min=%u)", WEWARE_NETWORK_MIN_CSQ);
        else
            LOG_ERROR("NET low signal (csq=%d, min=%u)",
                          g_network.radio.signal_strength, WEWARE_NETWORK_MIN_CSQ);
    }
#endif
}

/*---------------------------------------------------------------
 * Connected-flag edge handling
 *--------------------------------------------------------------*/
static void network_set_connected(bool connected)
{
    if (connected == g_network.connected)
        return;

    LOG_INFO("NET connection %s", connected ? "UP" : "DOWN");
    g_network.connected = connected;

    if (connected) {
        network_refresh_radio_info(true);
        network_apply_stable_from_radio();
        if (g_network.radio.valid)
            LOG_INFO("NET status: csq=%d mcc=%d mnc=%d lac=0x%04X cell=0x%08X stable=%d",
                         g_network.radio.signal_strength,
                         g_network.radio.mcc, g_network.radio.mnc,
                         (unsigned)g_network.radio.lac,
                         (unsigned)g_network.radio.cell_id,
                         g_network.stable_network ? 1 : 0);
        else
            LOG_WARN("NET status: radio info unavailable");
    } else {
        g_network.stable_network = false;
        g_network.not_connected_since = wm_sdk_get_ticks();
    }

    if (g_network_status_cb)
        g_network_status_cb(connected);
}

/*---------------------------------------------------------------
 * State Handlers
 *--------------------------------------------------------------*/
static Result network_state_handle_check_sim(void)
{
    /* Debounced presence from the SIM module's detect task */
    return weware_sim_get_sim_status() ? RESULT_SUCCESS : RESULT_BUSY;
}

static Result network_state_handle_sim_insert(void)
{
    /* SIM present: wait for CPIN readiness before touching the network */
    wm_SdkResult result = weware_sim_check_sim_ready(NULL);

    if (result == WM_SDK_RESULT_SUCCESS)
        return RESULT_SUCCESS;
    if (result == WM_SDK_RESULT_NOT_SUPPORTED)
        return RESULT_SUCCESS;   /* cannot verify - assume ready */

    /* PIN pending or modem not answering: keep polling until the state
     * timeout escalates to RESTART_CFUN. */
    return RESULT_BUSY;
}

static Result network_state_handle_sim_remove(void)
{
    network_set_connected(false);
    return RESULT_SUCCESS;
}

static Result network_state_handle_check_ctzu(void)
{
    UINT32 ctzu = 0;
    wm_SdkResult result = wm_sdk_network_get_ctzu(&ctzu);

    if (result == WM_SDK_RESULT_NOT_SUPPORTED)
        return RESULT_SUCCESS;   /* skip the CTZU stage entirely */
    if (result != WM_SDK_RESULT_SUCCESS)
        return RESULT_BUSY;      /* modem busy - retry */

    /* Mismatch => busy path routes to SET_CTZU */
    return (ctzu == WEWARE_NETWORK_CTZU_VALUE) ? RESULT_SUCCESS : RESULT_BUSY;
}

static Result network_state_handle_set_ctzu(void)
{
    if (wm_sdk_network_set_ctzu(WEWARE_NETWORK_CTZU_VALUE) != WM_SDK_RESULT_SUCCESS)
        return RESULT_BUSY;

    LOG_INFO("NET CTZU=%u set, restarting CFUN to apply", WEWARE_NETWORK_CTZU_VALUE);
    return RESULT_SUCCESS;       /* success path routes to RESTART_CFUN */
}

static Result network_reg_status_to_result(wm_SdkResult api_result, wm_SdkNetRegStatus status)
{
    if (api_result != WM_SDK_RESULT_SUCCESS)
        return RESULT_BUSY;

    switch (status) {
        case WM_SDK_NET_REG_HOME:
        case WM_SDK_NET_REG_ROAMING:
            return RESULT_SUCCESS;
        case WM_SDK_NET_REG_DENIED:
            return RESULT_ERROR;
        default:                    /* not registered / searching / unknown */
            return RESULT_BUSY;
    }
}

static Result network_state_handle_check_registration(void)
{
    wm_SdkNetRegStatus status = WM_SDK_NET_REG_NOT_REGISTERED;
    return network_reg_status_to_result(wm_sdk_network_get_creg(&status), status);
}

static Result network_state_handle_check_gprs_registration(void)
{
    wm_SdkNetRegStatus status = WM_SDK_NET_REG_NOT_REGISTERED;
    return network_reg_status_to_result(wm_sdk_network_get_cgreg(&status), status);
}

static Result network_state_handle_check_lte_attachment(void)
{
    wm_SdkNetAttStatus status = WM_SDK_NET_DETACHED;
    if (wm_sdk_network_get_cgatt(&status) != WM_SDK_RESULT_SUCCESS)
        return RESULT_BUSY;
    return (status == WM_SDK_NET_ATTACHED) ? RESULT_SUCCESS : RESULT_BUSY;
}

static Result network_state_handle_setup_pdp(void)
{
    if (wm_sdk_network_set_pdp_context((UINT32)g_network_config.cid,
                                    g_network.pdp_type,
                                    g_network_config.apn) != WM_SDK_RESULT_SUCCESS)
        return RESULT_BUSY;

    LOG_INFO("NET PDP ctx %d apn=%s type=%s",
                 g_network_config.cid, g_network_config.apn, g_network.pdp_type);
    return RESULT_SUCCESS;
}

static Result network_state_handle_activate_pdp(void)
{
    return (wm_sdk_network_activate_pdp(1U, (UINT32)g_network_config.cid) == WM_SDK_RESULT_SUCCESS)
               ? RESULT_SUCCESS : RESULT_BUSY;
}

static Result network_state_handle_get_ip(void)
{
    wm_SdkIpAddress ip;
    memset(&ip, 0, sizeof(ip));

    if (wm_sdk_network_get_ip_address((UINT32)g_network_config.cid, &ip) != WM_SDK_RESULT_SUCCESS)
        return RESULT_BUSY;
    if (ip.ipv4[0] == '\0' && ip.ipv6[0] == '\0')
        return RESULT_BUSY;

    LOG_INFO("NET IP %s%s%s",
                 ip.ipv4[0] ? ip.ipv4 : "",
                 (ip.ipv4[0] && ip.ipv6[0]) ? " / " : "",
                 ip.ipv6[0] ? ip.ipv6 : "");
    return RESULT_SUCCESS;
}

/* Reference 4-way health check: CREG + CGREG + CGATT + IP must all pass.
 * (Not wm_sdk_network_get_network_status(): that API also requires the wm-lib
 * gf_pdp_ready flag, which only the kernel's own connection path sets - it
 * stays 0 for a PDP context activated directly via AT+CGACT.) */
static Result network_health_check(void)
{
    Result results[] = {
        network_state_handle_check_registration(),
        network_state_handle_check_gprs_registration(),
        network_state_handle_check_lte_attachment(),
        network_state_handle_get_ip()
    };
    for (int i = 0; i < 4; i++) {
        if (results[i] != RESULT_SUCCESS) {
            LOG_WARN("NET health fail i=%d r=%d", i, (int)results[i]);
            return RESULT_ERROR;
        }
    }
    return RESULT_SUCCESS;
}

/* BUSY = still connected; SUCCESS = disconnect detected (reference
 * network_state_handle_connected: 10 s stabilize grace after entry, then the
 * 4-way health check every 65 s). */
static Result network_state_handle_connected(void)
{
    static UINT32 connected_start_ticks   = 0;
    static UINT32 last_health_check_ticks = 0;
    UINT32 now_ticks = wm_sdk_get_ticks();

    if (connected_start_ticks == 0) {
        LOG_DEBUG("NET conn enter");
        connected_start_ticks   = (now_ticks == 0U) ? 1U : now_ticks;
        last_health_check_ticks = connected_start_ticks;
        return RESULT_BUSY;
    }
    if ((now_ticks - connected_start_ticks) < NETWORK_CONNECTED_STABILIZE_MS)
        return RESULT_BUSY;

    if ((now_ticks - last_health_check_ticks) >= NETWORK_HEALTH_CHECK_INTERVAL_MS) {
        last_health_check_ticks = (now_ticks == 0U) ? 1U : now_ticks;
        if (network_health_check() != RESULT_SUCCESS) {
            LOG_WARN("NET conn drop (health check failed)");
            connected_start_ticks   = 0;
            last_health_check_ticks = 0;
            return RESULT_SUCCESS;   /* -> DISCONNECTED */
        }
        LOG_DEBUG("NET conn health ok");
    }
    return RESULT_BUSY;
}

static Result network_state_handle_disconnected(void)
{
    network_set_connected(false);
    /* Best-effort context teardown so the next cycle starts clean */
    (void)wm_sdk_network_activate_pdp(0U, (UINT32)g_network_config.cid);
    return RESULT_SUCCESS;
}

/*---------------------------------------------------------------
 * CFUN restart (non-blocking sub-sequence)
 *--------------------------------------------------------------*/
static void network_reset_cfun_restart_state(void)
{
    g_network.cfun_step = CFUN_STEP_IDLE;
}

static Result network_state_handle_restart_cfun(void)
{
    UINT32 now = wm_sdk_get_ticks();

    switch (g_network.cfun_step) {
    case CFUN_STEP_IDLE:
        if (wm_sdk_network_set_cfun(0U) != WM_SDK_RESULT_SUCCESS)
            return RESULT_BUSY;
        LOG_INFO("NET CFUN=0");
        g_network.cfun_step      = CFUN_STEP_WAIT_OFF;
        g_network.cfun_step_tick = now;
        return RESULT_BUSY;

    case CFUN_STEP_WAIT_OFF:
        if ((now - g_network.cfun_step_tick) < NETWORK_CFUN_OFF_SETTLE_MS)
            return RESULT_BUSY;
        if (wm_sdk_network_set_cfun(1U) != WM_SDK_RESULT_SUCCESS)
            return RESULT_BUSY;
        LOG_INFO("NET CFUN=1");
        g_network.cfun_step      = CFUN_STEP_WAIT_ON;
        g_network.cfun_step_tick = now;
        return RESULT_BUSY;

    case CFUN_STEP_WAIT_ON:
    default:
        if ((now - g_network.cfun_step_tick) < NETWORK_CFUN_ON_SETTLE_MS)
            return RESULT_BUSY;
        network_reset_cfun_restart_state();
        return RESULT_SUCCESS;
    }
}

/*---------------------------------------------------------------
 * URC handling (async queue, drained by the state-machine task)
 *--------------------------------------------------------------*/
static void network_handle_urc(urcEvent_e event)
{
    LOG_DEBUG("NET URC %d in %s",
                    (int)event, weware_network_state_to_string(g_network.state));

    switch (event) {
    case URC_NET_ACTIVE:
    case URC_PDP_ACTIVE:
        /* Positive indicator: during bring-up the state machine verifies via
         * the AT status reads itself; only a DISCONNECTED idle needs a kick. */
        if (g_network.state == NETWORK_STATE_DISCONNECTED) {
            LOG_INFO("NET URC recovery -> INIT");
            network_set_state(NETWORK_STATE_INIT);
        }
        break;

    case URC_NET_DISCONNECTED:
        if (g_network.state == NETWORK_STATE_CONNECTED) {
            LOG_ERROR("NET URC detach -> DISCONNECTED");
            network_set_state(NETWORK_STATE_DISCONNECTED);
        }
        break;

    case URC_PDP_INACTIVE:
        LOG_ERROR("NET URC PDP deactivated -> DISCONNECTED");
        network_set_state(NETWORK_STATE_DISCONNECTED);
        break;

    default:
        break;
    }
}

static void network_drain_urc_queue(void)
{
    Module *net = module_manager_get_module(MODULE_ID_NETWORK);
    UINT32  event  = 0;
    UINT32  popped = 0;

    if (!net || !net->config.urc_q)
        return;

    /* Filled by the URC processor fan-out; element is the bare urcEvent_e code */
    while (queue_pop(net->config.urc_q, &net->config.urc_q_config,
                     &event, 1U, &popped) == RESULT_SUCCESS && popped > 0U)
        network_handle_urc((urcEvent_e)event);
}

/*---------------------------------------------------------------
 * SIM available/unavailable events (broadcast by the SIM module;
 * reference network_on_sim_available/_unavailable via event_manager)
 *--------------------------------------------------------------*/
static void network_on_sim_available(const EventData *event, void *user_data)
{
    (void)event;
    (void)user_data;

    /* Reference behavior: a freshly available SIM restarts the radio
     * (RESTART_CFUN) so registration starts clean. */
    LOG_INFO("NET SIM available: RESTART_CFUN (s=%s)",
                 weware_network_state_to_string(g_network.state));
    network_set_state(NETWORK_STATE_RESTART_CFUN);
}

static void network_on_sim_unavailable(const EventData *event, void *user_data)
{
    (void)event;
    (void)user_data;

    /* SIM removal surfaces through modem/network URCs; do not force
     * a disconnect from here (reference same). */
    LOG_DEBUG("NET SIM unavailable ignored (s=%s)",
                    weware_network_state_to_string(g_network.state));
}

/*---------------------------------------------------------------
 * Timeout supervision
 *--------------------------------------------------------------*/
static void network_check_state_timeout(void)
{
    UINT32 elapsed = wm_sdk_get_ticks() - g_network.state_entry_tick;
    UINT32 timeout = 0;
    UINT32 i;

    for (i = 0; i < sizeof(g_network_state_timeouts) / sizeof(g_network_state_timeouts[0]); i++) {
        if (g_network_state_timeouts[i].state == g_network.state) {
            timeout = g_network_state_timeouts[i].timeout_ms;
            break;
        }
    }
    if (timeout == 0U || elapsed < timeout)
        return;

    LOG_WARN("NET state timeout s=%s t=%lu",
                    weware_network_state_to_string(g_network.state),
                    (unsigned long)elapsed);

    if (g_network.state == NETWORK_STATE_RESTART_CFUN) {
        network_reset_cfun_restart_state();
        network_set_state(NETWORK_STATE_INIT);
    } else {
        network_set_state(NETWORK_STATE_RESTART_CFUN);
    }
}

static void network_check_overall_timeout(void)
{
    if (g_network.connected) {
        g_network.not_connected_since = wm_sdk_get_ticks();
        return;
    }
    /* No SIM is not a network fault - never reset while waiting for one */
    if (g_network.state == NETWORK_STATE_CHECK_SIM) {
        g_network.not_connected_since = wm_sdk_get_ticks();
        return;
    }
    if ((wm_sdk_get_ticks() - g_network.not_connected_since) < NETWORK_OVERALL_TIMEOUT_MS)
        return;

    /* Reference escalation: broadcast EVENT_RESET_SOFT so the reset handler
     * persists queues/preboot state before rebooting (was a direct
     * wm_sdk_system_reset() that lost all persisted state). */
    LOG_ERROR("NET not connected for %lu ms - requesting soft reset",
                  (unsigned long)NETWORK_OVERALL_TIMEOUT_MS);
    event_manager_broadcast(EVENT_RESET_SOFT, "Network Manager", NULL, 0);
    /* Re-arm so the broadcast is not repeated every loop while the deferred
     * reset is carried out. */
    g_network.not_connected_since = wm_sdk_get_ticks();
}

/*---------------------------------------------------------------
 * State-machine task
 *--------------------------------------------------------------*/
static void network_task_entry(void *arg)
{
    NetworkState last_state = NETWORK_STATE_INIT;

    (void)arg;
    LOG_INFO("Network task started");

    g_network.state_entry_tick    = wm_sdk_get_ticks();
    g_network.not_connected_since = wm_sdk_get_ticks();

    for (;;) {
        /* Task-stall watchdog feed (module_manager_monitor_tasks) */
        module_manager_update_uptime(MODULE_ID_NETWORK);
        (void)task_stats_update_periodic(g_network_task, "Network", MODULE_ID_NETWORK,
                                         &g_network_task_stats, 0);

        network_drain_urc_queue();
        network_refresh_radio_info(false);

        if (last_state != g_network.state) {
            LOG_INFO("NET %s -> %s",
                         weware_network_state_to_string(last_state),
                         weware_network_state_to_string(g_network.state));
            last_state = g_network.state;
            g_network.state_entry_tick = wm_sdk_get_ticks();
        }

        network_check_state_timeout();
        network_check_overall_timeout();

        switch (g_network.state) {
        case NETWORK_STATE_INIT:
            network_set_connected(false);
            NW_SET_STATE(NETWORK_STATE_CHECK_SIM);
            break;

        case NETWORK_STATE_CHECK_SIM:
            NW_HANDLE_STATE(network_state_handle_check_sim(),
                                  NETWORK_STATE_SIM_INSERT,
                                  NETWORK_STATE_ERROR,
                                  NETWORK_STATE_CHECK_SIM);
            break;

        case NETWORK_STATE_SIM_INSERT:
            NW_HANDLE_STATE(network_state_handle_sim_insert(),
                                  NETWORK_STATE_CHECK_CTZU,
                                  NETWORK_STATE_ERROR,
                                  NETWORK_STATE_SIM_INSERT);
            break;

        case NETWORK_STATE_SIM_REMOVE:
            NW_HANDLE_STATE(network_state_handle_sim_remove(),
                                  NETWORK_STATE_DISCONNECTED,
                                  NETWORK_STATE_ERROR,
                                  NETWORK_STATE_SIM_REMOVE);
            break;

        case NETWORK_STATE_CHECK_CTZU:
            NW_HANDLE_STATE(network_state_handle_check_ctzu(),
                                  NETWORK_STATE_CHECK_REGISTRATION,
                                  NETWORK_STATE_ERROR,
                                  NETWORK_STATE_SET_CTZU);
            break;

        case NETWORK_STATE_SET_CTZU:
            NW_HANDLE_STATE(network_state_handle_set_ctzu(),
                                  NETWORK_STATE_RESTART_CFUN,
                                  NETWORK_STATE_ERROR,
                                  NETWORK_STATE_SET_CTZU);
            break;

        case NETWORK_STATE_CHECK_REGISTRATION:
            NW_HANDLE_STATE(network_state_handle_check_registration(),
                                  NETWORK_STATE_CHECK_GPRS_REGISTRATION,
                                  NETWORK_STATE_ERROR,
                                  NETWORK_STATE_CHECK_REGISTRATION);
            break;

        case NETWORK_STATE_CHECK_GPRS_REGISTRATION:
            NW_HANDLE_STATE(network_state_handle_check_gprs_registration(),
                                  NETWORK_STATE_CHECK_LTE_ATTACHMENT,
                                  NETWORK_STATE_ERROR,
                                  NETWORK_STATE_CHECK_GPRS_REGISTRATION);
            break;

        case NETWORK_STATE_CHECK_LTE_ATTACHMENT:
            NW_HANDLE_STATE(network_state_handle_check_lte_attachment(),
                                  NETWORK_STATE_SETUP_PDP,
                                  NETWORK_STATE_ERROR,
                                  NETWORK_STATE_CHECK_LTE_ATTACHMENT);
            break;

        case NETWORK_STATE_SETUP_PDP:
            NW_HANDLE_STATE(network_state_handle_setup_pdp(),
                                  NETWORK_STATE_ACTIVATE_PDP,
                                  NETWORK_STATE_ERROR,
                                  NETWORK_STATE_SETUP_PDP);
            break;

        case NETWORK_STATE_ACTIVATE_PDP:
            NW_HANDLE_STATE(network_state_handle_activate_pdp(),
                                  NETWORK_STATE_GET_IP,
                                  NETWORK_STATE_ERROR,
                                  NETWORK_STATE_ACTIVATE_PDP);
            break;

        case NETWORK_STATE_GET_IP:
            NW_HANDLE_STATE(network_state_handle_get_ip(),
                            NETWORK_STATE_CONNECTED,
                            NETWORK_STATE_ERROR,
                            NETWORK_STATE_GET_IP);
            /* Reference behavior (verbatim): connected is asserted on every
             * GET_IP pass, not only on SUCCESS. */
            network_set_connected(true);
            break;

        case NETWORK_STATE_CONNECTED:
        {
            Result r = network_state_handle_connected();
            if (r != RESULT_BUSY)
            {
                NW_SET_STATE(r == RESULT_SUCCESS ?
                             NETWORK_STATE_DISCONNECTED :
                             NETWORK_STATE_ERROR);
            }
            break;
        }

        case NETWORK_STATE_DISCONNECTED:
            NW_HANDLE_STATE(network_state_handle_disconnected(),
                                  NETWORK_STATE_INIT,
                                  NETWORK_STATE_ERROR,
                                  NETWORK_STATE_DISCONNECTED);
            break;

        case NETWORK_STATE_RESTART_CFUN:
            NW_HANDLE_STATE(network_state_handle_restart_cfun(),
                                  NETWORK_STATE_INIT,
                                  NETWORK_STATE_ERROR,
                                  NETWORK_STATE_RESTART_CFUN);
            break;

        case NETWORK_STATE_ERROR:
            NW_SET_STATE(NETWORK_STATE_INIT);
            break;
        }

        wm_sdk_task_sleep(NETWORK_TASK_INTERVAL_MS);
    }
}

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/
bool weware_network_is_connected(void)
{
    return g_network.connected;
}

bool weware_network_is_stable(void)
{
    return g_network.stable_network;
}

NetworkState weware_network_get_state(void)
{
    return g_network.state;
}

bool weware_network_get_radio_snapshot(NetworkRadioSnapshot *out)
{
    if (!out)
        return false;
    *out = g_network.radio;
    return true;
}

wm_SdkResult weware_network_set_apn(const char *apn, const char *pdp_type)
{
    if (apn) {
        /* Persists to the config file (reference network_config_set_apn).
         * Username/password are preserved via a local snapshot (set_apn
         * clears them when passed NULL). */
        NetworkConfig *cfg = network_config_get_storage();
        char user[32] = "";
        char pass[32] = "";
        if (cfg) {
            strcpy(user, cfg->username);
            strcpy(pass, cfg->password);
        }
        if (network_config_set_apn(apn, user, pass) != RESULT_SUCCESS)
            return WM_SDK_RESULT_INVALID_PARAM;
    }
    if (pdp_type) {
        if (strlen(pdp_type) >= sizeof(g_network.pdp_type))
            return WM_SDK_RESULT_INVALID_PARAM;
        strcpy(g_network.pdp_type, pdp_type);
    }
    return WM_SDK_RESULT_SUCCESS;
}

void weware_network_register_status_callback(weware_network_status_cb_t callback)
{
    g_network_status_cb = callback;
}

wm_SdkResult weware_network_init(void)
{
    LOG_INFO("Initializing network module");

    if (g_network.initialized) {
        LOG_WARN("Network module already initialized");
        return WM_SDK_RESULT_SUCCESS;
    }

    /* Config: config_load_from_file fills g_network_config before init runs;
     * apply defaults only when it is still empty (reference pattern). */
    if (g_network_config.apn[0] == '\0')
        network_config_get_defaults(&g_network_config);

    /* Bench override: NETWORK_FORCE_APN (network_config.h), applied after the
     * config file load so it beats both the defaults above and the persisted
     * APN. No-op when the macro is not defined. */
    network_config_apply_forced_apn();
  /* URC landing queue (reference network_manager pattern): the URC
     * processor is the sole kernel registrant and queue_push()es the bare
     * urcEvent_e code here; this module owns create/destroy. */
    Module *nm = module_manager_get_module(MODULE_ID_NETWORK);
    if (nm && nm->config.urc_q == NULL &&
        nm->config.urc_q_config.capacity > 0U &&
        nm->config.urc_q_config.element_size > 0U) {
        if (queue_manager_create(&nm->config.urc_q_config,
                                 &nm->config.urc_q) != RESULT_SUCCESS) {
            LOG_ERROR("Network URC queue create failed");
            return WM_SDK_RESULT_ERROR;
        }
    }

    /* SIM presence via event manager (reference pattern; replaces the
     * single-slot weware_sim callback, which stays free for other users). */
    event_manager_register(EVENT_SIM_AVAILABLE, network_on_sim_available, NULL, "Network Manager");
    event_manager_register(EVENT_SIM_UNAVAILABLE, network_on_sim_unavailable, NULL, "Network Manager");

    if (g_network_task == NULL) {
        g_network_task = wm_sdk_task_create(network_task_entry, NULL, "NETMGR",
                                         NULL, NETWORK_TASK_STACK,
                                         TP_TIMED_ACTIVITY);
        if (g_network_task == NULL) {
            LOG_ERROR("Failed to create network task");
            event_manager_unregister_module("Network Manager");
            if (nm && nm->config.urc_q != NULL) {
                queue_manager_destroy(nm->config.urc_q, &nm->config.urc_q_config);
                nm->config.urc_q = NULL;
            }
            return WM_SDK_RESULT_ERROR;
        }
    }

    g_network.initialized = true;
    LOG_INFO("Network module ready (apn=%s cid=%d)",
                 g_network_config.apn, g_network_config.cid);
    return WM_SDK_RESULT_SUCCESS;
}

wm_SdkResult weware_network_deinit(void)
{
    if (!g_network.initialized)
        return WM_SDK_RESULT_SUCCESS;

    event_manager_unregister_module("Network Manager");

    if (g_network_task != NULL) {
        wm_sdk_task_delete(g_network_task);
        g_network_task = NULL;
    }
    Module *nm = module_manager_get_module(MODULE_ID_NETWORK);
    if (nm && nm->config.urc_q != NULL) {
        queue_manager_destroy(nm->config.urc_q, &nm->config.urc_q_config);
        nm->config.urc_q = NULL;
    }

    memset(&g_network, 0, sizeof(g_network));
    g_network.state = NETWORK_STATE_INIT;
    strcpy(g_network.pdp_type, WEWARE_NETWORK_PDP_TYPE);

    LOG_INFO("Network module stopped");
    return WM_SDK_RESULT_SUCCESS;
}
