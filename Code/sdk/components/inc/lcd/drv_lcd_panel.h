#ifndef _DRV_LCD_PANEL_H_
#define _DRV_LCD_PANEL_H_

#include "drv_lcd.h"

OSI_EXTERN_C_BEGIN

typedef enum
{
    DRV_LCD_SPI_4WIRE,
    DRV_LCD_SPI_3WIRE,
    DRV_LCD_SPI_4WIRE_START_BYTE,
    DRV_LCD_SPI_3WIRE_2LANE,
} drvLcdSpiLineMode_t;

typedef enum
{
    DRV_LCD_OUT_FMT_8BIT_RGB332 = 0,
    DRV_LCD_OUT_FMT_8BIT_RGB444 = 1,
    DRV_LCD_OUT_FMT_8BIT_RGB565 = 2,
    DRV_LCD_OUT_FMT_16BIT_RGB332 = 4,
    DRV_LCD_OUT_FMT_16BIT_RGB444 = 5,
    DRV_LCD_OUT_FMT_16BIT_RGB565 = 6,
} drvLcdOutputFormat_t;

/**
 * \brief LCD panel operations
 *
 * All functions can't be NULL. User won't check NULL pointer before calling
 * panel operations. Also, caller will take care thread safe.
 */
typedef struct
{
    /**
     * \brief probe the panel
     * \param d         LCD instance
     * \param id        LCD panel id
     * \return
     *      - true if the panel matches the driver
     *      - false if not match
     */
    bool (*probe)(drvLcd_t *d, drvLcdMoudleID_t id);
    /**
     * \brief initialization
     * \param d         LCD instance
     * \param cfg       LCD config
     */
    void (*init)(drvLcd_t *d, drvLcdConfig_t *cfg);
    /**
     * \brief prepare data transfer, set direction and ROI
     * \param d         LCD instance
     * \param dir       direction
     * \param roi       region of interest
     */
    void (*blit_prepare)(drvLcd_t *d, drvLcdDirection_t dir, const drvLcdArea_t *roi);
    /**
     * \brief data transfer
     * \param d         LCD instance
     * \param dir       direction
     * \param cfg       lcd layers config
     */
    void (*blit_data)(drvLcd_t *d, drvLcdDirection_t dir, const drvLcdLayers_t *cfg);
} drvLcdPanelOps_t;

bool drvLcdDummyProbe(drvLcd_t *d, drvLcdMoudleID_t id);
void drvLcdDummyInit(drvLcd_t *d);
void drvLcdDummyBlitPrepare(drvLcd_t *d, drvLcdDirection_t dir, const drvLcdArea_t *roi);

/**
 * \brief panel configurations
 */
typedef struct
{
    /** panel operation */
    drvLcdPanelOps_t ops;
    /** name string */
    const char *name;
    /** device ID */
    int dev_id;
    /** reset time in us */
    int reset_us;
    /** delay time after reset in us */
    int init_delay_us;
    /** width in normal direction */
    uint16_t width;
    /** height in normal direction */
    uint16_t height;
    /** output format */
    drvLcdOutputFormat_t out_fmt;
    /** panel initial direction */
    drvLcdDirection_t dir;
    /** SPI line mode */
    drvLcdSpiLineMode_t line_mode;
    /** fmark enabled */
    bool fmark_enabled;
    /** fmark delay */
    int fmark_delay;
    /** SPI frequency */
    int freq;
    /** frmae period in us */
    int frame_us;
    bool dirswap_support;
} drvLcdPanelDesc_t;

/**
 * \brief set panel description to LCD instance
 *
 * \param d         LCD instance
 * \param desc      panel description
 */
void drvLcdSetDesc(drvLcd_t *d, const drvLcdPanelDesc_t *desc);

/**
 * \brief get panel description from LCD instance
 *
 * \param d         LCD instance
 * \return
 *      - panel description
 */
const drvLcdPanelDesc_t *drvLcdGetDesc(drvLcd_t *d);

/**
 * \brief write command to panel
 *
 * This should be called only in panel driver, caller will ensure it is
 * called inside lock.
 *
 * \param d         LCD instance
 * \param cmd       command
 */
void drvLcdWriteCmd(drvLcd_t *d, uint16_t cmd);

/**
 * \brief write data to panel
 *
 * This should be called only in panel driver, caller will ensure it is
 * called inside lock.
 *
 * \param d         LCD instance
 * \param data      data
 */
void drvLcdWriteData(drvLcd_t *d, uint16_t data);

/**
 * \brief read data from panel by command
 */
void drvLcdReadData(drvLcd_t *d, uint16_t cmd, uint8_t *data, int len);

OSI_EXTERN_C_END
#endif
