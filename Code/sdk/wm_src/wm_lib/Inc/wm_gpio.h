/**
******************************************************************************
* @file    wm_gpio.h
* @author  Walnut Medical
* @brief   External fucntion calls of GPIO.
******************************************************************************
* @attention
*
* Copyright (c) 2022 Walnut Medical.
* All rights reserved.
*
******************************************************************************
*/

#ifndef __WM_GPIO_H__
#define __WM_GPIO_H__

/*******************************************************************************
** Header Files
******************************************************************************/
#include "sc_gpio.h"
#include "sc_os.h"
#include "zx_api.h"
#include "wm_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** Defines
******************************************************************************/
#define WM_GPIO_HIGH 1
#define WM_GPIO_LOW 0
#define MAX_GPIO_CONFIG 50
#define WM_PWM_SUPPORT_OFF -1

/*******************************************************************************
 ** External Functions
 ******************************************************************************/

/*******************************************************************************
** Type Definitions
******************************************************************************/
/* Key Board ID */
extern UINT8 wm_board_ID;
extern UINT8 wm_battery_ID;
extern UINT8 wm_sleep_mode;
extern volatile int gi_key_out_timeout;

/* Key GPIO */
extern unsigned int KEY1;
extern unsigned int KEY2;
extern unsigned int KEY3;
extern unsigned int KEY4;
extern unsigned int KEY5;

/* LED GPIO */
extern unsigned int LED_1_GPIO_NUM_RED;
extern unsigned int LED_2_GPIO_NUM_GREEN;
extern unsigned int LED_3_GPIO_NUM_BLUE;
extern unsigned int AUX_LED_GPIO_NUM;
extern int RGB_RED_PWM;
extern int RGB_GREEN_PWM;
extern int RGB_BLUE_PWM;

/* Amplifier GPIO */
extern unsigned int KeySpkENABLE;

/* LCD GPIO */
extern unsigned int LCD_BL;
extern unsigned int LCD_WR_CLK;
extern unsigned int LCD_DAT_SEL;
extern unsigned int LCD_CHIP_SEL;

/* Hardware Support Flags */
extern BOOL KEY1_SUPPORT;
extern BOOL KEY2_SUPPORT;
extern BOOL KEY3_SUPPORT;
extern BOOL KEY4_SUPPORT;
extern BOOL KEY5_SUPPORT;
extern BOOL RGB_LED_SUPPORT;
extern BOOL AUX_LED_SUPPORT;
extern BOOL LCD_SUPPORT;
extern BOOL AMP_CONTROL_SUPPORT;

/* SIM GPIO */
extern unsigned int WM_SIM_DET_GPIO;
extern unsigned int WM_SIM1_DET_GPIO;
extern unsigned int WM_SIM2_DET_GPIO;

/*******************************************************************************
 ** Functions
 ******************************************************************************/
 /* GPIO Functions */
void wm_gpio_init(void);
void wm_configure_key_GPIO(uint32_t key, void (*int_handler)(void));
void wm_configure_ctrl_GPIO(uint32_t pin, uint8_t initial_value);

/* Amplifier GPIO Control */
void wm_audio_aplifier_enabled(void);
void wm_audio_aplifier_disabled(void);

/* Custom LCD GPIO Control */
void wm_custom_LCD_WR_HI(void);
void wm_custom_LCD_WR_LO(void);
void wm_custom_LCD_CS_HI(void);
void wm_custom_LCD_CS_LO(void);
void wm_custom_LCD_DATA_HI(void);
void wm_custom_LCD_DATA_LO(void);

/* WM PWM Control */
void wm_pwm_ch_init(void);
int wm_pwm_ch_1_set(unsigned int period_ns, unsigned int duty_ns);
int wm_pwm_ch_2_set(unsigned int period_ns, unsigned int duty_ns);

/* WM I2C Control */
void wm_i2c_init(void);

/* WM DQR LCD Control */
void wm_init_DQR_LCD(void);
void wm_DQR_LCD_RST_HI(void);
void wm_DQR_LCD_RST_LO(void);
void wm_DQR_LCD_CS_HI(void);
void wm_DQR_LCD_CS_LO(void);
void wm_DQR_LCD_DCX_HI(void);
void wm_DQR_LCD_DCX_LO(void);

/* WIFI Control */
void wm_WIFI_SLEEP(BOOL enable);
void wm_WIFI_EN(BOOL enable);

/*LDO Control */
void wm_LDO_3V3_CTRL(BOOL enable);

/* GPS reset/enable control (drives BK16 RST_N inverted via GPS_EN pin) */
void wm_GPS_EN(BOOL enable);

#ifdef __cplusplus
}
#endif
#endif/*__WM_GPIO_H__*/