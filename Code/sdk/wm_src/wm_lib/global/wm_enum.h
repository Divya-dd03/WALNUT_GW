/**
  ******************************************************************************
  * @file    wm_enum.h
  * @brief   External function calls.
  * @author  Walnut Medical
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */

#ifndef __WM_ENUM_H__
#define __WM_ENUM_H__

/* Include statements */

/* OS */

/* URC */
typedef enum
{
    WM_PDP_ACTIVE_STATE = 1,
    WM_PDP_INACTIVE_STATE = 2,
    WM_NET_ACTIVE = 3,
    WM_NET_DISCONNECTED = 4,
    WM_SIM_INSERTED = 5,
    WM_SIM_REMOVED = 6,
    WM_ISDN_FOUND = 7,
    WM_TIME_DATA_READY = 8,
    WM_CPIN_ERROR = 14,
    WM_SIM_READY = 15,
    WM_USB_PLUGGED = 16,  /*MSG WILL ARRIVE WHEN USB IS PLUGGED*/
    WM_USB_REMOVED = 17,
    WM_HTTP_FD_STARTED = 18,  /*FILE DOWNLAOD STARTED AFTER HTTP CALL*/
    WM_HTTP_FD_HALF_DONE = 19,  /*FILE DOWNLAOD JUST CROSSED 50% */
    WM_HTTP_FD_COMPLETED = 20,  /*FILE DOWNLAOD JUST COMPLETED  */
    WM_HTTP_FD_ERROR = 21,  /*FILE DOWNLAOD ERROR  */
    WM_FIRMWARE_BT = 22,  /*FIRMWARE BUILD TIME */
    WM_AUDIO_VOL_MAX = 23,  /*MSG ON VOL MAX*/
    WM_AUDIO_VOL_MIN = 24,  /*MSG ON VOL MIN*/
    WM_ISDN_READY = 25,  /*ISDN READY IN g_isdn*/
    WM_OPERATOR_STR_READY = 26,  /*OPER AVAILABLE READY IN g_str_operator*/
    WM_SIM_OPEN_TIMEOUT = 27,  /*SIM OPEN MORE THAN TIME SET BY TM_OUT_SIM_OFF_STATE*/
    WM_MQTT_CONNECTION_START = 28,
    WM_MQTT_CONNECTION_SUCCESSFUL = 29,
    WM_MQTT_CONNECTION_FAILED = 30,
    WM_MQTT_SUBSCRIBE_FAILED = 31,
    WM_MQTT_SUBSCRIBE_SUCCESSFUL = 32,
    WM_MQTT_ONBOARDING_FAILED = 33,
    WM_MQTT_ONBOARDING_SUCCESSFUL = 34,
    WM_MQTT_DISCONNECTED = 35,
    WM_PING_FAILED = 36,
    WM_PING_SUCCESS = 37,
    WM_HTTPS_TIME_SYNC_FAILED = 38,
    WM_HTTPS_TIME_SYNC_SUCCESSFUL = 39,
    WM_NO_CONNECTION_TIMEOUT = 40,
    WM_NO_CONNECTION_RESET = 41,
    WM_NTP_TIME_SYNC_FAILED = 42,
    WM_NTP_TIME_SYNC_SUCCESSFUL = 43,
    WM_AUDIO_PLAY_STARTED = 100,
    WM_AUDIO_PLAY_COMPLETED = 101,
    WM_AUDIO_PLAY_ERROR = 102,   /*FILE DOES NOT EXIST*/
    WM_AUDIO_PLAY_SYSTEM_ERR_STR = 103,
    WM_AUDIO_PLAY_STOPPED = 104,
    WM_UPDATE_STARTED = 105,  /*APP UPDATE STARTED*/
    WM_UPDATE_STOPPED = 106, /*APP UPDATE STOPPED*/
    WM_AUDIO_STOPPED_URC = 107
}WM_URC_TASK_SELECTION_CASE;

/* UI App - Common Gateway SDK demo menu (one entry per wm_sdk_* API section,
 * matching the WEGW API requirements document). */
typedef enum
{
    WM_DEMO_NETWORK = 1,   /* wm_sdk_network_*  */
    WM_DEMO_SIM,           /* wm_sdk_sim_*      */
    WM_DEMO_SMS_CONFIG,    /* wm_sdk_sms_set_format + set_charset + set_new_msg_ind + queue */
    WM_DEMO_SMS_STORAGE,   /* wm_sdk_sms_get_storage_status */
    WM_DEMO_SMS_SEND,      /* wm_sdk_sms_send               */
    WM_DEMO_SMS_READ,      /* wm_sdk_sms_read (index 1)     */
    WM_DEMO_SMS_DELETE,    /* wm_sdk_sms_delete (index 1)   */
    WM_DEMO_SMS_DELETE_ALL,/* wm_sdk_sms_delete_all         */
    WM_DEMO_SMS_DRAIN,     /* wm_sdk_sms_msgq_poll + drain  */
    WM_DEMO_GPS_CONFIG,    /* wm_sdk_gps_set_mode/_nmea_rate/_enable_nmea_output   */
    WM_DEMO_GPS_FIX,       /* wm_sdk_gps_get_navdata (snapshot)                    */
    WM_DEMO_GPS_STREAM,    /* wm_sdk_gps_set_fix_callback (live fixes)             */
    WM_DEMO_GPS_NMEA,      /* wm_sdk_gps_set_nmea_callback (raw sentences)         */
    WM_DEMO_GPS_POWER_OFF, /* wm_sdk_gps_set_power_status(0)                       */
    WM_DEMO_TCP,           /* wm_sdk_tcp_*      */
    /* HTTPS demos, all in wm_ui_https.c. */
    WM_DEMO_HTTPS,         /* wm_sdk_https_*: GET, synchronous                   */
    WM_DEMO_HTTPS_POST,    /* wm_sdk_https_*: POST a JSON body + api-key header  */
    WM_DEMO_HTTPS_ASYNC,   /* wm_sdk_https_*: async GET, result on a queue       */
    WM_DEMO_HTTPS_DOWNLOAD,/* wm_sdk_https_download_*: ranged fetch + SHA-256    */
    /* OTA / DFOTA demos, all in wm_ui_ota.c. */
    WM_DEMO_OTA_VERSION,   /* wm_sdk_ota_get_app_version / _get_sdk_version       */
    WM_DEMO_OTA_UPDATE,    /* wm_sdk_ota_*: download + verify + apply APP image   */
    WM_DEMO_DFOTA_UPDATE,  /* wm_sdk_ota_*: download + verify + apply kernel patch*/
    WM_DEMO_UART,          /* wm_sdk_uart_*     */
    WM_DEMO_FILE,          /* wm_sdk_file_*     */
    WM_DEMO_STORAGE,       /* wm_sdk_storage_*  */
    WM_DEMO_OS,            /* wm_sdk_os / rtos  */
    WM_DEMO_DEVICE,        /* wm_sdk_device_*   */
    WM_DEMO_GPIO,          /* wm_sdk_gpio_*     */
    WM_DEMO_ADC,           /* wm_sdk_adc_*      */
    WM_DEMO_I2C,           /* wm_sdk_i2c_*      */
    WM_DEMO_URC,           /* wm_sdk_urc_*      */
    WM_DEMO_SYSTEM,        /* wm_sdk_system_*   */
    WM_DEMO_LOG,           /* wm_sdk_log_*      */
    /* MQTT lives in its own demo file (wm_ui_mqtt.c). */
    WM_DEMO_MQTT,          /* wm_sdk_mqtt_*: connect + stream raw NMEA (toggle)  */
    WM_DEMO_LED,           /* wm_sdk_led_*: R/G/B indicator LEDs + blink         */
    /* BLE demos, all in wm_ui_ble.c. */
    WM_DEMO_BLE_SCAN,      /* wm_sdk_ble_scan_*: filtered scan (toggle)          */
    WM_DEMO_BLE_READ,      /* fuel readings + one Autoguard health exchange   */
    WM_DEMO_BLE_MONITOR,   /* repeat the read on a period (toggle)            */
    WM_DEMO_BLE_POWER_OFF  /* wm_sdk_ble_set_power_status(0)                     */
}WM_TASK_SELECTION;

/* HTTP Control */
/* Audio Control */
typedef enum
{
    WM_AUDIO_PLAY_FREE = 0,
    WM_AUDIO_PLAY_RUN,
    WM_AUDIO_PLAY_END,
    WM_AUDIO_PLAY_STOP,
    WM_AUDIO_PLAY_ERR,
} AUD_PLAY_STATUS;
/* USB Control */

/* Timer Control*/
typedef enum {
    TIMER_NOT_CREATED = 0,
    TIMER_RUNNING,
    TIMER_STANDBY,
    TIMER_START_ERR,
    TIMER_STOP_ERR,
    TIMER_CREATE_ERR,
    TIMER_RESOURCE_OVERLOAD,   /*SDKSBV68M supports 12 timers at max*/
}WM_tmr_resp_code;

// MBEDTLS
typedef enum {
    WM_AES_ENCRYPT = MBEDTLS_AES_ENCRYPT,  // 1 (Defined in aes.h)
    WM_AES_DECRYPT = MBEDTLS_AES_DECRYPT   // 0 (Defined in aes.h)
} WM_AES_OPERATION;

typedef enum {
    WM_AES_MODE_CBC = 0,   // Cipher Block Chaining
    WM_AES_MODE_ECB,       // Electronic Codebook
    WM_AES_MODE_CFB128,    // Cipher Feedback (128-bit) - Not supported
    WM_AES_MODE_OFB,       // Output Feedback - Not supported
    WM_AES_MODE_CTR,       // Counter Mode - Not supported
    WM_AES_MODE_GCM        // Galois/Counter Mode - Not supported
} WM_AES_MODE;

typedef enum {
    WM_AES_KEY_128 = 128,  // 128-bit key (16 bytes)
    WM_AES_KEY_192 = 192,  // 192-bit key (24 bytes)
    WM_AES_KEY_256 = 256   // 256-bit key (32 bytes)
} WM_AES_KEY_SIZE;

typedef enum {
    WM_BASE64_ENCODE = 0,  // Encode mode
    WM_BASE64_DECODE       // Decode mode
} WM_BASE64_OPERATION;

// Time SYnc Modes
typedef enum {
    WM_TIME_SYNC_MODE_INVALID = -1,
    WM_TIME_SYNC_MODE_HTTPS = 0,
    WM_TIME_SYNC_MODE_NTP,
    WM_TIME_SYNC_MODE_LOCAL,  // e.g., device-set or host time
    WM_TIME_SYNC_MODE_MAX
} WM_TIME_SYNC_MODE_E;

typedef enum
{
    WM_TIMER_ID_1 = 5,
    WM_TIMER_ID_2 = 6,
    WM_TIMER_ID_3 = 7,
    WM_TIMER_ID_4 = 8,
    WM_TIMER_ID_5 = 9,
    WM_TIMER_ID_6 = 10,
    WM_TIMER_ID_7 = 11,
    WM_TIMER_ID_8 = 12,
    TIMER_MAX_ID = 13
} WM_TIMER_ID;

#endif
