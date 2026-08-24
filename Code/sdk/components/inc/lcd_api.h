/* All rights reserved.
 *
 */

#ifndef _LCD_API_H_
#define _LCD_API_H_

#include "typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief set panel name to turn on the LCD display function.
 * 
 * @note lcd supports "ST7789"/"ST7567A"/"UC1604"/"ST7735S"/"GC9306"
 *       direction setting now supports "ST7789"/"GC9306"
 *
 * @param panel_name    lcd panel name 
 * @param dir           lcd direction, the default parameter is 0
 *                      0 normal
 *                      1 Y-invert
 *                      2 X-invert
 *                      3 XY-invert
 *                      4 XY-exchange
 *                      5 XY-exchange Y-invert
 *                      6 XY-exchange X-invert
 *                      7 XY-exchange XY-invert
 * 
 */
void Lcd_OpenByName(char* panel_name, int dir);

/**
 * @brief get the width and height params of panel.
 *
 * @param width         the width size of panel
 * @param height        the height size of panel
 * 
 */
void Lcd_GetDimension(UINT16* width, UINT16* height);

/**
 * @brief send display data to the screen.
 *
 * @param lcdData       buffer of display data
 * @param x             the initial horizontal coordinates 
 * @param y             the initial vertical coordinates
 * @param width         horizontal length of display area
 * @param height        vertical length of display area
 * 
 */
int Lcd_SendFB(void *lcdData, unsigned short x, unsigned short y, unsigned short width, unsigned short height);

/**
 * @brief close the LCD display function.
 */
void Lcd_Close(void);

/**
 * @brief set the LCD backlight brightness.
 *
 * @param level        screen brightness, range: 0-5
 * 
 */
void Lcd_SetBrightness(UINT8 level);

/**
 * @brief write cmd to panel(that's only used to test ST7567A)
 */
void lcd_write_cmd(unsigned int cmd);

/**
 * @brief wake up the panel
 */
void Lcd_Wakeup(void);

/**
 * @brief sleep the panel 
 */
void Lcd_Sleep(void);

#ifdef __cplusplus
}
#endif
#endif
