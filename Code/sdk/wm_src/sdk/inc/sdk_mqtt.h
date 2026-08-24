/**
 ******************************************************************************
 * @file    sdk_mqtt.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - MQTT client API.
 *
 *          Wraps the bundled Paho/KawaiiMQTT client (components/net/pahomqtt)
 *          into the sdk_* convention. One client is supported at a time and it
 *          is owned by the SDK, so the application never handles a client
 *          pointer; the calls below act on that single client.
 *
 *          Lifecycle:
 *
 *              sdk_mqtt_init()          allocate the client
 *              sdk_mqtt_config()        broker identity, credentials, TLS
 *              sdk_mqtt_connect()       TCP/TLS connect + MQTT CONNECT
 *              sdk_mqtt_subscribe()     per-topic receive callbacks
 *              sdk_mqtt_publish()       send payloads
 *              sdk_mqtt_disconnect()    MQTT DISCONNECT, client kept
 *              sdk_mqtt_deinit()        release the client
 *
 *          A PDP context must be up before sdk_mqtt_connect() (the client uses
 *          the TCP/IP stack directly - see sdk_network.h). Once connected the
 *          library runs its own keep-alive/receive task, which is where
 *          subscribe callbacks are invoked.
 *
 *          Concurrency: publish/subscribe/unsubscribe are safe to call from any
 *          task (the client serialises transmission internally). The lifecycle
 *          calls - init/config/connect/disconnect/deinit - are not mutually
 *          thread-safe and should be driven from a single task.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __SDK_MQTT_H__
#define __SDK_MQTT_H__

#include "sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** Functions
******************************************************************************/
/**
 * @brief  Allocate the MQTT client and bring it to the configurable state.
 *         Idempotent: a second call while a client exists succeeds and changes
 *         nothing. The client allocates its own receive/transmit buffers, so
 *         this needs a few kilobytes of heap.
 * @return SdkResult - 0 success; SDK_RESULT_ERROR if the client could not be
 *                     allocated.
 */
SdkResult sdk_mqtt_init(void);

/**
 * @brief  Set the broker address, client identity, credentials and TLS material.
 *         Must be called before sdk_mqtt_connect(); calling it while connected
 *         is rejected, since the library reads these fields at connect time.
 *         The identity strings are copied; the PEM blobs are not (see
 *         SdkMqttConfig).
 * @param  cfg  configuration; host, port and client_id are required.
 * @return SdkResult - 0 success; SDK_RESULT_INVALID_PARAM on a NULL argument, a
 *                     missing required field, or a field longer than its
 *                     SDK_MQTT_*_MAX bound; SDK_RESULT_NOT_INITIALIZED before
 *                     sdk_mqtt_init(); SDK_RESULT_BUSY while connected.
 */
SdkResult sdk_mqtt_config(const SdkMqttConfig *cfg);

/**
 * @brief  Open the transport and complete the MQTT CONNECT handshake. Blocks
 *         until the broker answers or the attempt fails (a few seconds). On
 *         success the library's keep-alive task is started.
 * @return SdkResult - 0 success; SDK_RESULT_NOT_INITIALIZED before
 *                     sdk_mqtt_init(); SDK_RESULT_INVALID_PARAM if
 *                     sdk_mqtt_config() has not been called; SDK_RESULT_TIMEOUT
 *                     if the broker did not answer; SDK_RESULT_ERROR otherwise.
 *                     Use sdk_mqtt_last_error() for the underlying reason (a
 *                     positive value is the broker's CONNACK refusal code).
 */
SdkResult sdk_mqtt_connect(void);

/**
 * @brief  Send an MQTT DISCONNECT and drop the transport. The client itself is
 *         kept, so sdk_mqtt_connect() can be called again (optionally after
 *         another sdk_mqtt_config()). Subscriptions do not survive.
 * @return SdkResult - 0 success; SDK_RESULT_NOT_INITIALIZED before
 *                     sdk_mqtt_init(); negative on failure.
 */
SdkResult sdk_mqtt_disconnect(void);

/**
 * @brief  Subscribe to a topic filter and register the callback for messages
 *         matching it. MQTT wildcards ('+' single level, '#' multi level) are
 *         accepted. Subscriptions are dropped by sdk_mqtt_disconnect() and must
 *         be re-established after reconnecting.
 * @param  topic  topic filter, NUL-terminated, at most SDK_MQTT_TOPIC_MAX-1.
 * @param  qos    maximum QoS to receive at.
 * @param  cb     callback invoked from the client's receive task; must not be
 *                NULL and must not block.
 * @return SdkResult - 0 success; SDK_RESULT_INVALID_PARAM on a NULL/oversized
 *                     topic or NULL callback; SDK_RESULT_NOT_INITIALIZED if no
 *                     client exists or it is not connected; negative on failure.
 */
SdkResult sdk_mqtt_subscribe(const char *topic, SdkMqttQos qos, SdkMqttMsgCallback cb);

/**
 * @brief  Unsubscribe from a topic filter and drop its callback.
 * @param  topic  the same filter passed to sdk_mqtt_subscribe().
 * @return SdkResult - 0 success; SDK_RESULT_INVALID_PARAM on a NULL topic;
 *                     SDK_RESULT_NOT_INITIALIZED if not connected; negative on
 *                     failure.
 */
SdkResult sdk_mqtt_unsubscribe(const char *topic);

/**
 * @brief  Publish a payload to a topic. Blocks until the packet has been handed
 *         to the transport; for QoS 1/2 the acknowledgement is tracked in the
 *         background rather than waited for, so a success here means "sent",
 *         not "acknowledged".
 * @param  topic        destination topic (no wildcards), NUL-terminated.
 * @param  payload      payload bytes; may be binary (not required to be a
 *                      NUL-terminated string).
 * @param  payload_len  payload length in bytes, 1..SDK_MQTT_PAYLOAD_MAX.
 * @param  qos          delivery guarantee.
 * @param  retained     TRUE to ask the broker to retain this as the topic's
 *                      last-known-good message.
 * @return SdkResult - 0 success; SDK_RESULT_INVALID_PARAM on a NULL/empty
 *                     argument or an oversized payload;
 *                     SDK_RESULT_NOT_INITIALIZED if not connected;
 *                     SDK_RESULT_ERROR on a transmit failure.
 */
SdkResult sdk_mqtt_publish(const char *topic, const void *payload,
                           UINT32 payload_len, SdkMqttQos qos, BOOL retained);

/**
 * @brief  Report whether the MQTT session is currently up. Tracks the library's
 *         own connection state, so it also goes FALSE when the broker or the
 *         network drops the link rather than only on sdk_mqtt_disconnect().
 * @return TRUE when connected; FALSE otherwise (including before
 *         sdk_mqtt_init()).
 */
BOOL sdk_mqtt_is_connected(void);

/**
 * @brief  Get the raw result code the underlying MQTT library returned from the
 *         most recent sdk_mqtt_* call, for diagnostics beyond SdkResult.
 *         Negative values are library errors; 1..5 after a failed
 *         sdk_mqtt_connect() is the broker's CONNACK refusal code (1 protocol
 *         version, 2 client id rejected, 3 server unavailable, 4 bad user name
 *         or password, 5 not authorised).
 * @return the last underlying code; 0 if the last call succeeded.
 */
INT32 sdk_mqtt_last_error(void);

/**
 * @brief  Disconnect if needed and release the client and its buffers. After
 *         this, sdk_mqtt_init() must be called again before anything else.
 *         Idempotent: succeeds when no client exists.
 * @return SdkResult - 0 success; negative if the client could not be released.
 */
SdkResult sdk_mqtt_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __SDK_MQTT_H__ */
