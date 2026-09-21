/**
 * @file gpio_manager.c
 * @brief GPIO manager - thin walnut wrapper over wm_sdk_gpio.
 */

#include "system/gpio/gpio_manager.h"
#include "wm_sdk_gpio.h"

#define LOG_TAG          "GPIO"
#define LOG_MODULE_LEVEL LOG_LEVEL_DEBUG
#include "module/log/log.h"

static BOOL s_ready = FALSE;

Result gpio_manager_init(void)
{
    s_ready = TRUE;
    return RESULT_SUCCESS;
}

Result gpio_manager_deinit(void)
{
    s_ready = FALSE;
    return RESULT_SUCCESS;
}

Result gpio_manager_set_direction(unsigned int gpio, GpioDirection direction)
{
    if (!s_ready)
        return RESULT_ERROR;
    if (wm_sdk_gpio_set_direction(gpio, (UINT32)direction) != WM_SDK_RESULT_SUCCESS) {
        LOG_WARN("set_direction failed (pin %u)", gpio);
        return RESULT_ERROR;
    }
    return RESULT_SUCCESS;
}

Result gpio_manager_set_level(unsigned int gpio, GpioLevel level)
{
    if (!s_ready)
        return RESULT_ERROR;
    if (wm_sdk_gpio_set_level(gpio, (UINT32)level) != WM_SDK_RESULT_SUCCESS) {
        LOG_WARN("set_level failed (pin %u)", gpio);
        return RESULT_ERROR;
    }
    return RESULT_SUCCESS;
}

Result gpio_manager_get_level(unsigned int gpio, GpioLevel *level)
{
    UINT32 v = 0;

    if (!level)
        return RESULT_INVALID_PARAM;
    if (!s_ready)
        return RESULT_ERROR;
    if (wm_sdk_gpio_get_level(gpio, &v) != WM_SDK_RESULT_SUCCESS)
        return RESULT_ERROR;
    *level = (v != 0u) ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW;
    return RESULT_SUCCESS;
}

Result gpio_manager_get_direction(unsigned int gpio, GpioDirection *direction)
{
    UINT32 v = 0;

    if (!direction)
        return RESULT_INVALID_PARAM;
    if (!s_ready)
        return RESULT_ERROR;
    if (wm_sdk_gpio_get_direction(gpio, &v) != WM_SDK_RESULT_SUCCESS)
        return RESULT_ERROR;
    *direction = (v != 0u) ? GPIO_DIRECTION_OUTPUT : GPIO_DIRECTION_INPUT;
    return RESULT_SUCCESS;
}

BOOL gpio_manager_is_ready(void)
{
    return s_ready;
}
