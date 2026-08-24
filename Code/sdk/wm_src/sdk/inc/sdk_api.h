/**
 ******************************************************************************
 * @file    sdk_api.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - umbrella header.
 *
 *          Single include that exposes the entire sdk_* Platform Abstraction
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

#ifndef __SDK_API_H__
#define __SDK_API_H__

#include "sdk_types.h"
#include "sdk_network.h"
#include "sdk_sim.h"
#include "sdk_sms.h"
#include "sdk_gps.h"
#include "sdk_tcp.h"
#include "sdk_https.h"
#include "sdk_mqtt.h"
#include "sdk_ota.h"
#include "sdk_uart.h"
#include "sdk_file.h"
#include "sdk_storage.h"
#include "sdk_os.h"
#include "sdk_device.h"
#include "sdk_gpio.h"
#include "sdk_adc.h"
#include "sdk_i2c.h"
#include "sdk_urc.h"
#include "sdk_system.h"
#include "sdk_log.h"
#include "sdk_wm.h"

#endif /* __SDK_API_H__ */
