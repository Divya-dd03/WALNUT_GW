/**
  ******************************************************************************
  * @file    sc_gpio.h
  * @brief   SC GPIO enum and defs.
  * @author  Walnut Medical
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */
#ifndef __SC_GPIO_H__
#define __SC_GPIO_H__

#include "sc_enum.h"
#include "sc_def.h"
#include "zx_api.h"

  // Type mappings
#define SC_GPIOReturnCode       GPIOReturnCode
#define SC_GPIOPinDirection     GPIOPinDirection
#define SC_GPIOPullUpDown       GPIOPullUpDown
#define SC_GPIOTransitionType   GPIOTransitionType
#define SC_GPIOConfiguration    GPIOConfiguration

// Enum value mappings
#define SC_GPIORC_FALSE					GPIORC_FALSE
#define SC_GPIORC_TRUE					GPIORC_TRUE
#define SC_GPIORC_LOW					GPIORC_LOW
#define SC_GPIORC_HIGH					GPIORC_HIGH
#define SC_GPIORC_OK					GPIORC_OK
#define SC_GPIORC_INVALID_PORT_HANDLE  GPIORC_INVALID_PORT_HANDLE
#define SC_GPIORC_NOT_OUTPUT_PORT      GPIORC_NOT_OUTPUT_PORT
#define SC_GPIORC_NO_TIMER             GPIORC_NO_TIMER
#define SC_GPIORC_NO_FREE_HANDLE       GPIORC_NO_FREE_HANDLE
#define SC_GPIORC_AMOUNT_OUT_OF_RANGE  GPIORC_AMOUNT_OUT_OF_RANGE
#define SC_GPIORC_INCORRECT_PORT_SIZE  GPIORC_INCORRECT_PORT_SIZE
#define SC_GPIORC_PORT_NOT_ON_ONE_REG  GPIORC_PORT_NOT_ON_ONE_REG
#define SC_GPIORC_INVALID_PIN_NUM      GPIORC_INVALID_PIN_NUM
#define SC_GPIORC_PIN_USED_IN_PORT     GPIORC_PIN_USED_IN_PORT
#define SC_GPIORC_PIN_NOT_FREE         GPIORC_PIN_NOT_FREE
#define SC_GPIORC_PIN_NOT_LOCKED       GPIORC_PIN_NOT_LOCKED
#define SC_GPIORC_NULL_POINTER         GPIORC_NULL_POINTER
#define SC_GPIORC_PULLED_AND_OUTPUT    GPIORC_PULLED_AND_OUTPUT
#define SC_GPIORC_INCORRECT_PORT_TYPE  GPIORC_INCORRECT_PORT_TYPE
#define SC_GPIORC_INCORRECT_TRANSITION_TYPE GPIORC_INCORRECT_TRANSITION_TYPE
#define SC_GPIORC_INCORRECT_DEBOUNCE   GPIORC_INCORRECT_DEBOUNCE
#define SC_GPIORC_INCORRECT_DIRECTION  GPIORC_INCORRECT_DIRECTION
#define SC_GPIORC_INCORRECT_PULL       GPIORC_INCORRECT_PULL
#define SC_GPIORC_INCORRECT_INIT_VALUE GPIORC_INCORRECT_INIT_VALUE
#define SC_GPIORC_WRITE_TO_INPUT       GPIORC_WRITE_TO_INPUT
// New API-specific error codes (not in old API) - map to a generic error
#define SC_GPIORC_INVALID_MULTI_FUNCTION  GPIORC_INVALID_PORT_HANDLE
#define SC_GPIORC_INVALID_PARAMETER       GPIORC_INVALID_PORT_HANDLE

// Pin direction mappings
#define SC_GPIO_IN_PIN          GPIO_IN_PIN
#define SC_GPIO_OUT_PIN         GPIO_OUT_PIN

// Pull-up/down mappings
#define SC_GPIO_PULL_DISABLE    GPIO_PULL_DISABLE
#define SC_GPIO_PULLUP_ENABLE   GPIO_PULLUP_ENABLE
#define SC_GPIO_PULLDN_ENABLE   GPIO_PULLDN_ENABLE

// Transition type mappings
#define SC_GPIO_NO_EDGE         GPIO_NO_EDGE
#define SC_GPIO_RISE_EDGE       GPIO_RISE_EDGE
#define SC_GPIO_FALL_EDGE       GPIO_FALL_EDGE
#define SC_GPIO_TWO_EDGE        GPIO_TWO_EDGE

// Function mappings
#define sAPI_GpioGetDirection(gpio)				GpioGetDirection(gpio)
#define sAPI_GpioSetDirection(gpio,direction)	GpioSetDirection(gpio,direction)
#define sAPI_GpioConfig(gpio,GpioConfig)		GpioInitConfiguration(gpio,GpioConfig)
#define sAPI_GpioGetValue(gpio)					GpioGetLevel(gpio)
#define sAPI_GpioSetValue(gpio,value)			GpioSetLevel(gpio,value)


// Inline function to map the interrupt configuration function
static inline SC_GPIOReturnCode sAPI_GpioConfigInterrupt(unsigned int gpio, SC_GPIOTransitionType type, GPIOCallback handler)
{
    // Bind the interrupt callback
    GPIOReturnCode ret = GpioBindInterruptCallback(gpio, handler);
    if (ret != GPIORC_OK)
        return ret;
    // Enable edge detection with the specified type
    return GpioEnableEdgeDetection(gpio, type);
}

#endif