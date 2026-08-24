/* All rights reserved.
 *
 */

#ifndef _CAM_API_H_
#define _CAM_API_H_

#include <stdbool.h>
#include "typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 
* @brief screen supporting list 
*/
typedef enum
{
	LCD_NAME_NONE,		///<NONE
	LCD_NAME_ST7789,	///<for ST7789, RGB
	LCD_NAME_ST7567,	///<for ST7567A, FSTN
	LCD_NAME_UC1604,	///<for UC1604, GRAY
	LCD_NAME_GC9306,	///<for GC9306, RGB
	LCD_NAME_ST7735,	///<for ST7735S, RGB
	LCD_TYPE_NUM		///<number of screens having supported
}LcdName_e;

typedef enum
{
	DRV_LCD_DIR_ROTATE_0,		///<DRV_LCD_DIR_NORMAL
	DRV_LCD_DIR_ROTATE_90,		///<DRV_LCD_DIR_EXCHG_XINV
	DRV_LCD_DIR_ROTATE_180,	///<DRV_LCD_DIR_YINV_XINV
	DRV_LCD_DIR_ROTATE_270,		///<DRV_LCD_DIR_EXCHG_YINV
	ROTATE_TYPE_NUM				///<rotating types supported
}Rotate_e;

/**
 * @brief turn on camera preview function and display the image on the screen 
 * 
 * @note 
 * lcd supports elements in LcdName_e
 * camera support: "GC032A/BF30A2"
 * the camera will auto adapter
 * if apply "LCD_OpenByName" before, the preview will follow the direction setting.
 * 
 * @param lcd_name    lcd panel name 
 * 
 */
int Camera_Preveiw(LcdName_e lcd_name);

/**
 * @brief set rotating ancle of camera image
 * 
 * @note 
 * this function must be used before "Camera_Preveiw" is applied
 * 
 * @param rotate    camera rotating ancle 
 * 
 */
void Camera_Rotating(Rotate_e rotate);

/**
 * @brief close the camera and screen 
 */
void Camera_Preveiw_Close(void);

/**
 * @brief open the camera without opening screen 
 */
int Camera_Open(void);

/**
 * @brief close the camera without closing screen 
 */
void Camera_Close(void);

/**
 * @brief Set code scanning function 
 * 
 * @note 
 * lcd supports "ST7789"/"ST7567A"/"UC1604"/"ST7735S"/"GC9306"
 * camera support: "GC032A/BF30A2"
 * the camera will auto adapter
 * 
 * @param enable    	switch of the function, set true or false to open or not
 * @param mode    		scanning mdoe, range 0-65535
 * 							[0]:keep scanning and reporting
 * 							[1~65535]:keep scanning until reporting n times and then stop scanning
 * 
 */
void Camera_ScanCode(bool enable, int mode);
#ifdef __cplusplus
}
#endif
#endif/*_CAM_API_H_*/
