/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @LastEditTime: 2020-09-14 14:55:11
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */
#ifndef _MQTT_CONFIG_H_
#define _MQTT_CONFIG_H_
#include "typedef.h"

typedef UINT8 OSA_STATUS;

#define KAWAII_MQTT_LOG_BASE_LEVEL      (0)
#define KAWAII_MQTT_LOG_ERR_LEVEL       (KAWAII_MQTT_LOG_BASE_LEVEL + 1)
#define KAWAII_MQTT_LOG_WARN_LEVEL      (KAWAII_MQTT_LOG_ERR_LEVEL + 1)
#define KAWAII_MQTT_LOG_INFO_LEVEL      (KAWAII_MQTT_LOG_WARN_LEVEL + 1)
#define KAWAII_MQTT_LOG_DEBUG_LEVEL     (KAWAII_MQTT_LOG_INFO_LEVEL + 1)

/*Customer log defined here!!!*/
#undef mqtt_printf
#include "osi_api.h"
#include "asros_mbedtls_config.h"

#if 0//debug print enable
#define mqtt_printf(fmt,args...)	    do { RTI_LOG("[MQTT]"fmt, ##args); } while(0)
#else
#define mqtt_printf(fmt,args...)
#endif

#define KAWAII_MQTT_LOG_LEVEL           (KAWAII_MQTT_LOG_DEBUG_LEVEL)

#define     KAWAII_MQTT_NETWORK_TYPE_TLS
#define     MQTT_MAX_PACKET_ID                  (0xFFFF - 1)
#define     MQTT_TOPIC_LEN_MAX                  64
#define     MQTT_ACK_HANDLER_NUM_MAX            64
#define     MQTT_DEFAULT_BUF_SIZE               1024
#define     MQTT_DEFAULT_CMD_TIMEOUT            5000
#define     MQTT_MAX_CMD_TIMEOUT                20000
#define     MQTT_MIN_CMD_TIMEOUT                1000
#define     MQTT_KEEP_ALIVE_INTERVAL            50         // unit: second
#define     MQTT_VERSION                        4           // 4 is mqtt 3.1.1
#define     MQTT_RECONNECT_DEFAULT_DURATION     1000
#define     MQTT_THREAD_STACK_SIZE              2048
#define     MQTT_THREAD_PRIO                    90
#define     MQTT_THREAD_TICK                    50

#endif /* _MQTT_CONFIG_H_ */
