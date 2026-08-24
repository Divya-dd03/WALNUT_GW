/**
 ******************************************************************************
 * @file    wm_custom_def.h
 * @brief   Custom definitions for Walnut Medical API calls.
 * @author  Walnut Medical
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2024 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __WM_CUSTOM_DEF_H__
#define __WM_CUSTOM_DEF_H__

/* Defines */
#define MAX_CONFIG_STRING_LENGTH 64

/* RTC */
typedef t_rtc_new_ms t_rtc;

/*led control*/
struct ledStrucVar
{
    char cred;
    char cgreen;
    char cblue;
    char blink;
};

/* Timer */
typedef int TIMER_t;
typedef void (*TimerCallback)(void *);

typedef struct
{
    TIMER_t timer;
    TimerCallback callback;
} TimerInfo;

typedef struct
{
    sTimerRef timerRef;
    int t_value;
    int cyclic;
    int g_timer_status;
    void(*callBackRoutine);
} timer_param;

typedef struct {
    char topic[250];
    uint8_t qos;
} mqtt_topic_config_t;

// MBEDTLS
typedef struct {
    const unsigned char *input;  // Input data
    size_t input_length;         // Input data length
    unsigned char *output;       // Output buffer
    size_t output_size;          // Output buffer size
    const unsigned char *key;    // AES key
    WM_AES_KEY_SIZE key_size;    // AES key size (128, 192, 256 bits)
    unsigned char iv[MBEDTLS_AES_BLOCK_SIZE];  // IV (Initialization Vector)
    WM_AES_OPERATION mode;       // WM_AES_ENCRYPT or WM_AES_DECRYPT
    WM_AES_MODE aes_mode;        // CBC, ECB, CFB128, OFB, CTR
    unsigned char auth_tag[16];  // Authentication tag (Only for AES-GCM)
} WM_AES_T;

typedef struct {
    const unsigned char *input;  // Input data
    size_t input_length;         // Input data length
    unsigned char *output;       // Output buffer
    size_t output_size;          // Output buffer size
    size_t output_length;        // Actual output length after encoding/decoding
    WM_BASE64_OPERATION mode;    // Encode or Decode
} WM_BASE64_T;

#endif