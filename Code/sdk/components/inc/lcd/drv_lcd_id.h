#ifndef _DRV_LCD_ID_H_
#define _DRV_LCD_ID_H_

#include "osi_compiler.h"

OSI_EXTERN_C_BEGIN

/**
 * \brief lcd panel id
 */
typedef enum {
    LCD_MOUDLE_ID_GC9305            =    0x009305,
    LCD_MOUDLE_ID_GC9306            =    0x009306,
    LCD_MOUDLE_ID_ST7789_3W         =    0x307789,
    LCD_MOUDLE_ID_ST7735S           =    0x7c89f0,
    LCD_MOUDLE_ID_UC1617            =    0x001617,
    LCD_MOUDLE_ID_UC1604            =    0x001604,
    LCD_MOUDLE_ID_ST7567A           =    0x007567,
    LCD_MOUDLE_ID_UNKNOWN           =    0xffffff,
}drvLcdMoudleID_t;

OSI_EXTERN_C_END

#endif //_DRV_LCD_ID_H_
