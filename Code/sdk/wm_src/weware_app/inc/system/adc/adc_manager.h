/**
 * @file adc_manager.h
 * @brief ADC manager - walnut port of the reference system/adc/adc_manager.h.
 *
 * Walnut adaptations: channel numbers and the external divider gain come from
 * the defines below instead of the reference SDK platform layer.
 * TODO(board): confirm GW1NS ADC channel mapping and divider gain; the
 * defaults mirror the reference SIMCOM board (ignition=1, external=2,
 * gain=131) and can be overridden with -D at build time.
 */

#ifndef WEWARE_ADC_MANAGER_H
#define WEWARE_ADC_MANAGER_H

#include "common/types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ADC_CHANNEL_IGNITION
#define ADC_CHANNEL_IGNITION 1  /**< TODO(board): GW1NS ignition-sense ADC channel */
#endif
#ifndef ADC_CHANNEL_EXTERNAL
#define ADC_CHANNEL_EXTERNAL 2  /**< TODO(board): GW1NS external-supply ADC channel */
#endif
#ifndef ADC_GAIN
#define ADC_GAIN 131            /**< TODO(board): external divider gain (reference value) */
#endif

/**
 * @brief ADC gain value (reference: from SDK platform layer).
 */
#define ADC_GAIN_VALUE ADC_GAIN

/**
 * @brief ADC channel enumeration
 */
typedef enum {
    ADC_GET_IGNITION = ADC_CHANNEL_IGNITION,
    ADC_GET_EXTERNAL = ADC_CHANNEL_EXTERNAL,
} AdcChannel;

/**
 * @brief Initialize ADC manager
 * @return Result status
 */
Result adc_manager_init(void);

/**
 * @brief Deinitialize ADC manager
 * @return Result status
 */
Result adc_manager_deinit(void);

/**
 * @brief Read voltage from a supported ADC channel.
 * @param channel ADC channel to read.
 * @param value Pointer to store voltage in volts.
 * @return RESULT_SUCCESS on success, otherwise error code.
 */
Result adc_manager_get_voltage(AdcChannel channel, float *value);

/**
 * @brief Read VBAT (modem battery) voltage.
 * @param value Pointer to store voltage in volts.
 * @return RESULT_SUCCESS on success, otherwise error code.
 */
Result adc_manager_get_vbat_voltage(float *value);

/**
 * @brief Check if ADC manager is ready
 * @return TRUE if ready, FALSE otherwise
 */
BOOL adc_manager_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_ADC_MANAGER_H */
