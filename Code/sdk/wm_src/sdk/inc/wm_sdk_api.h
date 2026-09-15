/**
 ******************************************************************************
 * @file    wm_sdk_api.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - umbrella header.
 *
 *          Single include that exposes the entire wm_sdk_* Platform Abstraction
 *          API (see WEGW_API_REQUIREMENTS_V0, "APIs used in Common Gateway").
 *          Application code can include just this header.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __WM_SDK_API_H__
#define __WM_SDK_API_H__

#include "wm_sdk_types.h"
#include "wm_sdk_network.h"
#include "wm_sdk_sim.h"
#include "wm_sdk_sms.h"
#include "wm_sdk_gps.h"
#include "wm_sdk_ble.h"
#include "wm_sdk_tcp.h"
#include "wm_sdk_https.h"
#include "wm_sdk_mqtt.h"
#include "wm_sdk_ota.h"
#include "wm_sdk_uart.h"
#include "wm_sdk_file.h"
#include "wm_sdk_storage.h"
#include "wm_sdk_os.h"
#include "wm_sdk_device.h"
#include "wm_sdk_gpio.h"
#include "wm_sdk_adc.h"
#include "wm_sdk_i2c.h"
#include "wm_sdk_led.h"
#include "wm_sdk_urc.h"
#include "wm_sdk_system.h"
#include "wm_sdk_log.h"
#include "wm_sdk_wm.h"

#endif /* __WM_SDK_API_H__ */
