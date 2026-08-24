/* All rights reserved.
 *
 */

#ifndef _DRV_ADC_H_
#define _DRV_ADC_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "osi_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief read vbat status of pmic
 * 
 * @return
 *      - calVbat
 *      - 0             default param
 */
uint32_t drvVbatRead(void);

/**
 * @brief read rtpadc status
 * 
 * @return
 *      - adc value
 *      - 0             default param
 */
uint32_t drvRtpAdcRead(void);

/**
 * @brief only for ZX600RG to read adc value
 * 
 * @param opt adc config option
 * @return  
 *      - adc value
 *      -0      default param
 */
uint32_t drvAdcRead(void *opt);

#ifdef __cplusplus
}
#endif
#endif
