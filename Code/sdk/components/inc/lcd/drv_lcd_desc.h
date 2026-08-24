#ifndef _DRV_LCD_DESC_H_
#define _DRV_LCD_DESC_H_

// #include "drv_config.h"
#include "drv_lcd.h"
#include "drv_lcd_panel.h"

//#define CONFIG_SUPPORT_LCD_ST7735S
#define CONFIG_SUPPORT_LCD_ST7789
#define CONFIG_SUPPORT_LCD_ST7567A

extern const drvLcdPanelDesc_t gLcdGc9305Desc;
extern const drvLcdPanelDesc_t gLcdGc9306Desc;
extern const drvLcdPanelDesc_t gLcdSt7735sDesc;
extern const drvLcdPanelDesc_t gLcdSt7789Desc;
extern const drvLcdPanelDesc_t gLcdSt7789W3Desc;
extern const drvLcdPanelDesc_t gLcdSt7789W3FDesc;
extern const drvLcdPanelDesc_t gLcdSt7789W3L2Desc;
extern const drvLcdPanelDesc_t gLcdSt7789W3L2FDesc;
extern const drvLcdPanelDesc_t gLcdUc1617Desc;
extern const drvLcdPanelDesc_t gLcdUc1604Desc;
extern const drvLcdPanelDesc_t gLcdSt7567aDesc;

static const drvLcdPanelDesc_t *gLcdDescs[] = {
#ifdef CONFIG_SUPPORT_LCD_GC9305
    &gLcdGc9305Desc,
#endif
#ifdef CONFIG_SUPPORT_LCD_GC9306
    &gLcdGc9306Desc,
#endif
#ifdef CONFIG_SUPPORT_LCD_ST7735S
    &gLcdSt7735sDesc,
#endif
#ifdef CONFIG_SUPPORT_LCD_ST7789
    &gLcdSt7789Desc,
#endif
#ifdef CONFIG_SUPPORT_LCD_UC1617
    &gLcdUc1617Desc,
#endif
#ifdef CONFIG_SUPPORT_LCD_UC1604
    &gLcdUc1604Desc,
#endif
#ifdef CONFIG_SUPPORT_LCD_ST7567A
    &gLcdSt7567aDesc,
#endif
};

#endif
