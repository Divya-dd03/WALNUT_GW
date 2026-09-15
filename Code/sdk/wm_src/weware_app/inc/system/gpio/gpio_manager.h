/**
 * @file gpio_manager.h
 * @brief GPIO manager - direction, level, and query helpers over the walnut
 *        wm_sdk_gpio API (reference: system/gpio/gpio_manager.h).
 *
 * Init/deinit only track manager readiness; GPIO hardware is configured via
 * set_direction / set_level. Walnut adaptation: direction/level enums map to
 * the wm_sdk_gpio 0/1 convention instead of vendor SC_/PIN_ constants.
 */

#ifndef WEWARE_GPIO_MANAGER_H
#define WEWARE_GPIO_MANAGER_H

#include "common/types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief GPIO direction enumeration (walnut wm_sdk_gpio: 0=input, 1=output)
 */
typedef enum {
    GPIO_DIRECTION_INPUT = 0,   /**< GPIO input direction */
    GPIO_DIRECTION_OUTPUT = 1   /**< GPIO output direction */
} GpioDirection;

/**
 * @brief GPIO level enumeration (walnut wm_sdk_gpio: 0=low, 1=high)
 */
typedef enum {
    GPIO_LEVEL_LOW = 0,   /**< GPIO low level (0) */
    GPIO_LEVEL_HIGH = 1   /**< GPIO high level (1) */
} GpioLevel;

/**
 * @brief Mark GPIO manager as ready for API use.
 * @return RESULT_SUCCESS
 */
Result gpio_manager_init(void);

/**
 * @brief Mark GPIO manager as not ready.
 * @return RESULT_SUCCESS
 */
Result gpio_manager_deinit(void);

/**
 * @brief Set GPIO direction (input or output).
 */
Result gpio_manager_set_direction(unsigned int gpio, GpioDirection direction);

/**
 * @brief Set GPIO output level (high or low).
 * @note Pin should be configured as output before setting level.
 */
Result gpio_manager_set_level(unsigned int gpio, GpioLevel level);

/**
 * @brief Read current GPIO level.
 */
Result gpio_manager_get_level(unsigned int gpio, GpioLevel *level);

/**
 * @brief Read GPIO direction configuration.
 */
Result gpio_manager_get_direction(unsigned int gpio, GpioDirection *direction);

/**
 * @brief Whether gpio_manager_init() has been called without a matching deinit.
 * @return TRUE if ready, FALSE otherwise
 */
BOOL gpio_manager_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_GPIO_MANAGER_H */
