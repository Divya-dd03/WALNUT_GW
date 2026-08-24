/**
 * @file vehicle_state.h
 * @brief Ignition and motion state evaluation (debounced, event-driven)
 */

#ifndef WEWARE_VEHICLE_STATE_H
#define WEWARE_VEHICLE_STATE_H

#include "common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

void vehicle_state_init(void);

/**
 * @brief Evaluate ignition/motion after ADC samples updated in PowerInfo.
 * @param adc_poll_done TRUE when this cycle included a fresh ADC read
 */
void vehicle_state_poll(BOOL adc_poll_done);

/** Run accelerometer sample when soft motion needs it (call from system loop). */
void vehicle_state_accel_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_VEHICLE_STATE_H */
