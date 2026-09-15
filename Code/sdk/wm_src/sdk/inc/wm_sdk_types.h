/**
 ******************************************************************************
 * @file    wm_sdk_types.h
 * @author  Walnut Medical
 * @brief   Shared types, result codes and data structures for the Common
 *          Gateway SDK Platform Abstraction API (wm_sdk_*).
 *
 *          This header is the single foundation included by every wm_sdk_*.h
 *          domain header. It defines the standard return type (wm_SdkResult) and
 *          the shared out-parameter structures referenced across the API, as
 *          specified in WEGW_API_REQUIREMENTS_V0 ("APIs used in Common Gateway").
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __WM_SDK_TYPES_H__
#define __WM_SDK_TYPES_H__

/*******************************************************************************
** Header Files
******************************************************************************/
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "typedef.h"     /* UINT8/16/32/64, INT8/16/32, BOOL */

#ifdef __cplusplus
extern "C"
{
#endif

/* typedef.h defines UINT64 but not INT64; the API needs a signed 64-bit type. */
#ifndef WM_SDK_HAS_INT64
#define WM_SDK_HAS_INT64
typedef signed long long INT64;
#endif

/* BOOL is provided by the vendor sc_def.h (guarded by #ifndef BOOL). Define it
 * here so the public sdk headers are self-sufficient when included first; the
 * same #ifndef guard keeps sc_def.h from re-defining it. */
#ifndef BOOL
#define BOOL bool
#endif

/*******************************************************************************
** Standard return type
**
** wm_SdkResult: 0 = success, negative = failure. All wm_sdk_* functions that do not
** follow BSD-socket conventions (see wm_sdk_tcp.h) return this type.
******************************************************************************/
typedef INT32 wm_SdkResult;

#define WM_SDK_RESULT_SUCCESS          (0)   /* operation succeeded            */
#define WM_SDK_RESULT_ERROR            (-1)  /* generic failure                */
#define WM_SDK_RESULT_TIMEOUT          (-2)  /* operation timed out            */
#define WM_SDK_RESULT_INVALID_PARAM    (-3)  /* bad / NULL argument            */
#define WM_SDK_RESULT_NOT_INITIALIZED  (-4)  /* subsystem not initialised      */
#define WM_SDK_RESULT_BUSY             (-8)  /* retry later (in progress)      */
#define WM_SDK_RESULT_NOT_SUPPORTED    (-9)  /* vendor does not implement it   */

/*******************************************************************************
** NETWORK types
******************************************************************************/
/* Circuit-/packet-switched registration state (AT+CREG / AT+CGREG). */
typedef enum
{
    WM_SDK_NET_REG_NOT_REGISTERED = 0, /* not registered, not searching        */
    WM_SDK_NET_REG_HOME           = 1, /* registered, home network             */
    WM_SDK_NET_REG_SEARCHING      = 2, /* not registered, searching            */
    WM_SDK_NET_REG_DENIED         = 3, /* registration denied                  */
    WM_SDK_NET_REG_UNKNOWN        = 4, /* unknown                              */
    WM_SDK_NET_REG_ROAMING        = 5, /* registered, roaming                  */
} wm_SdkNetRegStatus;

/* Packet-domain attach state (AT+CGATT). */
typedef enum
{
    WM_SDK_NET_DETACHED = 0,
    WM_SDK_NET_ATTACHED = 1,
} wm_SdkNetAttStatus;

/* IP address assigned to a PDP context (AT+CGPADDR). */
typedef struct
{
    char ipv4[16];   /* dotted-decimal, NUL-terminated ("" if none)        */
    char ipv6[64];   /* colon-hex, NUL-terminated ("" if none)             */
} wm_SdkIpAddress;

/* Radio info bundled for the GPS/telemetry packet (signal + serving cell). */
typedef struct
{
    int  csq;        /* signal quality 0..31 / 99 unknown                   */
    int  rsrp;       /* dBm                                                 */
    int  rsrq;       /* dB                                                  */
    int  mcc;        /* mobile country code                                 */
    int  mnc;        /* mobile network code                                 */
    int  lac;        /* location / tracking area code                       */
    int  cell_id;    /* serving cell id                                     */
    BOOL valid;      /* TRUE when the fields above are populated            */
} wm_SdkNetworkGpsRadioInfo;

/* Network / RTC wall-clock time. */
typedef struct
{
    UINT16 year;             /* full year, e.g. 2026                        */
    UINT8  month;            /* 1..12                                       */
    UINT8  day;              /* 1..31                                       */
    UINT8  hour;             /* 0..23                                       */
    UINT8  minute;           /* 0..59                                       */
    UINT8  second;           /* 0..59                                       */
    INT8   tz_quarter_hours; /* timezone offset in quarter-hours (0 unknown)*/
} wm_SdkNetworkTime;

/*******************************************************************************
** SIM types
******************************************************************************/
typedef enum
{
    WM_SDK_SIM_PRESENT = 0,   /* SIM present but not yet ready (PIN/PUK locked) */
    WM_SDK_SIM_ABSENT  = 1,   /* no SIM detected                               */
    WM_SDK_SIM_READY   = 2,   /* SIM ready (PIN ok)                            */
    WM_SDK_SIM_ERROR   = 3,   /* SIM error / unknown                          */
} wm_SdkSimStatus;

/*******************************************************************************
** SMS types
**
** The SMS operations that take a message queue (wm_sdk_sms_read / wm_sdk_sms_delete /
** wm_sdk_sms_delete_all) report their outcome as one wm_SdkSmsMessage, and incoming
** messages are reported the same way. A queue used with the SMS API must be
** created with msg_size == sizeof(wm_SdkSmsMessage). Each received message's
** 'text' pointer is heap-owned by the caller - release it with
** wm_sdk_sms_msg_free() once handled.
******************************************************************************/
/* Message format (wm_sdk_sms_set_format). */
typedef enum
{
    WM_SDK_SMS_FORMAT_PDU  = 0,   /* PDU mode  */
    WM_SDK_SMS_FORMAT_TEXT = 1,   /* text mode */
} wm_SdkSmsFormat;

/* Character set (wm_sdk_sms_set_charset). */
typedef enum
{
    WM_SDK_SMS_CHARSET_GSM  = 0,  /* GSM 7-bit default alphabet */
    WM_SDK_SMS_CHARSET_UCS2 = 1,  /* UCS2 (16-bit)              */
    WM_SDK_SMS_CHARSET_IRA  = 2,  /* IRA / ASCII                */
} wm_SdkSmsCharset;

/* Storage-area selector (wm_sdk_sms_read). */
typedef enum
{
    WM_SDK_SMS_STORAGE_SM = 0,    /* SIM storage         */
    WM_SDK_SMS_STORAGE_ME = 1,    /* module/phone memory */
    WM_SDK_SMS_STORAGE_MT = 2,    /* combined            */
} wm_SdkSmsStorage;

/* Maximum text-mode body length for wm_sdk_sms_send(), in characters (a single
 * GSM-7 segment; longer text is rejected - no concatenation/UDH). */
#define WM_SDK_SMS_MAX_BODY_LEN  (160u)

/* Kind of event delivered to the SMS message queue. */
typedef enum
{
    WM_SDK_SMS_EVT_INCOMING      = 0,  /* a new message arrived                   */
    WM_SDK_SMS_EVT_READ_RESULT   = 1,  /* result of wm_sdk_sms_read()                */
    WM_SDK_SMS_EVT_DELETE_RESULT = 2,  /* result of wm_sdk_sms_delete / _delete_all()*/
} wm_SdkSmsEventType;

/* One SMS event/result received from the SMS message queue. Kept to 16 bytes
 * (the platform SIM_MSG_T convention: the kernel message queue only accepts
 * small fixed messages), so the variable-length text is carried by a heap
 * pointer rather than inline.
 *
 * 'text' is allocated by the SDK and OWNED BY THE RECEIVER: after handling the
 * message call wm_sdk_sms_msg_free() to release it. It is NULL for events that
 * carry no text (delete results, or a read / incoming that failed). */
typedef struct
{
    wm_SdkSmsEventType type;    /* which kind of event                       */
    wm_SdkResult       status;  /* operation result (0 = ok)                 */
    INT32           index;   /* message index (-1 if n/a)                 */
    char           *text;    /* heap-owned message text; NULL if none     */
} wm_SdkSmsMessage;

/*******************************************************************************
** GPS / GNSS types
******************************************************************************/
/* Parsed navigation data from the GNSS receiver. */
typedef struct
{
    BOOL           fix_valid;   /* TRUE when a valid position fix is present */
    double         latitude;    /* decimal degrees, +N                      */
    double         longitude;   /* decimal degrees, +E                      */
    float          altitude_m;  /* metres MSL                               */
    float          speed_kmh;   /* ground speed, km/h                       */
    float          course_deg;  /* true track over ground, degrees          */
    UINT8          satellites;  /* satellites used in the fix               */
    wm_SdkNetworkTime utc;         /* UTC date/time of the fix                 */
} wm_SdkGpsNavData;

/*******************************************************************************
** BLE types
**
** See wm_sdk_ble.h. Payloads are carried as raw bytes; any framing on top of them
** belongs to the application.
******************************************************************************/
#define WM_SDK_BLE_ADDR_STR_LEN   (18u)  /* "AA:BB:CC:DD:EE:FF" + NUL            */
#define WM_SDK_BLE_NAME_MAX       (21u)  /* 20 UTF-8 bytes + NUL                 */
#define WM_SDK_BLE_ADV_MAX        (31u)  /* one BLE advertising payload          */
#define WM_SDK_BLE_UUID_STR_LEN   (5u)   /* 4 hex digits + NUL (16-bit UUID only)*/
#define WM_SDK_BLE_FILTER_MAX     (8u)   /* addresses the scan filter can hold   */

/* Scan mode for wm_sdk_ble_scan_start(). */
typedef enum
{
    WM_SDK_BLE_SCAN_OFF     = 0,   /* scanning stopped                           */
    WM_SDK_BLE_SCAN_DECODED = 1,   /* name / UUID / manufacturer decoded for you  */
    WM_SDK_BLE_SCAN_RAW     = 2,   /* whole advertising payload per report       */
} wm_SdkBleScanMode;

/* Link event delivered to an wm_sdk_ble_evt_cb_t. */
typedef enum
{
    WM_SDK_BLE_EVT_READY        = 0,  /* BLE (re)initialised; no link exists     */
    WM_SDK_BLE_EVT_CONNECTED    = 1,  /* link ready on the reported channel      */
    WM_SDK_BLE_EVT_DISCONNECTED = 2,  /* the channel went down                   */
} wm_SdkBleEvent;

/* One scan report. In RAW mode 'data'/'len' hold the advertising payload (walk
 * it with wm_sdk_ble_adv_find) and 'name' is empty; in DECODED mode the reverse. */
typedef struct
{
    char  addr[WM_SDK_BLE_ADDR_STR_LEN];  /* "AA:BB:CC:DD:EE:FF"                */
    UINT8 addr_type;                   /* peer address type                  */
    INT16 rssi;                        /* dBm, negative                      */
    UINT8 pkt_type;                    /* 0 = advertisement, 1 = scan response*/
    UINT8 data[WM_SDK_BLE_ADV_MAX];       /* advertising payload (RAW mode)     */
    UINT8 len;                         /* bytes valid in 'data'              */
    char  name[WM_SDK_BLE_NAME_MAX];      /* advertised name (DECODED mode)     */
} wm_SdkBleScanResult;

/*******************************************************************************
** TCP types (BSD socket conventions - see wm_sdk_tcp.h)
******************************************************************************/
/* Socket event codes delivered to wm_SdkTcpSocketCallback. */
#define WM_SDK_TCP_EVENT_CONNECT   (1)
#define WM_SDK_TCP_EVENT_RECV      (2)
#define WM_SDK_TCP_EVENT_CLOSE     (3)
#define WM_SDK_TCP_EVENT_ERROR     (4)

/* Address family and socket type for wm_sdk_tcp_socket_create_with_callback(), so
 * app code does not have to pull in the lwIP headers just to name them. Values
 * match lwip/sockets.h. */
#define WM_SDK_AF_INET             (2)
#define WM_SDK_SOCK_STREAM         (1)

/* Async socket event callback: fd is the socket, event is one of the
 * WM_SDK_TCP_EVENT_* codes. arg is reserved and always NULL - the create call
 * takes no user pointer, so key per-connection context off fd. */
typedef void (*wm_SdkTcpSocketCallback)(int fd, int event, void *arg);

/*******************************************************************************
** HTTPS types
**
** The HTTPS client runs on top of the TCP/IP stack, so a PDP context must be up
** (see wm_sdk_network.h) before a request can succeed. Requests are driven through
** a small set of independent sessions, each addressed by an "SSL index" - see
** wm_sdk_https.h.
******************************************************************************/
/* Number of general-purpose request sessions. Valid ssl_index values are
 * 0..WM_SDK_HTTPS_SESSION_MAX-1, plus WM_SDK_HTTPS_DOWNLOAD_INDEX. */
#define WM_SDK_HTTPS_SESSION_MAX       (2u)

/* Session reserved for the wm_sdk_https_download_* file-transfer calls, which take
 * no index of their own. Point it at the file with wm_sdk_https_set_params(). */
#define WM_SDK_HTTPS_DOWNLOAD_INDEX    (WM_SDK_HTTPS_SESSION_MAX)

/* Field bounds. The URL, the custom-header block and the content type are
 * copied into SDK-owned storage, so anything longer is rejected. */
#define WM_SDK_HTTPS_URL_MAX           (256u)
#define WM_SDK_HTTPS_HEADER_MAX        (512u)
#define WM_SDK_HTTPS_CONTENT_TYPE_MAX  (64u)

/* Most body bytes a single wm_sdk_https_read() can return. A larger body is not
 * truncated: it arrives over successive reads.
 *
 * Do not lower this. It has to stay at or above the underlying client's own
 * read size, or the tail of the first read is dropped instead of being handed
 * over - wm_sdk_https.c asserts the relationship at compile time and explains it. */
#define WM_SDK_HTTPS_RESP_BUF_SIZE     (4608u)

/* Request timeout applied when wm_sdk_https_set_params() is given 0, in seconds. */
#define WM_SDK_HTTPS_DEFAULT_TIMEOUT   (30u)

/* Delivery mode for wm_sdk_https_init(). */
typedef enum
{
    WM_SDK_HTTPS_MODE_SYNC  = 0,  /* wm_sdk_https_action() blocks until the response
                                * headers are in; no queue is used            */
    WM_SDK_HTTPS_MODE_ASYNC = 1,  /* wm_sdk_https_action() returns immediately and a
                                * worker task reports completion on the queue */
} wm_SdkHttpsTxMode;

/* Kind of event delivered to the HTTPS message queue (async mode only). */
typedef enum
{
    WM_SDK_HTTPS_EVT_ACTION_DONE = 0,  /* an wm_sdk_https_action() request finished */
} wm_SdkHttpsEventType;

/* One event received from the HTTPS message queue. It reports the outcome only;
 * the body stays on the session, so read it with wm_sdk_https_read() once this
 * arrives.
 *
 * A queue used with the HTTPS API must be created with
 * msg_size == sizeof(wm_SdkHttpsEvent). */
typedef struct
{
    UINT16    type;       /* wm_SdkHttpsEventType                              */
    UINT16    ssl_index;  /* session the event belongs to                   */
    wm_SdkResult status;     /* operation result (0 = ok)                      */
    INT32     http_code;  /* HTTP status line code, e.g. 200 (0 if none)    */
    UINT32    length;     /* bytes of body buffered and ready to read       */
} wm_SdkHttpsEvent;

/*******************************************************************************
** MQTT types
**
** The MQTT client runs on top of the TCP/IP stack, so a PDP context must be up
** (see wm_sdk_network.h) before wm_sdk_mqtt_connect() can succeed. Exactly one client
** is supported at a time - see wm_sdk_mqtt.h.
******************************************************************************/
/* Delivery guarantee used for publish and subscribe. */
typedef enum
{
    WM_SDK_MQTT_QOS0 = 0,   /* at most once  - fire and forget, no ack          */
    WM_SDK_MQTT_QOS1 = 1,   /* at least once - acknowledged, may be duplicated  */
    WM_SDK_MQTT_QOS2 = 2,   /* exactly once  - two-phase handshake              */
} wm_SdkMqttQos;

/* Field bounds for wm_SdkMqttConfig. The broker-identity strings are copied into
 * SDK-owned storage by wm_sdk_mqtt_config(), so anything longer is rejected. */
#define WM_SDK_MQTT_HOST_MAX       (64u)   /* hostname or dotted-decimal IP     */
#define WM_SDK_MQTT_PORT_MAX       (8u)    /* port as a decimal string          */
#define WM_SDK_MQTT_CLIENT_ID_MAX  (64u)   /* MQTT client identifier            */
#define WM_SDK_MQTT_USER_MAX       (64u)   /* user name                         */
#define WM_SDK_MQTT_PASS_MAX       (64u)   /* password                          */

/* Longest topic the client can report on a received message. Matches the
 * underlying library's limit; longer topics arrive truncated. */
#define WM_SDK_MQTT_TOPIC_MAX      (100u)

/* Largest payload a single wm_sdk_mqtt_publish() can carry, in bytes. */
#define WM_SDK_MQTT_PAYLOAD_MAX    (4096u)

/* Broker identity and session parameters for wm_sdk_mqtt_config().
 *
 * host/port/client_id/user_name/password are copied into SDK-owned storage, so
 * the caller's buffers need not outlive the call. The three PEM certificate
 * blobs are NOT copied (they are large): they stay CALLER-OWNED and must remain
 * valid for as long as the client is connected. */
typedef struct
{
    const char *host;            /* broker hostname or IP (required)         */
    const char *port;            /* broker port, e.g. "1883" (required)      */
    const char *client_id;       /* MQTT client id (required, unique)        */
    const char *user_name;       /* NULL when the broker needs no auth       */
    const char *password;        /* NULL when the broker needs no auth       */
    UINT16      keep_alive_sec;  /* PINGREQ interval; 0 => library default   */
    BOOL        clean_session;   /* TRUE to discard any stored session       */

    /* TLS is selected by supplying ca_cert: NULL means a plain TCP connection.
     * client_cert/client_key are only used for mutual TLS and are ignored
     * unless BOTH are non-NULL. Caller-owned - see the note above. */
    const char *ca_cert;         /* root CA in PEM, or NULL for plain TCP    */
    const char *client_cert;     /* client certificate in PEM, or NULL       */
    const char *client_key;      /* client private key in PEM, or NULL       */
} wm_SdkMqttConfig;

/* One message delivered to an wm_SdkMqttMsgCallback.
 *
 * 'topic' is NUL-terminated. 'payload' is NOT NUL-terminated and is NOT owned
 * by the callback: it points into the client's receive buffer, which is wiped
 * as soon as the callback returns. Copy out whatever must be kept, honouring
 * 'payload_len'. */
typedef struct
{
    const char *topic;        /* topic the message arrived on (NUL-terminated) */
    const void *payload;      /* payload bytes; NOT NUL-terminated             */
    UINT32      payload_len;  /* payload length in bytes                       */
    wm_SdkMqttQos  qos;          /* QoS the message was published with            */
    BOOL        retained;     /* TRUE if the broker replayed a retained message*/
} wm_SdkMqttMessage;

/* Received-message callback, registered per topic by wm_sdk_mqtt_subscribe().
 * Invoked from the MQTT client's own keep-alive/receive task, so it must not
 * block: hand anything slow to an application task. Calling wm_sdk_mqtt_publish()
 * from here is safe but will stall message reception while it transmits. */
typedef void (*wm_SdkMqttMsgCallback)(const wm_SdkMqttMessage *msg);

/*******************************************************************************
** OTA types
******************************************************************************/
/* Which staged image an OTA call operates on. Both stage on the internal C:
 * volume; retrieve the exact path for a type with wm_sdk_ota_get_image_path(). The
 * OTA layer republishes that path into the vendor globals the apply step reads,
 * so the downloaded bytes and the applied bytes cannot drift apart. */
typedef enum
{
    WM_SDK_OTA_IMAGE_APP    = 0,  /* customer application image (C:/customer_app.bin) */
    WM_SDK_OTA_IMAGE_KERNEL = 1,  /* kernel delta patch         (C:/system_patch.bin) */
} wm_SdkOtaImageType;

/* Buffer bound for the version strings returned by wm_sdk_ota_get_app_version() and
 * wm_sdk_ota_get_sdk_version(), including the terminating NUL. Sized to the
 * underlying WM_CUS_APP_VERSION / WM_SDK_VERSION storage. */
#define WM_SDK_OTA_VERSION_MAX     (100u)

/* Length of a SHA-256 digest in lowercase hex, excluding the terminating NUL.
 * wm_sdk_ota_verify_image() requires exactly this many characters. */
#define WM_SDK_OTA_SHA256_HEX_LEN  (64u)

/*******************************************************************************
** UART types
******************************************************************************/
typedef struct
{
    UINT32 baud_rate;     /* e.g. 115200                                    */
    UINT8  data_bits;     /* 5..8                                           */
    UINT8  stop_bits;     /* 1 or 2                                         */
    UINT8  parity;        /* 0=none, 1=odd, 2=even                          */
    UINT8  flow_control;  /* 0=none, 1=RTS/CTS                              */
} wm_SdkUartConfig;

/* Receive callback: invoked from the SDK UART service context (not an ISR)
 * when data has been received on @p port. @p data points to the @p len received
 * bytes (NUL-terminated for convenience) and is valid only for the duration of
 * the call - copy it if it must outlive the callback. @p arg is the user
 * pointer registered with wm_sdk_uart_set_rx_callback(). Keep the handler short. */
typedef void (*wm_SdkUartRxCallback)(UINT32 port, const UINT8 *data, UINT32 len, void *arg);

/*******************************************************************************
** I2C types
******************************************************************************/
typedef enum
{
    WM_SDK_I2C_SPEED_STANDARD = 0,   /* 100 kHz                                */
    WM_SDK_I2C_SPEED_FAST     = 1,   /* 400 kHz                                */
} wm_SdkI2cSpeed;

/*******************************************************************************
** LED types
**
** The board carries three separate indicator LEDs - red, green and blue - each
** driven on its own channel at the platform's standard indicator intensity.
******************************************************************************/
/* Channel bits for wm_sdk_led_set_state(). OR them together to light more than
 * one LED at a time; each channel drives its own LED independently. */
#define WM_SDK_LED_CH_NONE     (0u)
#define WM_SDK_LED_CH_RED      (1u << 0)
#define WM_SDK_LED_CH_GREEN    (1u << 1)
#define WM_SDK_LED_CH_BLUE     (1u << 2)
#define WM_SDK_LED_CH_ALL      (WM_SDK_LED_CH_RED | WM_SDK_LED_CH_GREEN | WM_SDK_LED_CH_BLUE)

/* Blink cadence for wm_sdk_led_set_state(). The platform blink timer runs at one
 * of two fixed periods, so these are the only rates available. */
typedef enum
{
    WM_SDK_LED_BLINK_NONE = 0,   /* steady - no blink                          */
    WM_SDK_LED_BLINK_SLOW = 1,   /* ~500 ms on/off period                      */
    WM_SDK_LED_BLINK_FAST = 2,   /* ~250 ms on/off period                      */
} wm_SdkLedBlinkRate;

/* Pass as the blink count to blink until the next wm_sdk_led_set_state() call. */
#define WM_SDK_LED_BLINK_FOREVER   (0u)

/* Largest finite blink cycle count wm_sdk_led_set_state() accepts. */
#define WM_SDK_LED_BLINK_COUNT_MAX (99u)

/*******************************************************************************
** STORAGE types
******************************************************************************/
/* Stored credential blobs (TLS root CA / client certificate / client key) that
 * can be read and updated at runtime via wm_sdk_storage_cred_read/write(). */
typedef enum
{
    WM_SDK_STORAGE_CRED_ROOT_CA     = 0,  /* server / root CA certificate (PEM) */
    WM_SDK_STORAGE_CRED_CLIENT_CERT = 1,  /* client certificate (PEM)           */
    WM_SDK_STORAGE_CRED_CLIENT_KEY  = 2,  /* client private key (PEM)           */
} wm_SdkStorageCredential;

/* Maximum credential blob size, in bytes, including the terminating NUL. A read
 * buffer must be large enough to hold the stored blob plus its NUL; a write
 * string must fit within this bound. */
#define WM_SDK_STORAGE_CRED_MAX_SIZE   (2048u)

#ifdef __cplusplus
}
#endif

#endif /* __WM_SDK_TYPES_H__ */
