/* All rights reserved.
 *
 */

#ifndef _DRV_KEYPAD_H_
#define _DRV_KEYPAD_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "osi_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief powerkey type
 */
typedef enum {
    PWRKEY_ON,                      ///<power type of KEY_ON
    PWRKEY_EXTON1,                  ///<power type of EXTON1_ON
}PowerKeyType_e;

/**
 * @brief powerkey status
 */
typedef enum
{
    PWRKEY_STATE_PRESS ,            ///<status pressing
    PWRKEY_STATE_RELEASE,           ///<status released
    PWRKEY_STATE_ERROR,             ///<status error
} pwrState_t;

/**
 * @brief keypad type
 */
typedef enum{
    DRV_KEY_TYPE_MATRIX,                ///<default type of matrix
    DRV_KEY_TYPE_DIRECT,
    DRV_KEY_TYPE_ROTARY_0,
    DRV_KEY_TYPE_ROTARY_1
}drvKeyPadType_e;

/**
 * @brief keypad status
 */
typedef enum
{
    DRV_KEYPAD_STATE_PRESS ,            ///<status pressing
    DRV_KEYPAD_STATE_RELEASE,           ///<status released
    DRV_KEYPAD_STATE_ERROR,             ///<status error
} drvKeypadState_e;


/**
 * @brief function type, key event handler
 */
typedef void (*keypadEventCb_t)(uint8_t row, uint8_t col, drvKeypadState_e evt, void *p);

/**
 * @brief function type, keypad wakeup handler
 */
typedef void (*keypadWakeupCb_t)(void *p);

/**
 * @brief Keypad Config infomation structure
 */
typedef struct
{
    drvKeyPadType_e key_type;
    uint32_t debunce_time;  /*in msecond for both direct/matrix key*/
    uint8_t kp_in_mask;
    uint8_t kp_out_mask;
    keypadEventCb_t event_cb;
    keypadWakeupCb_t wakeup_cb;
    void *ctx;
} drvKeypadConfig_t;

/**
 * @brief keypad wakeup type
 */
typedef enum{
    KEYPAD_SLEEP_TO_WAKEUP,
    KEYPAD_WAKEUP_TO_SLEEP
}drvKeypadSleepWakeupType_e;

/**
 * @brief function type, power key event handler
 */

typedef void (*powerkeyCb_t)(void);

/**
 * @brief function type, keypad wakeup gpio config handler
 */
typedef void (*KeypadWakeupCallback)(drvKeypadSleepWakeupType_e type);

/**
 * @brief set powerkey event handler
 *
 * @param pwrKeyType        type of power mode, KEY_EXTON1 or KEY_ON
 * @param cb                the handler
 */

void drvPwrKeySetCB(PowerKeyType_e pwrKeyType, powerkeyCb_t cb);

/**
 * @brief get powerkey status
 *
 * @param pwrKeyType        type of power mode, KEY_EXTON1 or KEY_ON
 * 
 * @param return
 *              - state     powerkey status, elements in pwrState_t
 *              - 2         fail
 */
pwrState_t drvPwrKeyGetStatus(PowerKeyType_e pwrKeyType);

/**
 * @brief config powerkey with parameters
 *
 * @param config        config of power mode, elements in drvKeypadConfig_t
 * 
 */
void drvKeypadConfig(drvKeypadConfig_t *config);

/**
 * @brief keypad wakeup callback function gpio config
 *
 * @param config        config of keypad wakeup
 * 
 */
void KeypadWakeupSetCallback(KeypadWakeupCallback callback);

#ifdef __cplusplus
}
#endif
#endif
