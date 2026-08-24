/**
  ******************************************************************************
  * @file    paho_mqtt_api.h
  * @author  Walnut Medical
  * @brief   Source file of key events.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */
#ifndef __PAHO_MQTT_API_H__
#define __PAHO_MQTT_API_H__

/*******************************************************************************
 ** Header Files
 ******************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
/*******************************************************************************
 ** MACROS
 ******************************************************************************/
#define PAHO_MQTT_TOPIC_LEN_MAX		64

typedef enum
{
	PAHO_MQTT_QOS0 = 0,
	PAHO_MQTT_QOS1 = 1,
	PAHO_MQTT_QOS2 = 2,
	PAHO_MQTT_SUBFAIL = 0x80
} paho_mqtt_qos_e;

/*mqtt error enum*/
typedef enum
{
    PAHO_MQTT_SSL_CERT_ERROR                                     = -0x001C,      /* cetr parse failed */
    PAHO_MQTT_SOCKET_FAILED_ERROR                                = -0x001B,      /* socket fd failed */
    PAHO_MQTT_SOCKET_UNKNOWN_HOST_ERROR                          = -0x001A,      /* socket unknown host ip or domain */
    PAHO_MQTT_SET_PUBLISH_DUP_FAILED_ERROR                       = -0x0019,      /* mqtt publish packet set udp bit failed */
    PAHO_MQTT_CLEAN_SESSION_ERROR                                = -0x0018,      /* mqtt clean session error */
    PAHO_MQTT_ACK_NODE_IS_EXIST_ERROR                            = -0x0017,      /* mqtt ack list is exist ack node */
    PAHO_MQTT_ACK_HANDLER_NUM_TOO_MUCH_ERROR                     = -0x0016,      /* mqtt ack handler number is too much */
    PAHO_MQTT_RESUBSCRIBE_ERROR                                  = -0x0015,      /* mqtt resubscribe error */
    PAHO_MQTT_SUBSCRIBE_ERROR                                    = -0x0014,      /* mqtt subscribe error */
    PAHO_MQTT_SEND_PACKET_ERROR                                  = -0x0013,      /* mqtt send a packet */
    PAHO_MQTT_SERIALIZE_PUBLISH_ACK_PACKET_ERROR                 = -0x0012,      /* mqtt serialize publish ack packet error */
    PAHO_MQTT_PUBLISH_PACKET_ERROR                               = -0x0011,      /* mqtt publish packet error */
    PAHO_MQTT_RECONNECT_TIMEOUT_ERROR                            = -0x0010,      /* mqtt try reconnect, but timeout */
    PAHO_MQTT_SUBSCRIBE_NOT_ACK_ERROR                            = -0x000F,      /* mqtt subscribe, but not ack */
    PAHO_MQTT_NOT_CONNECT_ERROR                                  = -0x000E,      /* mqtt not connect */
    PAHO_MQTT_SUBSCRIBE_ACK_PACKET_ERROR                         = -0x000D,      /* mqtt subscribe, but ack packet error */
    PAHO_MQTT_UNSUBSCRIBE_ACK_PACKET_ERROR                       = -0x000C,      /* mqtt unsubscribe, but ack packet error */
    PAHO_MQTT_PUBLISH_ACK_PACKET_ERROR                           = -0x000B,      /* mqtt pubilsh ack packet error */
    PAHO_MQTT_PUBLISH_ACK_TYPE_ERROR                             = -0x000A,      /* mqtt pubilsh ack type error */
    PAHO_MQTT_PUBREC_PACKET_ERROR                                = -0x0009,      /* mqtt pubrec packet error */
    PAHO_MQTT_BUFFER_TOO_SHORT_ERROR                             = -0x0008,      /* mqtt buffer too short */
    PAHO_MQTT_NOTHING_TO_READ_ERROR                              = -0x0007,      /* mqtt nothing to read */
    PAHO_MQTT_SUBSCRIBE_QOS_ERROR                                = -0x0006,      /* mqtt subsrcibe qos error */
    PAHO_MQTT_BUFFER_OVERFLOW_ERROR                              = -0x0005,      /* mqtt buffer overflow */
    PAHO_MQTT_CONNECT_FAILED_ERROR                               = -0x0004,      /* mqtt connect failed */
	PAHO_MQTT_MEM_NOT_ENOUGH_ERROR								 = -0x0003,      /* mqtt memory not enough */
	PAHO_MQTT_NULL_VALUE_ERROR 		    						 = -0x0002,      /* mqtt value is null */
	PAHO_MQTT_FAILED_ERROR										 = -0x0001,      /* failed */
	PAHO_MQTT_SUCCESS_ERROR 									 = -0x0000        /* success */
}paho_mqtt_error_e;

/*******************************************************************************
 ** Type Definitions
 ******************************************************************************/
typedef struct
{
	char *mqtt_host;
	char *mqtt_port;

	char *mqtt_client_id;
	char *mqtt_user_name;
	char *mqtt_password;

	uint32_t mqtt_version : 4;
	uint16_t mqtt_keep_alive_interval;
}paho_mqtt_config_t;

typedef struct
{
	paho_mqtt_qos_e qos;
	uint8_t	retained;
	uint8_t dup;
	uint16_t id;
	size_t payloadlen;
	void *payload;
} paho_mqtt_message_t;

typedef struct
{
    char topic_name[PAHO_MQTT_TOPIC_LEN_MAX];
    paho_mqtt_message_t *message;
} paho_message_data_t;

/*******************************************************************************
 ** Variables
 ******************************************************************************/

/*******************************************************************************
 ** Function Declerations
 ******************************************************************************/
typedef void (*paho_mqtt_message_handler_t)(void* client, paho_message_data_t* msg);

/*******************************************************************************
 ** Functions
 ******************************************************************************/
paho_mqtt_error_e paho_mqtt_client(void);
paho_mqtt_error_e paho_mqtt_config(paho_mqtt_config_t *cfg);
paho_mqtt_error_e paho_mqtt_connect(void);
paho_mqtt_error_e paho_mqtt_disconnect(void);
paho_mqtt_error_e paho_mqtt_subscribe(const char *topic, paho_mqtt_qos_e qos, paho_mqtt_message_handler_t handler);
paho_mqtt_error_e paho_mqtt_unsubscribe(const char *topic);
paho_mqtt_error_e paho_mqtt_publish(const char *topic, paho_mqtt_message_t *msg);
paho_mqtt_error_e paho_mqtt_release(void);

#ifdef __cplusplus
}
#endif
#endif/*__PAHO_MQTT_API_H__*/
