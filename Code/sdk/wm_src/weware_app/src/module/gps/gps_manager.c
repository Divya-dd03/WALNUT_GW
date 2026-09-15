/**
  ******************************************************************************
  * @file    gps_manager.c
  * @author  WheelsEye
  * @brief   GPS manager: task loop and init/deinit; operations in gps_ops -
  *          walnut port of the reference firmware's module/gps/gps_manager.c.
  *
  *          Walnut adaptations (business logic unchanged):
  *          - SIM presence is polled from the weware SIM module each cycle
  *            and fed through the same edge latch the reference drives from
  *            EVENT_SIM_AVAILABLE/UNAVAILABLE.
  *          - ignition / motion / charge have no source yet (power and accel
  *            subsystems not ported): public setters expose the same edge
  *            latches; bench defaults are ign=ON, motion=ON, charge=OFF so
  *            the ign-on interval (10 s) drives sends. TODO(power)
  *          - A-GPS open/refresh run in this task (reference: system task
  *            loop); both are NOT_SUPPORTED stubs on this receiver.
  *          - module manager / queues / task stats wired as in the reference.
  ******************************************************************************
  */

#include <string.h>

// sdk
#include "wm_global.h"
#include "wm_sdk_os.h"
#include "wm_sdk_log.h"
#include "wm_sdk_gps.h"

// app
#include "module/gps/gps_manager.h"
#include "module/gps/gps_config.h"
#include "module/gps/gps_ops.h"
#include "module/gps/gps_triggers.h"
#include "module/gps/gps_storage.h"
#include "module/gps/gps_urc_queue_types.h"
#include "module/sim/sim.h"
#include "module/module_manager.h"
#include "common/queue_manager.h"
#include "common/utils.h"

#include <stdio.h>

/*---------------------------------------------------------------
 * Runtime State
 *--------------------------------------------------------------*/
gps_manager_runtime_t g_gps = {
    .task_ref = NULL,
    .config = &g_gps_config,
    .ign_status = FALSE,
    .mot_status = FALSE,
    .charge_status = FALSE,
    .trigger_reason = 0,
    .current_gps_data = {0},
    .last_valid_gps_data = {0},
    .should_trigger = FALSE,
    .last_packet_send_uptime_sec = 0,
    .packets_dropped = 0,
    .gnss_mode_set = FALSE,
    .gnss_nmea_rate_set = FALSE,
    .gnss_nmea_output_configured = FALSE,
    .gnss_nmea_output_started = FALSE,
    .gnss_info_report_set = FALSE,
    .gnss_start_mode_set = FALSE,
    .gnss_agps_set = FALSE,
};

static volatile BOOL g_gps_running = FALSE;

/*---------------------------------------------------------------
 * Trigger latch helpers (verbatim reference logic)
 *--------------------------------------------------------------*/

static void gps_latch_send_trigger(int reason)
{
    g_gps.should_trigger = TRUE;
    g_gps.trigger_reason = reason;
}

static void gps_set_ignition_status(BOOL ign_status)
{
    if (g_gps.ign_status == ign_status)
        return;
    g_gps.ign_status = ign_status;
    gps_latch_send_trigger(ign_status ? GPS_TRIGGER_IGN_ON : GPS_TRIGGER_IGN_OFF);
    wm_sdk_log_info("GPS ignition latch %s", ign_status ? "ON" : "OFF");
}

static void gps_set_motion_status(BOOL mot_status)
{
    if (g_gps.mot_status == mot_status)
        return;
    g_gps.mot_status = mot_status;
    gps_ops_loc_storage_update_on_motion_changed(mot_status);
    /* Reference keeps the motion trigger latch disabled too:
     * gps_latch_send_trigger(mot_status ? GPS_TRIGGER_MOTION_ON : GPS_TRIGGER_MOTION_OFF); */
    wm_sdk_log_info("GPS motion latch %s", mot_status ? "ON" : "OFF");
}

static void gps_set_charge_status(BOOL charge_connected)
{
    static BOOL prev = FALSE;

    if (prev == charge_connected)
        return;
    prev = charge_connected;
    g_gps.charge_status = charge_connected;
    gps_latch_send_trigger(charge_connected ? GPS_TRIGGER_CHARGE_CONNECTED
                                            : GPS_TRIGGER_CHARGE_DISCONNECTED);
}

static void gps_set_sim_status(BOOL sim_available)
{
    static BOOL prev = FALSE;

    if (prev == sim_available)
        return;
    prev = sim_available;
    gps_latch_send_trigger(sim_available ? GPS_TRIGGER_SIM_INSERTED
                                         : GPS_TRIGGER_SIM_REMOVED);
}

/*---------------------------------------------------------------
 * NMEA feed (reference: urc_processor GNSS block)
 * Walnut delta: the kernel has no GNSS URCs; wm_sdk_gps_set_nmea_callback()
 * delivers complete sentences from the GNSS parser task, so the reference's
 * 768-byte fragment reassembler is not needed. RMC/GGA are paired here and
 * the combined record is queue_push()ed into the GPS urc_q; the GPS task
 * pops and parses it (gps_ops_query_and_parse).
 *--------------------------------------------------------------*/
static char s_nmea_slot_rmc[GPS_URC_SENTENCE_MAX];
static char s_nmea_slot_gga[GPS_URC_SENTENCE_MAX];

static void gps_nmea_trim_eol(char *s)
{
    size_t l = strlen(s);
    while (l > 0 && (s[l - 1] == '\r' || s[l - 1] == '\n'))
        s[--l] = '\0';
}

static void gps_nmea_push_combined_pair(void)
{
    Module          *mod = module_manager_get_module(MODULE_ID_GPS);
    gps_urc_queued_t gps_el;
    int              n;

    if (!s_nmea_slot_rmc[0] || !s_nmea_slot_gga[0])
        return;
    if (!mod || !mod->config.urc_q)
        return;

    memset(&gps_el, 0, sizeof(gps_el));
    n = snprintf(gps_el.combined, sizeof(gps_el.combined), "%s\n%s",
                 s_nmea_slot_rmc, s_nmea_slot_gga);
    if (n < 0 || (size_t)n >= sizeof(gps_el.combined)) {
        wm_sdk_log_warning("GPS NMEA combined truncated or snprintf error");
        s_nmea_slot_rmc[0] = '\0';
        s_nmea_slot_gga[0] = '\0';
        return;
    }

    /* Full queue just means the GPS task is behind by >4 s; the pair is
     * droppable (a fresh one arrives next epoch) - debug, not error. */
    if (queue_push(mod->config.urc_q, &mod->config.urc_q_config, &gps_el) != RESULT_SUCCESS)
        wm_sdk_debug_print("GPS NMEA queue_push failed (full)\r\n");

    s_nmea_slot_rmc[0] = '\0';
    s_nmea_slot_gga[0] = '\0';
}

/* Runs on the kernel GNSS parser task - keep short, never block. Multi-GNSS
 * receivers emit the GN talker, GPS-only mode emits GP - accept both (the
 * reference matches GN only; its receiver always ran multi-GNSS). */
static void gps_nmea_sentence_cb(const char *sentence, UINT16 len)
{
    (void)len;

    if (!sentence || sentence[0] != '$' || sentence[1] != 'G' ||
        (sentence[2] != 'N' && sentence[2] != 'P'))
        return;

    if (strncmp(sentence + 3, "RMC,", 4) == 0) {
        (void)utils_strncpy_safe(s_nmea_slot_rmc, sentence, sizeof(s_nmea_slot_rmc));
        gps_nmea_trim_eol(s_nmea_slot_rmc);
        if (s_nmea_slot_gga[0])
            gps_nmea_push_combined_pair();
        return;
    }
    if (strncmp(sentence + 3, "GGA,", 4) == 0) {
        (void)utils_strncpy_safe(s_nmea_slot_gga, sentence, sizeof(s_nmea_slot_gga));
        gps_nmea_trim_eol(s_nmea_slot_gga);
        if (s_nmea_slot_rmc[0])
            gps_nmea_push_combined_pair();
    }
}

BOOL gps_manager_nmea_feed_register(void)
{
    wm_SdkResult r = wm_sdk_gps_set_nmea_callback(gps_nmea_sentence_cb);
    if (r != WM_SDK_RESULT_SUCCESS) {
        wm_sdk_log_warning("GPS NMEA callback register failed (%d) - no position source until retry", (int)r);
        return FALSE;
    }
    return TRUE;
}

/*---------------------------------------------------------------
 * Input polling (reference: event manager registrations)
 *--------------------------------------------------------------*/

/** SIM presence edge, polled 1 Hz from the weware SIM module (reference:
 *  EVENT_SIM_AVAILABLE/UNAVAILABLE broadcast). */
static void gps_poll_sim_presence(void)
{
    gps_set_sim_status(weware_sim_get_sim_status() ? TRUE : FALSE);
}

/*---------------------------------------------------------------
 * Task loop (verbatim reference shape)
 *--------------------------------------------------------------*/

static void gps_task_entry(void *argv)
{
    char append_buf[MODULE_MESSAGE_INLINE_SIZE];
    char msg_buf[MODULE_MESSAGE_INLINE_SIZE];

    (void)argv;
    wm_sdk_log_info("GPS task started");

    while (g_gps_running) {
        wm_sdk_task_sleep(GPS_TASK_LOOP_SLEEP_MS);

        /* Task-stall watchdog feed (module_manager_monitor_tasks) */
        module_manager_update_uptime(MODULE_ID_GPS);
        task_stats_update_periodic(g_gps.task_ref, "GPS", MODULE_ID_GPS,
                                   &g_gps.task_stats, 0);

        gps_poll_sim_presence();

        (void)gps_ops_power_on_and_configure_nmea_step();
        gps_ops_retry_configuration();
        /* Reference runs the A-GPS maintenance from the system task loop;
         * walnut has no system task yet (and A-GPS is a stub here). */
        gps_ops_open_agps_if_needed();
        gps_ops_agps_refresh_if_needed();

        gps_ops_refresh_current_sample();
        gps_ops_handle_fix_transition(gps_ops_process_fix_transition());

        UINT32 append_len = gps_ops_pop_gps_msg_queue_latch_append(
            append_buf, sizeof(append_buf));

        if (!gps_triggers_should_send())
            continue;

        GpsPacketBuildKind kind = gps_ops_pick_send_kind();
        int gps_len = gps_ops_build_send_packet(msg_buf, sizeof(msg_buf), kind);
        if (gps_len <= 0)
            continue;

        UINT32 total_len = gps_ops_merge_tcp_append(msg_buf, (UINT32)gps_len,
                                                    append_buf, append_len);
        (void)gps_ops_push_tcp_position_message(msg_buf, total_len);
        gps_triggers_clear_after_send();
    }
}

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/

wm_SdkResult gps_manager_deinit(void)
{
    /* Stop the NMEA feed first: the callback runs on the kernel GNSS task
     * and pushes into the urc_q we are about to destroy. */
    (void)wm_sdk_gps_set_nmea_callback(NULL);

    if (g_gps.task_ref) {
        g_gps_running = FALSE;
        wm_sdk_task_delete(g_gps.task_ref);
        g_gps.task_ref = NULL;
        /* Reference broadcasts EVENT_GPS_DISCONNECTED (event manager TODO). */
    }

    Module *gps_module = module_manager_get_module(MODULE_ID_GPS);
    if (gps_module && gps_module->config.urc_q != NULL) {
        queue_manager_destroy(gps_module->config.urc_q, &gps_module->config.urc_q_config);
        gps_module->config.urc_q = NULL;
    }

    gps_ops_set_runtime_defaults();
    wm_sdk_log_info("GPS manager stopped");
    return WM_SDK_RESULT_SUCCESS;
}

BOOL gps_manager_get_last_valid_position(double *lat, double *lon, float *course)
{
    if (g_gps.last_valid_gps_data.utc_time == 0)
        return FALSE;
    if (lat) *lat = g_gps.last_valid_gps_data.latitude_deg;
    if (lon) *lon = g_gps.last_valid_gps_data.longitude_deg;
    if (course) *course = g_gps.last_valid_gps_data.course_deg;
    return TRUE;
}

BOOL gps_manager_is_ignition_on(void)
{
    return g_gps.ign_status ? TRUE : FALSE;
}

BOOL gps_manager_is_motion_on(void)
{
    return g_gps.mot_status ? TRUE : FALSE;
}

void gps_manager_set_ignition_status(BOOL ign_on)
{
    gps_set_ignition_status(ign_on);
}

void gps_manager_set_motion_status(BOOL mot_on)
{
    gps_set_motion_status(mot_on);
}

void gps_manager_set_charge_status(BOOL charge_connected)
{
    gps_set_charge_status(charge_connected);
}

BOOL gps_manager_get_charge_status(void)
{
    return g_gps.charge_status ? TRUE : FALSE;
}

/*---------------------------------------------------------------
 * Init (last - bottom-up entry)
 *--------------------------------------------------------------*/

wm_SdkResult gps_manager_init(void)
{
    if (g_gps.task_ref != NULL)
        return WM_SDK_RESULT_SUCCESS;

    gps_ops_set_runtime_defaults();
    gps_storage_handle_post_boot();

    /* Reference resolves config through module_manager. Apply defaults only
     * when the module framework (config_load_from_file) hasn't already
     * initialized the global storage - otherwise loaded values survive. */
    if (!gps_config_defaults_applied())
        gps_config_get_defaults(&g_gps_config);
    g_gps.config = &g_gps_config;

    /* Reference snapshots PowerInfo for the initial ignition/motion state.
     * TODO(power): power/accel subsystems not ported - bench placeholders
     * report a live moving device so the ign-on interval drives sends. */
    g_gps.ign_status = TRUE;
    g_gps.mot_status = TRUE;
    g_gps.charge_status = FALSE;

    /* NMEA landing queue (reference gps_create_queues pattern), then the
     * kernel NMEA callback that fills it. The queue is THE position source
     * (reference URC build): on register failure the gps_ops stall recovery
     * retries every 20 cycles, and a sustained silence escalates through the
     * reference parse-fail streak (60 fails -> reboot). */
    Module *gps_module = module_manager_get_module(MODULE_ID_GPS);
    if (gps_module && gps_module->config.urc_q == NULL &&
        gps_module->config.urc_q_config.capacity > 0U &&
        gps_module->config.urc_q_config.element_size > 0U) {
        if (queue_manager_create(&gps_module->config.urc_q_config,
                                 &gps_module->config.urc_q) != RESULT_SUCCESS) {
            wm_sdk_log_error("GPS URC queue create failed - navdata fallback only");
            gps_module->config.urc_q = NULL;
        }
    }
    s_nmea_slot_rmc[0] = '\0';
    s_nmea_slot_gga[0] = '\0';
    (void)gps_manager_nmea_feed_register();

    g_gps_running = TRUE;
    g_gps.task_ref = wm_sdk_task_create(gps_task_entry, NULL, "gpsTask",
                                     NULL, GPS_TASK_STACK_SIZE,
                                     TP_TIMED_ACTIVITY);
    if (!g_gps.task_ref) {
        g_gps_running = FALSE;
        wm_sdk_log_error("GPS task create failed");
        return WM_SDK_RESULT_ERROR;
    }

    wm_sdk_log_info("GPS manager ready");
    return WM_SDK_RESULT_SUCCESS;
}
