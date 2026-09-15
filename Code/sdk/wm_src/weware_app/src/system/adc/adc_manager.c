/**
 * @file adc_manager.c
 * @brief ADC manager - thin walnut wrapper over wm_sdk_adc (values in volts).
 */

#include "system/adc/adc_manager.h"
#include "wm_sdk_adc.h"

#define LOG_TAG          "ADC"
#define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

static BOOL s_ready = FALSE;

Result adc_manager_init(void)
{
    UINT16 vbat_mv = 0;

    /* Probe VBAT once so a broken ADC path is visible at boot; the manager
     * still comes up (reads report errors per call, matching the reference). */
    if (wm_sdk_adc_read_vbat_voltage(&vbat_mv) != WM_SDK_RESULT_SUCCESS)
        LOG_WARN("VBAT probe failed at init");
    else
        LOG_INFO("Ready (vbat=%u mV)", (unsigned)vbat_mv);

    s_ready = TRUE;
    return RESULT_SUCCESS;
}

Result adc_manager_deinit(void)
{
    s_ready = FALSE;
    return RESULT_SUCCESS;
}

Result adc_manager_get_voltage(AdcChannel channel, float *value)
{
    UINT16 mv = 0;

    if (!value)
        return RESULT_INVALID_PARAM;
    if (!s_ready)
        return RESULT_NOT_INITIALIZED;
    if (channel != ADC_GET_IGNITION && channel != ADC_GET_EXTERNAL)
        return RESULT_INVALID_PARAM;

    if (wm_sdk_adc_read_voltage((INT32)channel, &mv) != WM_SDK_RESULT_SUCCESS) {
        LOG_WARN("Read failed (channel %d)", (int)channel);
        return RESULT_ERROR;
    }

    *value = (float)mv / 1000.0f;
    return RESULT_SUCCESS;
}

Result adc_manager_get_vbat_voltage(float *value)
{
    UINT16 mv = 0;

    if (!value)
        return RESULT_INVALID_PARAM;
    if (!s_ready)
        return RESULT_NOT_INITIALIZED;

    if (wm_sdk_adc_read_vbat_voltage(&mv) != WM_SDK_RESULT_SUCCESS) {
        LOG_WARN("VBAT read failed");
        return RESULT_ERROR;
    }

    *value = (float)mv / 1000.0f;
    return RESULT_SUCCESS;
}

BOOL adc_manager_is_ready(void)
{
    return s_ready;
}
