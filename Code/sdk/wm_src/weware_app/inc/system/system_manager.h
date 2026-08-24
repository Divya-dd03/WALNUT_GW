/**
 * @file system_manager.h
 * @brief System-level initialization and deinitialization for Weware platform
 *
 * Initializes and deinitializes all system components: logging, device utils,
 * file system, storage (reset state), config, event manager, event registrations
 * (module_manager, reset_handler), and GPIO. Does not initialize application
 * modules (use module_manager for that).
 */

#ifndef WEWARE_SYSTEM_MANAGER_H
#define WEWARE_SYSTEM_MANAGER_H

#include "common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Power information (from ADC, used by GPS packet and others)
 */
typedef struct {
    float external_voltage;
    float input_wire_voltage;
    UINT8 battery_percentage;
    BOOL charge_connected;
    BOOL ignition_on;
    BOOL motion_on;
    /** Successful @c system_manager_adc_poll() count since boot (saturates at 255). */
    UINT8 adc_poll_count;
    /** TRUE once enough post-boot ADC polls ran (see @c TcpConfig.ign_det_debounce_polls). */
    BOOL post_boot_ignition_ready;
    UINT32 last_update_timestamp;
} PowerInfo;

/** Default ignition debounce polls if TCP config unavailable (@c ign-det debounce). */
#define IGNITION_ADC_DEBOUNCE_POLLS_DEFAULT 2u

/**
 * @brief Get current power info (updated by system_manager from ADC)
 * @return Pointer to power info (never NULL; values may be zero if ADC not yet updated)
 */
const PowerInfo *system_manager_get_power_info(void);

/**
 * @brief Initialize all system components
 *
 * Order: logging, device utils, file system, storage reset state, config load,
 * event manager, module_manager and reset_handler event registration, GPIO
 * (including status indicator pin).
 *
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result system_manager_init(void);

/**
 * @brief Deinitialize all system components (reverse order of init)
 *
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result system_manager_deinit(void);

/**
 * @brief Periodic loop iteration for system (OTA, status print, SIM hotswap, deferred reset)
 */
void system_manager_loop_iteration(void);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_SYSTEM_MANAGER_H */
