/**
 * @file system_manager.c
 * @brief System-level init/deinit and periodic loop - walnut port.
 *
 * Walnut adaptations vs the reference:
 * - OTA manager, digout (relay) manager and the STK8321 accelerometer are not
 *   ported yet; their init/poll calls are TODO-stubbed out.
 * - Status/netlight/power-select GPIO pins are unknown for the GW1NS board;
 *   they default to "unassigned" and every GPIO touch is skipped until
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

/* OTA cycle (HTTPS version check + firmware download) master switch. Disabled
 * while the cycle still runs inline on WEMAIN's stack - see the note at the
 * ota_manager_check_and_update() call site. Re-enable with -DWEWARE_OTA_ENABLED=1
 * once the cycle owns a TLS-sized task stack. */
#ifndef WEWARE_OTA_ENABLED
#define WEWARE_OTA_ENABLED 0
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

#include <stdio.h>

extern gps_manager_runtime_t g_gps;

/*---------------------------------------------------------------
 * Log Configuration
 *--------------------------------------------------------------*/
#define LOG_TAG          "SYSTEM"
#define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

/*---------------------------------------------------------------
 * Configuration
 *--------------------------------------------------------------*/
#define NETLIGHT_BLINK_HALF_PERIOD_MS    500U    /* 500ms on/off = 1s period */
#define UPTIME_SOFT_RESET_THRESHOLD_SEC  (24U * 3600U)
#define ADC_POLL_INTERVAL_MS             2000U
#define WEWARE_STATUS_PRINT_INTERVAL_MS  10000U
#define ACCEL_POLL_INTERVAL_MS           1000U

/* TODO(board): GW1NS pin numbers unknown - GPIO indications are disabled
 * until real pins are supplied (e.g. -DSDK_GPIO_NETLIGHT_PIN=9). */
#define WEWARE_GPIO_PIN_UNASSIGNED       0xFFFFFFFFu
#ifndef SDK_GPIO_STATUS_PIN
#define SDK_GPIO_STATUS_PIN              WEWARE_GPIO_PIN_UNASSIGNED
#endif
#ifndef SDK_GPIO_NETLIGHT_PIN
#define SDK_GPIO_NETLIGHT_PIN            WEWARE_GPIO_PIN_UNASSIGNED
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
static UINT32    g_adc_poll_last_ticks        = 0;
static PowerInfo g_power_info                 = {0};

static void system_manager_adc_poll(void);

static inline BOOL board_pin_valid(unsigned int pin)
{
    return pin != WEWARE_GPIO_PIN_UNASSIGNED;
}

/*---------------------------------------------------------------
 * Netlight event handlers
 *--------------------------------------------------------------*/
static void netlight_on_network_connected(const EventData *event, void *user_data)
{
    (void)event;
    (void)user_data;
    g_netlight_connected = TRUE;
    if (gpio_manager_is_ready() && board_pin_valid(SDK_GPIO_NETLIGHT_PIN)) {
        gpio_manager_set_level(SDK_GPIO_NETLIGHT_PIN, GPIO_LEVEL_HIGH);
        LOG_DEBUG("[netlight] Network connected - solid ON");
    }
}

static void netlight_on_network_disconnected(const EventData *event, void *user_data)
{
    (void)event;
    (void)user_data;
    g_netlight_connected = FALSE;
    g_netlight_last_toggle_ticks = SDK_GET_TICKS();
    g_netlight_blink_state = FALSE;
    LOG_DEBUG("[netlight] Network disconnected - blinking 1s");
}

static void netlight_tick(void)
{
    if (!gpio_manager_is_ready() || g_netlight_connected ||
        !board_pin_valid(SDK_GPIO_NETLIGHT_PIN))
        return;
    if (utils_elapsed_ms_since(g_netlight_last_toggle_ticks) >= NETLIGHT_BLINK_HALF_PERIOD_MS) {
        g_netlight_last_toggle_ticks = SDK_GET_TICKS();
        g_netlight_blink_state = !g_netlight_blink_state;
        gpio_manager_set_level(SDK_GPIO_NETLIGHT_PIN,
                               g_netlight_blink_state ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
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
    UINT32 ticks_per_second = 1000; /* walnut sdk_get_ticks() is ms */

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

    if (sdk_system_get_stats(&ram_total_kb, &ram_free_kb,
                             &flash_total_kb, &flash_free_kb,
                             &cpu_usage_percent) == SDK_RESULT_SUCCESS) {
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

    SDK_DEBUG_PRINT(
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

    if (device_utils_init() != SDK_RESULT_SUCCESS) {
        LOG_WARN("Device utils init failed, continuing anyway");
    }

    if (file_system_init() == RESULT_ERROR) {
        LOG_ERROR("File system init failed");
        return RESULT_ERROR;
    }

    (void)flash_paths_ensure_directories();

    if (post_boot_handler_init() != RESULT_SUCCESS)
        LOG_WARN("post_boot_handler init failed, continuing anyway");

    config_get_current();
    system_config_migrate_from_tcp_if_needed();
    vehicle_state_init();

    if (event_manager_init() == RESULT_ERROR) {
        LOG_ERROR("Event manager init failed");
        return RESULT_ERROR;
    }

#if WEWARE_OTA_ENABLED
    {
        Result ota_r = ota_manager_init();
        if (ota_r != RESULT_SUCCESS && ota_r != RESULT_ALREADY_INITIALIZED)
            LOG_WARN("OTA manager init failed, continuing anyway");
    }
#else
    LOG_WARN("OTA manager disabled at compile time (WEWARE_OTA_ENABLED=0)");
#endif

    if (reset_handler_init() != RESULT_SUCCESS) {
        LOG_WARN("Reset handler init failed, continuing anyway");
    }

    /* Walnut event bridge (see note above). Registering before network init
     * is safe: network.c keeps the callback slot across weware_network_init. */
    weware_network_register_status_callback(bridge_network_status);

    if (gpio_manager_init() == RESULT_SUCCESS && board_pin_valid(SDK_GPIO_STATUS_PIN) &&
        gpio_manager_set_direction(SDK_GPIO_STATUS_PIN, GPIO_DIRECTION_OUTPUT) == RESULT_SUCCESS) {
        gpio_manager_set_level(SDK_GPIO_STATUS_PIN, GPIO_LEVEL_HIGH);
        LOG_DEBUG("GPIO status indicator set (pin %u)", (unsigned)SDK_GPIO_STATUS_PIN);
    } else {
        LOG_DEBUG("GPIO status indicator disabled (pin unassigned)");
    }

    if (board_pin_valid(POWER_SRC_SELECT_PIN) &&
        gpio_manager_set_direction(POWER_SRC_SELECT_PIN, GPIO_DIRECTION_OUTPUT) == RESULT_SUCCESS) {
        gpio_manager_set_level(POWER_SRC_SELECT_PIN, GPIO_LEVEL_LOW);
        LOG_DEBUG("Power source select GPIO set (pin %u)", (unsigned)POWER_SRC_SELECT_PIN);
    }

    if (board_pin_valid(SDK_GPIO_NETLIGHT_PIN) &&
        gpio_manager_set_direction(SDK_GPIO_NETLIGHT_PIN, GPIO_DIRECTION_OUTPUT) == RESULT_SUCCESS) {
        g_netlight_connected = FALSE;
        g_netlight_last_toggle_ticks = SDK_GET_TICKS();
        gpio_manager_set_level(SDK_GPIO_NETLIGHT_PIN, GPIO_LEVEL_LOW);
    }
    /* Netlight handlers registered regardless - they no-op without a pin and
     * start driving the LED the moment a pin define is supplied. */
    if (event_manager_register(EVENT_NETWORK_CONNECTED,    netlight_on_network_connected,    NULL, "Netlight") == RESULT_SUCCESS &&
        event_manager_register(EVENT_NETWORK_DISCONNECTED, netlight_on_network_disconnected, NULL, "Netlight") == RESULT_SUCCESS) {
        LOG_DEBUG("Netlight events registered");
    }

    /* TODO(digout): relay digout manager not ported yet. */

    if (adc_manager_init() != RESULT_SUCCESS) {
        LOG_WARN("ADC manager init failed, continuing anyway");
    } else {
        /* Prime power/charge before any GPS packet (e.g. TCP login+GPS) - avoids charge=OFF from zeroed PowerInfo */
        system_manager_adc_poll();
    }
    g_adc_poll_last_ticks = SDK_GET_TICKS();

    /* TODO(accel): STK8321 accelerometer driver not ported (no walnut I2C
     * transfer API in sdk_*; needs vendor i2cc_*). */

    LOG_INFO("System components initialized");
    return RESULT_SUCCESS;
}

Result system_manager_deinit(void)
{
    event_manager_unregister(EVENT_NETWORK_CONNECTED,    netlight_on_network_connected);
    event_manager_unregister(EVENT_NETWORK_DISCONNECTED, netlight_on_network_disconnected);
    weware_network_register_status_callback(NULL);
    (void)gpio_manager_deinit();
    (void)adc_manager_deinit();
    (void)event_manager_deinit();
    LOG_INFO("System components deinitialized");
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
        LOG_WARN("[ADC] read failed");
        return;
    }

    LOG_INFO("[ADC] ev: %.3fV, iv:%.3fV, bv:%.3fV", ev, iv, bv);

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
    /* NOTE: runs the whole OTA cycle - including HTTPS/TLS version check and
     * download - INLINE on the caller's (WEMAIN) stack. Confirmed to overflow
     * an 8KB task stack; before re-enabling, move the OTA cycle to its own
     * task with a TLS-sized stack (>=16KB) or enlarge WEMAIN's accordingly. */
    ota_manager_check_and_update();
#endif

    if (utils_get_uptime_seconds() >= UPTIME_SOFT_RESET_THRESHOLD_SEC) {
        LOG_INFO("Uptime >= 24h, broadcasting soft reset");
        event_manager_broadcast(EVENT_RESET_SOFT, "System Manager", NULL, 0);
    }

    if (utils_elapsed_ms_since(last_status_print_time) >= WEWARE_STATUS_PRINT_INTERVAL_MS) {
        last_status_print_time = now;
        print_system_status();
    }

    if (reset_handler_process_deferred_reset()) {
        LOG_INFO("Deferred reset performed");
    }
}
