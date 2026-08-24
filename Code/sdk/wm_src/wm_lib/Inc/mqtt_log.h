/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2019-12-27 03:25:58
 * @LastEditTime: 2020-06-17 15:26:54
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */
#ifndef _MQTT_LOG_H_
#define _MQTT_LOG_H_

#include "mqtt_defconfig.h"

#if KAWAII_MQTT_LOG_LEVEL < KAWAII_MQTT_LOG_DEBUG_LEVEL
#define KAWAII_MQTT_LOG_D(fmt, ...)
#else
#define KAWAII_MQTT_LOG_D(fmt, ...)        { mqtt_printf(fmt, ##__VA_ARGS__);}
#endif

#if KAWAII_MQTT_LOG_LEVEL < KAWAII_MQTT_LOG_INFO_LEVEL
#define KAWAII_MQTT_LOG_I(fmt, ...)
#else
#define KAWAII_MQTT_LOG_I(fmt, ...)        { mqtt_printf(fmt, ##__VA_ARGS__);}
#endif

#if KAWAII_MQTT_LOG_LEVEL < KAWAII_MQTT_LOG_WARN_LEVEL
#define KAWAII_MQTT_LOG_W(fmt, ...)
#else
#define KAWAII_MQTT_LOG_W(fmt, ...)        { mqtt_printf(fmt, ##__VA_ARGS__);}
#endif

#if KAWAII_MQTT_LOG_LEVEL < KAWAII_MQTT_LOG_ERR_LEVEL
#define KAWAII_MQTT_LOG_E(fmt, ...)
#else
#define KAWAII_MQTT_LOG_E(fmt, ...)        { mqtt_printf(fmt, ##__VA_ARGS__);}
#endif

#if KAWAII_MQTT_LOG_LEVEL < KAWAII_MQTT_LOG_BASE_LEVEL
#define KAWAII_MQTT_LOG(fmt, ...)
#else
#define KAWAII_MQTT_LOG(fmt, ...)          { mqtt_printf(fmt, ##__VA_ARGS__);}
#endif

#define mqtt_log_init()

#endif /* _LOG_H_ */
