/**
 ******************************************************************************
 * @file    wm_sdk_mqtt.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - MQTT client API.
 *
 *          Wraps the bundled Paho/KawaiiMQTT client (components/net/pahomqtt)
 *          into the wm_sdk_* convention. One client is supported at a time and it
 *          is owned by the SDK, so the application never handles a client
 *          pointer; the calls below act on that single client.
 *
 *          Lifecycle:
 *
 *              wm_sdk_mqtt_init()          allocate the client
 *              wm_sdk_mqtt_config()        broker identity, credentials, TLS
 *              wm_sdk_mqtt_connect()       TCP/TLS connect + MQTT CONNECT
 *              wm_sdk_mqtt_subscribe()     per-topic receive callbacks
 *              wm_sdk_mqtt_publish()       send payloads
 *              wm_sdk_mqtt_disconnect()    MQTT DISCONNECT, client kept
 *              wm_sdk_mqtt_deinit()        release the client
 *
 *          A PDP context must be up before wm_sdk_mqtt_connect() (the client uses
 *          the TCP/IP stack directly - see wm_sdk_network.h). Once connected the
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

#ifndef __WM_SDK_MQTT_H__
#define __WM_SDK_MQTT_H__

#include "wm_sdk_types.h"

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
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_ERROR if the client could not be
 *                     allocated.
 */
wm_SdkResult wm_sdk_mqtt_init(void);

/**
 * @brief  Set the broker address, client identity, credentials and TLS material.
 *         Must be called before wm_sdk_mqtt_connect(); calling it while connected
 *         is rejected, since the library reads these fields at connect time.
 *         The identity strings are copied; the PEM blobs are not (see
 *         wm_SdkMqttConfig).
 * @param  cfg  configuration; host, port and client_id are required.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL argument, a
 *                     missing required field, or a field longer than its
 *                     WM_SDK_MQTT_*_MAX bound; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_mqtt_init(); WM_SDK_RESULT_BUSY while connected.
 */
wm_SdkResult wm_sdk_mqtt_config(const wm_SdkMqttConfig *cfg);

/**
 * @brief  Open the transport and complete the MQTT CONNECT handshake. Blocks
 *         until the broker answers or the attempt fails (a few seconds). On
 *         success the library's keep-alive task is started.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_mqtt_init(); WM_SDK_RESULT_INVALID_PARAM if
 *                     wm_sdk_mqtt_config() has not been called; WM_SDK_RESULT_TIMEOUT
 *                     if the broker did not answer; WM_SDK_RESULT_ERROR otherwise.
 *                     Use wm_sdk_mqtt_last_error() for the underlying reason (a
 *                     positive value is the broker's CONNACK refusal code).
 */
wm_SdkResult wm_sdk_mqtt_connect(void);

/**
 * @brief  Send an MQTT DISCONNECT and drop the transport. The client itself is
 *         kept, so wm_sdk_mqtt_connect() can be called again (optionally after
 *         another wm_sdk_mqtt_config()). Subscriptions do not survive.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_mqtt_init(); negative on failure.
 */
wm_SdkResult wm_sdk_mqtt_disconnect(void);

/**
 * @brief  Subscribe to a topic filter and register the callback for messages
 *         matching it. MQTT wildcards ('+' single level, '#' multi level) are
 *         accepted. Subscriptions are dropped by wm_sdk_mqtt_disconnect() and must
 *         be re-established after reconnecting.
 * @param  topic  topic filter, NUL-terminated, at most WM_SDK_MQTT_TOPIC_MAX-1.
 * @param  qos    maximum QoS to receive at.
 * @param  cb     callback invoked from the client's receive task; must not be
 *                NULL and must not block.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL/oversized
 *                     topic or NULL callback; WM_SDK_RESULT_NOT_INITIALIZED if no
 *                     client exists or it is not connected; negative on failure.
 */
wm_SdkResult wm_sdk_mqtt_subscribe(const char *topic, wm_SdkMqttQos qos, wm_SdkMqttMsgCallback cb);

/**
 * @brief  Unsubscribe from a topic filter and drop its callback.
 * @param  topic  the same filter passed to wm_sdk_mqtt_subscribe().
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL topic;
 *                     WM_SDK_RESULT_NOT_INITIALIZED if not connected; negative on
 *                     failure.
 */
wm_SdkResult wm_sdk_mqtt_unsubscribe(const char *topic);

/**
 * @brief  Publish a payload to a topic. Blocks until the packet has been handed
 *         to the transport; for QoS 1/2 the acknowledgement is tracked in the
 *         background rather than waited for, so a success here means "sent",
 *         not "acknowledged".
 * @param  topic        destination topic (no wildcards), NUL-terminated.
 * @param  payload      payload bytes; may be binary (not required to be a
 *                      NUL-terminated string).
 * @param  payload_len  payload length in bytes, 1..WM_SDK_MQTT_PAYLOAD_MAX.
 * @param  qos          delivery guarantee.
 * @param  retained     TRUE to ask the broker to retain this as the topic's
 *                      last-known-good message.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL/empty
 *                     argument or an oversized payload;
 *                     WM_SDK_RESULT_NOT_INITIALIZED if not connected;
 *                     WM_SDK_RESULT_ERROR on a transmit failure.
 */
wm_SdkResult wm_sdk_mqtt_publish(const char *topic, const void *payload,
                           UINT32 payload_len, wm_SdkMqttQos qos, BOOL retained);

/**
 * @brief  Report whether the MQTT session is currently up. Tracks the library's
 *         own connection state, so it also goes FALSE when the broker or the
 *         network drops the link rather than only on wm_sdk_mqtt_disconnect().
 * @return TRUE when connected; FALSE otherwise (including before
 *         wm_sdk_mqtt_init()).
 */
BOOL wm_sdk_mqtt_is_connected(void);

/**
 * @brief  Get the raw result code the underlying MQTT library returned from the
 *         most recent wm_sdk_mqtt_* call, for diagnostics beyond wm_SdkResult.
 *         Negative values are library errors; 1..5 after a failed
 *         wm_sdk_mqtt_connect() is the broker's CONNACK refusal code (1 protocol
 *         version, 2 client id rejected, 3 server unavailable, 4 bad user name
 *         or password, 5 not authorised).
 * @return the last underlying code; 0 if the last call succeeded.
 */
INT32 wm_sdk_mqtt_last_error(void);

/**
 * @brief  Disconnect if needed and release the client and its buffers. After
 *         this, wm_sdk_mqtt_init() must be called again before anything else.
 *         Idempotent: succeeds when no client exists.
 * @return wm_SdkResult - 0 success; negative if the client could not be released.
 */
wm_SdkResult wm_sdk_mqtt_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __WM_SDK_MQTT_H__ */
