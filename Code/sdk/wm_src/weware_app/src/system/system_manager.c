/**
 * @file system_manager.c
 * @brief System-level init/deinit and periodic loop - walnut port.
 *
 * Walnut adaptations vs the reference:
 * - OTA manager, digout (relay) manager and the STK8321 accelerometer are not
 *   ported yet; their init/poll calls are TODO-stubbed out.
 * - Netlight drives the GW1NS on-board blue LED (GPIO 69, active high) from
 *   its own 500ms task: solid ON while the data connection is up, fast blink
 *   while searching for / having lost the network.
 * - Status and power-select GPIO pins are still unknown for the GW1NS board;
 *   they default to "unassigned" and those GPIO touches are skipped until
 *   TODO(board) pin numbers are provided (override with -D defines).
 * - Network connected/disconnected events are bridged from the walnut
 *   network module's single-slot status callback (the reference modules
 *   broadcast events themselves).
 * - Charge state changes also drive the walnut GPS charge latch.
 * - Status print: walnut ticks are milliseconds; platform string is fixed.
 */

/*---------------------------------------------------------------
 * Includes
 *--------------------------------------------------------------*/
#include "system/system_manager.h"
#include "common/event_manager.h"
#include "common/utils.h"
#include "config/config.h"
#include "system/storage/flash_paths.h"
#include "system/storage/file_system.h"
#include "system/adc/adc_manager.h"
#include "system/device_utils.h"
#include "system/gpio/gpio_manager.h"
#include "module/log/log_manager.h"
#include "module/log/log_config.h"
#include "system/reset/reset_handler.h"
#include "system/reset/post_boot_handler.h"
#include "system/ota/ota_manager.h"

/* OTA cycle (HTTPS version check + firmware download) master switch. Runs
 * inline on WEMAIN like the reference; WEMAIN's stack is sized for the
 * kernel HTTPS client (weware_main.c). Disable with -DWEWARE_OTA_ENABLED=0. */
#ifndef WEWARE_OTA_ENABLED
#define WEWARE_OTA_ENABLED 1
#endif
#include "system/time_utils.h"
#include "weware_version.h"
#include "module/module_manager.h"
#include "system/system_config.h"
#include "system/vehicle_state.h"
#include "module/gps/gps_manager.h"
#include "module/gps/gps_config.h"
#include "module/gps/gps_ops.h"
#include "module/network/network.h"

#include "sdk_platform.h"
#include "wm_global.h"          /* walnut: TP_TIMED_ACTIVITY task priority */

#include <stdio.h>

extern gps_manager_runtime_t g_gps;

/*---------------------------------------------------------------
 * Log Configuration
 *--------------------------------------------------------------*/
#define LOG_TAG          "SYSTEM"
#define LOG_MODULE_LEVEL LOG_LEVEL_DEBUG
#include "module/log/log.h"

/*---------------------------------------------------------------
 * Configuration
 *--------------------------------------------------------------*/
#define NETLIGHT_BLINK_HALF_PERIOD_MS    500U    /* 500ms on/off = 1s period */
#define NETLIGHT_TASK_STACK              2048U
#define UPTIME_SOFT_RESET_THRESHOLD_SEC  (24U * 3600U)
#define ADC_POLL_INTERVAL_MS             2000U
#define WEWARE_STATUS_PRINT_INTERVAL_MS  10000U
#define ACCEL_POLL_INTERVAL_MS           1000U

/* TODO(board): GW1NS status / power-select pin numbers are still unknown -
 * those indications stay disabled until real pins are supplied (-D define). */
#define WEWARE_GPIO_PIN_UNASSIGNED       0xFFFFFFFFu
#ifndef SDK_GPIO_STATUS_PIN
#define SDK_GPIO_STATUS_PIN              WEWARE_GPIO_PIN_UNASSIGNED
#endif
/* GW1NS on-board LEDs (see wm_ui_app WM_DEMO_GPIO): 69 = blue, 70 = red,
 * both active high (level 1 = lit). Netlight uses the blue one. */
#ifndef SDK_GPIO_NETLIGHT_PIN
#define SDK_GPIO_NETLIGHT_PIN            69u
#endif
#ifndef POWER_SRC_SELECT_PIN
#define POWER_SRC_SELECT_PIN             WEWARE_GPIO_PIN_UNASSIGNED
#endif

/*---------------------------------------------------------------
 * Static State
 *--------------------------------------------------------------*/
static BOOL      g_netlight_connected         = FALSE;
static BOOL      g_netlight_blink_state       = FALSE;
static UINT32    g_netlight_last_toggle_ticks = 0;
static BOOL      g_netlight_pin_ready         = FALSE;
static void     *g_netlight_task              = NULL;
static UINT32    g_adc_poll_last_ticks        = 0;
static PowerInfo g_power_info                 = {0};

static void system_manager_adc_poll(void);

static inline BOOL board_pin_valid(unsigned int pin)
{
    return pin != WEWARE_GPIO_PIN_UNASSIGNED;
}

/*---------------------------------------------------------------
 * Netlight (blue LED, active high)
 *
 * Registered/searching for network -> fast blink (500ms on / 500ms off);
 * data connection up                -> solid ON.
 *
 * Driven by its own task: WEMAIN sleeps 1s per iteration and runs the OTA
 * cycle (synchronous ranged downloads) inline, so a WEMAIN-polled tick
 * cannot keep a 500ms blink. netlight_tick() stays the single apply point
 * and is also called from the WEMAIN loop as a fallback when the task
 * could not be created (degraded ~1s blink).
 *--------------------------------------------------------------*/
/* Drive the LED and remember the level we last wrote (active high). */
static void netlight_drive(BOOL on)
{
    g_netlight_blink_state = on;
    gpio_manager_set_level(SDK_GPIO_NETLIGHT_PIN, on ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
}

static void netlight_on_network_connected(const EventData *event, void *user_data)
{
    (void)event;
    (void)user_data;
    g_netlight_connected = TRUE;
    if (g_netlight_pin_ready && gpio_manager_is_ready()) {
        netlight_drive(TRUE);
        wm_sdk_debug_print("[netlight] Network connected - solid ON\r\n");
    }
}

static void netlight_on_network_disconnected(const EventData *event, void *user_data)
{
    (void)event;
    (void)user_data;
    g_netlight_connected = FALSE;
    g_netlight_last_toggle_ticks = SDK_GET_TICKS();
    if (g_netlight_pin_ready && gpio_manager_is_ready())
        netlight_drive(FALSE);
    wm_sdk_debug_print("[netlight] Network lost - blinking %ums\r\n", (unsigned)NETLIGHT_BLINK_HALF_PERIOD_MS);
}

static void netlight_tick(void)
{
    BOOL connected;

    if (!g_netlight_pin_ready || !gpio_manager_is_ready())
        return;

    /* Read the network module directly: covers boot (no event broadcast yet)
     * and any transition the single-slot status callback could not deliver. */
    connected = weware_network_is_connected() ? TRUE : FALSE;

    if (connected != g_netlight_connected) {
        g_netlight_connected = connected;
        g_netlight_last_toggle_ticks = SDK_GET_TICKS();
        netlight_drive(connected);          /* solid ON, or start the blink low */
        return;
    }

    if (connected) {
        if (!g_netlight_blink_state)        /* self-heal a stray write */
            netlight_drive(TRUE);
        return;
    }

    if (utils_elapsed_ms_since(g_netlight_last_toggle_ticks) >= NETLIGHT_BLINK_HALF_PERIOD_MS) {
        g_netlight_last_toggle_ticks = SDK_GET_TICKS();
        netlight_drive(g_netlight_blink_state ? FALSE : TRUE);
    }
}

static void netlight_task_entry(void *arg)
{
    (void)arg;
    while (1) {
        netlight_tick();
        /* Half the blink half-period so the toggle deadline is never missed
         * by a full step (500ms on/off stays visually even). */
        wm_sdk_task_sleep(NETLIGHT_BLINK_HALF_PERIOD_MS / 2U);
    }
}

/*---------------------------------------------------------------
 * Walnut event bridge: network status callback -> event broadcasts
 * (reference modules broadcast EVENT_NETWORK_* themselves).
 * SIM status callback slot is owned by network.c; TCP/GPS connected
 * events are a TODO on the walnut modules.
 *--------------------------------------------------------------*/
static void bridge_network_status(bool connected)
{
    event_manager_broadcast(connected ? EVENT_NETWORK_CONNECTED : EVENT_NETWORK_DISCONNECTED,
                            "network", NULL, 0);
}

/*---------------------------------------------------------------
 * Internal: status display
 *--------------------------------------------------------------*/
static void print_system_status(void)
{
    UINT32 ticks_per_second = 1000; /* walnut wm_sdk_get_ticks() is ms */

    UINT32 ticks   = SDK_GET_TICKS();
    UINT32 seconds = ticks / ticks_per_second;
    UINT32 hours   = seconds / 3600;
    UINT32 minutes = (seconds % 3600) / 60;
    UINT32 secs    = seconds % 60;

    const char *reset_reason_str = post_boot_handler_get_soc_reset_reason_string();

    UINT32 ram_total_kb      = 0, ram_free_kb = 0;
    INT64  flash_total_kb    = 0, flash_free_kb = 0;
    UINT8  cpu_usage_percent = 0;
    BOOL   stats_available   = FALSE;

    if (wm_sdk_system_get_stats(&ram_total_kb, &ram_free_kb,
                             &flash_total_kb, &flash_free_kb,
                             &cpu_usage_percent) == WM_SDK_RESULT_SUCCESS) {
        stats_available = TRUE;
    }

    char flash_info[64] = "N/A";
    if (stats_available && flash_total_kb > 0) {
        snprintf(flash_info, sizeof(flash_info), "%lldKB/%lldKB free",
                 (long long)flash_free_kb, (long long)flash_total_kb);
    }

    char ram_info[64] = "N/A";
    if (stats_available && ram_free_kb > 0) {
        if (ram_total_kb > 0) {
            snprintf(ram_info, sizeof(ram_info), "%luKB/%luKB free",
                     (unsigned long)ram_free_kb, (unsigned long)ram_total_kb);
        } else {
            snprintf(ram_info, sizeof(ram_info), "~%luKB free", (unsigned long)ram_free_kb);
        }
    }

    char cpu_info[16] = "N/A";
    if (stats_available && cpu_usage_percent > 0) {
        snprintf(cpu_info, sizeof(cpu_info), "%u%%", cpu_usage_percent);
    }

    ResetType reset_type   = RESET_TYPE_NONE;
    UINT32    total_resets = 0;
    char      reboot_module[33] = "N/A";
    post_boot_handler_load_reset_state(&reset_type, NULL, reboot_module, &total_resets);

    const char *reset_type_str = "NONE";
    switch (reset_type) {
        case RESET_TYPE_SOFT: reset_type_str = "SOFT"; break;
        case RESET_TYPE_HARD: reset_type_str = "HARD"; break;
        case RESET_TYPE_CFUN: reset_type_str = "CFUN"; break;
        default: break;
    }

    wm_sdk_log_info(
        "WEWARE STATUS - Uptime: %lu:%02lu:%02lu | FW: %s | HW: %s | App: %s | SDK: WALNUT"
        " | Reset: %s (code:%lu) | Reboot: %s/%s #%lu | RAM: %s | Flash: %s | CPU: %s\r\n",
        (unsigned long)hours, (unsigned long)minutes, (unsigned long)secs,
        FIRMWARE_VERSION, HARDWARE_VERSION, APP_VERSION,
        reset_reason_str, (unsigned long)post_boot_handler_get_soc_reset_reason(),
        reset_type_str,
        reboot_module[0] != '\0' ? reboot_module : "N/A",
        (unsigned long)total_resets,
        ram_info, flash_info, cpu_info);
}

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/

Result system_manager_init(void)
{
    (void)logger_init(g_log_config.output);

    if (device_utils_init() != WM_SDK_RESULT_SUCCESS) {
        wm_sdk_log_warning("Device utils init failed, continuing anyway");
    }

    if (file_system_init() == RESULT_ERROR) {
        wm_sdk_log_error("File system init failed");
        return RESULT_ERROR;
    }

    (void)flash_paths_ensure_directories();

    if (post_boot_handler_init() != RESULT_SUCCESS)
        wm_sdk_log_warning("post_boot_handler init failed, continuing anyway");

    config_get_current();
    system_config_migrate_from_tcp_if_needed();
    vehicle_state_init();

    if (event_manager_init() == RESULT_ERROR) {
        wm_sdk_log_error("Event manager init failed");
        return RESULT_ERROR;
    }

#if WEWARE_OTA_ENABLED
    {
        Result ota_r = ota_manager_init();
        if (ota_r != RESULT_SUCCESS && ota_r != RESULT_ALREADY_INITIALIZED)
            wm_sdk_log_warning("OTA manager init failed, continuing anyway");
    }
#else
    wm_sdk_log_warning("OTA manager disabled at compile time (WEWARE_OTA_ENABLED=0)");
#endif

    if (reset_handler_init() != RESULT_SUCCESS) {
        wm_sdk_log_warning("Reset handler init failed, continuing anyway");
    }

    /* Walnut event bridge (see note above). Registering before network init
     * is safe: network.c keeps the callback slot across weware_network_init. */
    weware_network_register_status_callback(bridge_network_status);

    if (gpio_manager_init() == RESULT_SUCCESS && board_pin_valid(SDK_GPIO_STATUS_PIN) &&
        gpio_manager_set_direction(SDK_GPIO_STATUS_PIN, GPIO_DIRECTION_OUTPUT) == RESULT_SUCCESS) {
        gpio_manager_set_level(SDK_GPIO_STATUS_PIN, GPIO_LEVEL_HIGH);
        wm_sdk_debug_print("GPIO status indicator set (pin %u)\r\n", (unsigned)SDK_GPIO_STATUS_PIN);
    } else {
        wm_sdk_debug_print("GPIO status indicator disabled (pin unassigned)\r\n");
    }

    if (board_pin_valid(POWER_SRC_SELECT_PIN) &&
        gpio_manager_set_direction(POWER_SRC_SELECT_PIN, GPIO_DIRECTION_OUTPUT) == RESULT_SUCCESS) {
        gpio_manager_set_level(POWER_SRC_SELECT_PIN, GPIO_LEVEL_LOW);
        wm_sdk_debug_print("Power source select GPIO set (pin %u)\r\n", (unsigned)POWER_SRC_SELECT_PIN);
    }

    if (board_pin_valid(SDK_GPIO_NETLIGHT_PIN) &&
        gpio_manager_set_direction(SDK_GPIO_NETLIGHT_PIN, GPIO_DIRECTION_OUTPUT) == RESULT_SUCCESS) {
        g_netlight_pin_ready = TRUE;
        g_netlight_connected = FALSE;
        g_netlight_last_toggle_ticks = SDK_GET_TICKS();
        netlight_drive(FALSE);
        wm_sdk_debug_print("Netlight blue LED on pin %u\r\n", (unsigned)SDK_GPIO_NETLIGHT_PIN);

        g_netlight_task = wm_sdk_task_create(netlight_task_entry, NULL, "NETLED",
                                          NULL, NETLIGHT_TASK_STACK, TP_TIMED_ACTIVITY);
        if (g_netlight_task == NULL)
            wm_sdk_log_warning("Netlight task create failed - falling back to WEMAIN tick (~1s blink)");
    } else {
        wm_sdk_debug_print("Netlight disabled (pin unassigned or direction set failed)\r\n");
    }
    /* Netlight handlers registered regardless - they no-op without a pin and
     * start driving the LED the moment a pin define is supplied. */
    if (event_manager_register(EVENT_NETWORK_CONNECTED,    netlight_on_network_connected,    NULL, "Netlight") == RESULT_SUCCESS &&
        event_manager_register(EVENT_NETWORK_DISCONNECTED, netlight_on_network_disconnected, NULL, "Netlight") == RESULT_SUCCESS) {
        wm_sdk_debug_print("Netlight events registered\r\n");
    }

    /* TODO(digout): relay digout manager not ported yet. */

    if (adc_manager_init() != RESULT_SUCCESS) {
        wm_sdk_log_warning("ADC manager init failed, continuing anyway");
    } else {
        /* Prime power/charge before any GPS packet (e.g. TCP login+GPS) - avoids charge=OFF from zeroed PowerInfo */
        system_manager_adc_poll();
    }
    g_adc_poll_last_ticks = SDK_GET_TICKS();

    /* TODO(accel): STK8321 accelerometer driver not ported (no walnut I2C
     * transfer API in wm_sdk_*; needs vendor i2cc_*). */

    wm_sdk_log_info("System components initialized");
    return RESULT_SUCCESS;
}

Result system_manager_deinit(void)
{
    event_manager_unregister(EVENT_NETWORK_CONNECTED,    netlight_on_network_connected);
    event_manager_unregister(EVENT_NETWORK_DISCONNECTED, netlight_on_network_disconnected);
    weware_network_register_status_callback(NULL);
    if (g_netlight_task != NULL) {
        (void)wm_sdk_task_delete(g_netlight_task);
        g_netlight_task = NULL;
    }
    if (g_netlight_pin_ready) {
        netlight_drive(FALSE);
        g_netlight_pin_ready = FALSE;
    }
    (void)gpio_manager_deinit();
    (void)adc_manager_deinit();
    (void)event_manager_deinit();
    wm_sdk_log_info("System components deinitialized");
    return RESULT_SUCCESS;
}

const PowerInfo *system_manager_get_power_info(void)
{
    return &g_power_info;
}

static void system_manager_adc_poll(void)
{
    if (!adc_manager_is_ready())
        return;

    float ev = 0.0f, iv = 0.0f, bv = 0.0f, real_ev = 0.0f;
    if (adc_manager_get_voltage(ADC_GET_IGNITION, &iv) != RESULT_SUCCESS ||
        adc_manager_get_voltage(ADC_GET_EXTERNAL, &ev) != RESULT_SUCCESS ||
        adc_manager_get_vbat_voltage(&bv) != RESULT_SUCCESS) {
        wm_sdk_log_warning("[ADC] read failed");
        return;
    }

    wm_sdk_log_info("[ADC] ev: %.3fV, iv:%.3fV, bv:%.3fV", ev, iv, bv);

    static BOOL prev_charge_connected = FALSE;

    real_ev = ev * ADC_GAIN_VALUE < 3 ? 0 : ev * ADC_GAIN_VALUE;

    g_power_info.external_voltage = real_ev;
    g_power_info.input_wire_voltage = iv * ADC_GAIN_VALUE;
    g_power_info.charge_connected = (g_power_info.external_voltage >= 3.0f);

    if (g_power_info.charge_connected)
        g_power_info.battery_percentage = 100;
    else if (bv <= 3.4f)
        g_power_info.battery_percentage = 0;
    else if (bv >= 4.1f)
        g_power_info.battery_percentage = 100;
    else
        g_power_info.battery_percentage = (UINT8)((bv - 3.4f) * 100.0f / 0.7f);

    if (g_power_info.charge_connected != prev_charge_connected) {
        prev_charge_connected = g_power_info.charge_connected;
        event_manager_broadcast(g_power_info.charge_connected ? EVENT_CHARGE_CONNECTED : EVENT_CHARGE_DISCONNECTED,
                                "system_manager", NULL, 0);
        /* Walnut: GPS charge latch input (reference reads PowerInfo). */
        gps_manager_set_charge_status(g_power_info.charge_connected);
    }

    vehicle_state_poll(TRUE);
}

void system_manager_loop_iteration(void)
{
    static UINT32 last_status_print_time = 0;

    /* Normally driven by the NETLED task; only tick here if it is missing. */
    if (g_netlight_task == NULL)
        netlight_tick();

    UINT32 now = SDK_GET_TICKS();
    if (utils_elapsed_ms_since(g_adc_poll_last_ticks) >= ADC_POLL_INTERVAL_MS) {
        g_adc_poll_last_ticks = now;
        system_manager_adc_poll();
    } else {
        vehicle_state_poll(FALSE);
    }

    /* TODO(accel): reference runs vehicle_state_accel_tick() here each
     * ACCEL_POLL_INTERVAL_MS when soft motion needs the accelerometer. */

    /* TODO(digout): digout_manager_poll() */

    /* A-GPS: unlike the reference (system loop drives it), the walnut GPS
     * task already calls gps_ops_open_agps_if_needed()/agps_refresh_if_needed()
     * itself (gps_manager.c) - do not double-drive from here. */

#if WEWARE_OTA_ENABLED
    /* Reference call site. Runs the whole OTA cycle inline on WEMAIN: the
     * version check is async on the kernel HTTPS worker; the ranged download
     * helpers are synchronous (~16 KB stack) - WEMAIN is sized for it. */
    ota_manager_check_and_update();
#endif

    if (utils_get_uptime_seconds() >= UPTIME_SOFT_RESET_THRESHOLD_SEC) {
        wm_sdk_log_info("Uptime >= 24h, broadcasting soft reset");
        event_manager_broadcast(EVENT_RESET_SOFT, "System Manager", NULL, 0);
    }

    if (utils_elapsed_ms_since(last_status_print_time) >= WEWARE_STATUS_PRINT_INTERVAL_MS) {
        last_status_print_time = now;
        print_system_status();
    }

    if (reset_handler_process_deferred_reset()) {
        wm_sdk_log_info("Deferred reset performed");
    }
}
