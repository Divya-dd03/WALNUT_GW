#ifndef __DRV_CAMERA_H__
#define __DRV_CAMERA_H__

#include <stdint.h>
// #include "camera_bf30a2.h"

typedef struct
{
    uint8_t regAddr;                            ///< Sensor I2C register address
    uint8_t regVal;                             ///< Sensor I2C register value
}camI2cCfg_t;

typedef enum {
  CAM_DATA_EVENT_LINE_COMPLETED = 0x00,
  CAM_DATA_EVENT_FRAME_START,
  CAM_DATA_EVENT_FRAME_COMPLETED,
  CAM_DATA_EVENT_FRAME_ABORT
}camDataEvent_e;

typedef int (*camRecvDataNotify)(camDataEvent_e event, const uint8_t *pdata, uint32_t data_len, void *param);

typedef struct camSensorCfg {
	char * const name;
  uint16_t sensor_id_reg;
  uint16_t sensor_id;
	struct crane_sensor_spec_ops *ops;
	int cam_i2c_idx;
  uint16_t cam_i2c_addr;
  camI2cCfg_t *i2c_cfg;
  int camI2cCfgCnt;
  camI2cCfg_t *i2c_poweroff;
  int camI2cPowerOffCnt;
  int sensor_width;
  int sensor_height;
}camSensorCfg_t;

typedef struct {
  char * init_table;
}camConfig_t;

typedef void (*camCbEvent_fn) (uint32_t event); ///< Camera callback event.
typedef void (*camIrq_fn)(void); 					///< Camera irq

int drvCamOpen(camConfig_t *cfg);
void drvCamClose(void);

void drvCamRecvStart(camRecvDataNotify fun, void *param);
int camSensorWidth(void);
int camSensorHeight(void);
/** \} */

#endif
