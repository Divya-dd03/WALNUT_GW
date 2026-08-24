/* All rights reserved.
 *
 */

#ifndef __GPIO_API_H__
#define __GPIO_API_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "osi_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*GPIOCallback)(void);

typedef enum
{
	GPIORC_FALSE = 0,
	GPIORC_TRUE = 1,
	GPIORC_LOW = 0,
	GPIORC_HIGH = 1,

	GPIORC_OK = 0,
    GPIORC_INVALID_PORT_HANDLE = -100,
    GPIORC_NOT_OUTPUT_PORT,
    GPIORC_NO_TIMER,
    GPIORC_NO_FREE_HANDLE,
    GPIORC_AMOUNT_OUT_OF_RANGE,
    GPIORC_INCORRECT_PORT_SIZE,
    GPIORC_PORT_NOT_ON_ONE_REG,
    GPIORC_INVALID_PIN_NUM,
    GPIORC_PIN_USED_IN_PORT,
    GPIORC_PIN_NOT_FREE,
    GPIORC_PIN_NOT_LOCKED,
    GPIORC_NULL_POINTER,
    GPIORC_PULLED_AND_OUTPUT,
	GPIORC_INCORRECT_PORT_TYPE,
	GPIORC_INCORRECT_TRANSITION_TYPE,
	GPIORC_INCORRECT_DEBOUNCE,
	GPIORC_INCORRECT_DIRECTION,
	GPIORC_INCORRECT_PULL,	
	GPIORC_INCORRECT_INIT_VALUE,
	GPIORC_WRITE_TO_INPUT
}GPIOReturnCode;

/**
 * @brief direction of gpio settings
 */
typedef enum
{
    GPIO_IN_PIN = 0,				///<direction input
    GPIO_OUT_PIN = 1				///<direction output
}GPIOPinDirection;

/**
 * @brief pull direction of gpio settings
 */
typedef enum
{
    GPIO_PULL_DISABLE = 0,			///<disable to pull up
    GPIO_PULLUP_ENABLE,				///<enable to pull up
    GPIO_PULLDN_ENABLE				///<enable to pull down
}GPIOPullUpDown;

/**
 * @brief enum of gpio transition type
 */
typedef enum
{
    GPIO_NO_EDGE = 0,				///<do not react
    GPIO_RISE_EDGE,					///<react when reach rise edge
    GPIO_FALL_EDGE,					///<react when reach fall edge
    GPIO_TWO_EDGE,					///<react when reach square wave
}GPIOTransitionType;

/**
 * @brief struct of gpio configuration
 */
typedef struct
{
	GPIOPinDirection 	pinDir;
	UINT32				initLv;
	GPIOPullUpDown 		pinPull;
	GPIOTransitionType 	pinEd;
	GPIOCallback 		isr;
	GPIOCallback 		wu;
} GPIOConfiguration;

/***********************************************************************
* Function: GpioSetLevel                                              *
************************************************************************
* @brief 		Writes a value to a port					               *
*                                                                      *
* @param		portHandle - the port handle                           *
* @param		value - the value to be written						   *
*                                                                      *
* @return 		GPIORC_OK		                                       *
*				GPIORC_INVALID_PORT_HANDLE
*				GPIORC_INCORRECT_INIT_VALUE
*				GPIORC_WRITE_TO_INPUT								   *
*                                                                      *
* @note                                                               *
***********************************************************************/
GPIOReturnCode GpioSetLevel(UINT32 portHandle, UINT32 value);

/***********************************************************************
* Function: GpioGetLevel                                              *
************************************************************************
* @brief 		Get current level of a port					           *
*                                                                      *
* @param		portHandle - the port handle                           *
*										   							   *
*                                                                      *
* @return 		GPIORC_LOW
				GPIORC_HIGH  										   *
*				GPIORC_INVALID_PORT_HANDLE
*				
*			                                            			   *
* @note                                                               *
***********************************************************************/
GPIOReturnCode GpioGetLevel(UINT32 portHandle);

/***********************************************************************
* Function: GpioEnableEdgeDetection                                    *
************************************************************************
* @brief 		Enable edge detection function of a port		           *
*                                                                      *
* @param		portHandle - the port handle                           *
* @param		edge - the edge detection type						   *
*                                                                      *
* @return 		GPIORC_OK
				GPIORC_INVALID_PORT_HANDLE
				GPIORC_INCORRECT_TRANSITION_TYPE					   *
*				GPIORC_PIN_NOT_FREE
*				
*			                                            			   *
* @note                                                               *
***********************************************************************/
GPIOReturnCode GpioEnableEdgeDetection(UINT32 portHandle,GPIOTransitionType edge);

/***********************************************************************
* Function: GpioDisableEdgeDetection                                   *
************************************************************************
* @brief 		Disable edge detection function of a port		       *
*                                                                      *
* @param		portHandle - the port handle                           *
* @param		edge - the edge detection type						   *
*                                                                      *
* @return 		GPIORC_OK
				GPIORC_INVALID_PORT_HANDLE
				GPIORC_INCORRECT_TRANSITION_TYPE					   *
*				
*				
*			                                            			   *
* @note                                                               *
***********************************************************************/
GPIOReturnCode GpioDisableEdgeDetection(UINT32 portHandle, GPIOTransitionType edge);

/***********************************************************************
* Function: GpioIsEdgeDetected                                   	   *
************************************************************************
* @brief 		check the pending bit of edge detection ofr a port      *
*                                                                      *
* @param		portHandle - the port handle                           *
*				                                                       *
* @return 		GPIORC_OK
				GPIORC_INVALID_PORT_HANDLE
																	   *
*			                                            			   *
* @note                                                               *
***********************************************************************/
GPIOReturnCode GpioIsEdgeDetected(UINT32 portHandle);

/***********************************************************************
* Function: GpioClearEdgeDetection                                     *
************************************************************************
* @brief 		clear the pending bit of edge detection ofr a port      *
*                                                                      *
* @param		portHandle - the port handle                           *
*				                                                       *
* @return 		GPIORC_OK
				GPIORC_INVALID_PORT_HANDLE
																	   *
*			                                            			   *
* @note                                                               *
***********************************************************************/
GPIOReturnCode GpioClearEdgeDetection(UINT32 portHandle);

/***********************************************************************
* Function: GpioSetDirection                                           *
************************************************************************
* @brief 	Controls the direction of a given port                  *
*                                                                      *
* @param		portHandle - the port handle                           *
* @param		dir - the required direction						   *
*                                                                      *
* @return 		GPIORC_OK		                                       *
*				GPIORC_INVALID_PORT_HANDLE						       *
*               GPIORC_INCORRECT_DIRECTION
				GPIORC_PIN_USED_IN_PORT							   *
*                                                                      *
* @note                                                               *
* 																	   *
*																	   *
* 																	   *
* If the port is with interrupt, it can not be changed to output with *
* this API																*
*************************************************************************/
GPIOReturnCode GpioSetDirection(UINT32 portHandle, GPIOPinDirection dir);

/***********************************************************************
* Function: GpioGetDirection                                           *
************************************************************************
* @brief 		Get the direction of a given port                  	   *
*                                                                      *
* @param		portHandle - the port handle                           *
*                                                                      *
* @return 		GPIO_IN_PIN
				GPIO_OUT_PIN										   *
*				GPIORC_INVALID_PORT_HANDLE						       *
*
*                                                                      *
* @note                                                               *
* 																	   *
**************************************************************************/
GPIOPinDirection GpioGetDirection(UINT32 portHandle);

/***********************************************************************
* Function: GpioSetPullControl                                           *
************************************************************************
* @brief 		Controls the pull up and pull down state of port        *
*                                                                      *
* @param		portHandle - the port handle                           *
* @param		pull - pull up/pull down/not pull					   *
*                                                                      *
* @return 		GPIORC_OK		                                       *
*				GPIORC_INVALID_PORT_HANDLE						       *
*               GPIORC_INCORRECT_PULL
				                                                       *
* @note                                                               *
* 																	   *
*																	   *
* 																	   *
* If the port is with interrupt, it can not be changed to output with *
* this API																*
*************************************************************************/
GPIOReturnCode GpioSetPullControl(UINT32 portHandle, GPIOPullUpDown pull);

/***********************************************************************
* Function: GpioBindWakeupCallback                          		   *
************************************************************************
* @brief 		Bind a callback function to a GPIO's wakeup event       *
*                                                                      *
* @param		portHandle - the port handle                           *
*                                                                      *
* @return 		GPIORC_OK
				GPIO_NULL_POINTER 									   *
*				GPIORC_INVALID_PORT_HANDLE			                   *
* @note                                                               *
* 																	   *
**************************************************************************/
GPIOReturnCode GpioInitConfiguration(UINT32 portHandle, GPIOConfiguration config);

/***********************************************************************
* Function: GpioBindInterruptCallback                          		   *
************************************************************************
* @brief 		Bind a callback function to a GPIO's interrupt	       *
*                                                                      *
* @param		portHandle - the port handle                           *
*                                                                      *
* @return 		GPIORC_OK
				GPIO_NULL_POINTER 									   *
*				GPIORC_INVALID_PORT_HANDLE			                   *
* @note                                                               *
* 																	   *
**************************************************************************/
GPIOReturnCode GpioBindInterruptCallback(UINT32 portHandle, GPIOCallback func);

GPIOReturnCode GpioPhase1Init(void);
GPIOReturnCode GpioPhase2Init(void);

GPIOReturnCode GpioEnableEdgeDetectionBit(UINT32 portHandle, 	GPIOTransitionType edge);
GPIOReturnCode GpioDisableEdgeDetectionBit(UINT32 portHandle, 	GPIOTransitionType edge);
GPIOReturnCode GpioIsEdgeDetected_wakeup(UINT32 portHandle);
UINT32 GPIO_MFPR_ADDR(UINT32 n);

#ifdef __cplusplus
}
#endif
#endif
