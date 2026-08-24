#ifndef  __DRV_PANEL_COMMON_H__
#define  __DRV_PANEL_COMMON_H__

#include <stdlib.h>
#include <string.h>

#include "drv_lcd_panel.h"
#include "osi_api.h"

typedef struct {
    void *in_buf;
    bool in_buf_alloc;
    uint8_t in_fmt;
    uint8_t threshold_h;
    uint8_t threshold_m;
    uint8_t threshold_l;
    uint16_t in_width;
    uint16_t in_height;
    drvLcdArea_t out;
    uint16_t width_shrink;
    uint16_t height_shrink;
    uint16_t width_shift;
    uint16_t height_shift;
}drvLcdDisplayConvert_t;

static uint8_t drvLcdGetYFromRGB(uint16_t rgb)
{
    return (uint8_t)((float)0.30 * (float)DRV_LCD_RGB565_R(rgb)+ (float)0.59 * (float)DRV_LCD_RGB565_G(rgb)+ (float)0.11 * (float)DRV_LCD_RGB565_B(rgb));
}

uint8_t drvLcdCalOtsu(const uint16_t *d, uint8_t fmt, uint32_t w, uint32_t h);
void drvLcdBlitDataPrepare2(drvLcd_t *d, drvLcdDirection_t dir, const drvLcdLayers_t *cfg, drvLcdDisplayConvert_t *convert, bool equal_scale);
void drvLcdBlitDataFree2(drvLcdDisplayConvert_t *convert);

int32_t drvLcdPicRotationRight90(uint8_t *src, int32_t srcW, int32_t srcH, int32_t channel);
int32_t drvLcdPicRotationLeft90(uint8_t *src, int32_t srcW, int32_t srcH, int32_t channel);
int32_t drvLcdPicRotation180(uint8_t *src, int32_t srcW, int32_t srcH, int32_t channel);
void drvLcdWriteInitTable(drvLcd_t *d, drvLcdConfig_t *cfg);
#endif //__DRV_PANEL_COMMON_H__
