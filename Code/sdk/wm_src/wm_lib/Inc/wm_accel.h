/**
******************************************************************************
* @file    wm_accel.h
* @author  Walnut Medical
* @brief   Header file of the I2C accelerometer driver.
******************************************************************************
* @attention
*
* Copyright (c) 2026 Walnut Medical.
* All rights reserved.
*
* Currently backed by an SC7A20HTR (LIS2DH-compatible register map). The API
* and file naming are kept sensor-agnostic so swapping the part only touches
* this .c file, not its callers.
******************************************************************************
*/

#ifndef __WM_ACCEL_H__
#define __WM_ACCEL_H__

/*******************************************************************************
** Header Files
******************************************************************************/
#include "wm_gpio.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** Functions
******************************************************************************/
void wm_accel_init(void);
int wm_accel_read_reg(uint8_t reg);
int wm_accel_write_reg(uint8_t reg, uint8_t val);
BOOL wm_accel_read_xyz(int16_t *x, int16_t *y, int16_t *z);

#ifdef __cplusplus
}
#endif

#endif /*__WM_ACCEL_H__*/
