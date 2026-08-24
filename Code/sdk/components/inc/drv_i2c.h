/* All rights reserved.
 *
 */

#ifndef _I2CC_API_H_
#define _I2CC_API_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DRV_I2C_PORT_0 = 0,    ///< separated I2C for PMIC, APP can not use
    DRV_I2C_PORT_1,        ///< I2C1
    DRV_I2C_PORT_2,        ///< I2C2
    DRV_I2C_PORT_3,        ///< I2C3
    DRV_I2C_PORT_4,        ///< I2C4, used for CAMERA 
    DRV_I2C_PORT_MAX,      ///< Max number of ports supported
}drvI2cPort_e;

/**
 * @brief modes of I2C interface function
 */
typedef enum
{
    DRV_I2C_STANDARD_MODE       = 0,	///<100Kbps
    DRV_I2C_FAST_MODE           = 1,	///<400Kbps
    DRV_I2C_HS_MODE             = 2,	///<3.4 Mbps slave/3.3 Mbps master,standard mode when not doing a high speed transfer
    DRV_I2C_HS_MODE_FAST        = 3,	///<3.4 Mbps slave/3.3 Mbps master,fast mode when not doing a high speed transfer
} drvI2cMode_e;

/**
 * @brief I2C interface initialization
 *
 * @param i2c_port    		    I2C port, range 1-4
 * @param fastMode           	I2C clk mode, supports elements in drvI2cMode_e
 * 
 */
void I2C_init(drvI2cPort_e i2c_port, drvI2cMode_e speed_mode);

/**
 * @brief I2C write data by byte
 *
 * @param i2c_no    		    I2C port, range 1-4
 * @param slaveaddress          slaveaddress
 * @param addr           	    1 byte address
 * @param data           	    1 byte data
 * 
 * @return
 *      -  0  success
 *      - -1  fail
 * 
 */
int I2C_write_byte(drvI2cPort_e i2c_port, uint8_t slaveaddress, uint8_t addr, uint8_t data);

/**
 * @brief I2C read data by byte
 *
 * @param i2c_no    		    I2C port, range 1-4
 * @param slaveaddress          slaveaddress
 * @param addr           	    1 byte address
 * 
 * @return
 *      -  0  success
 *      - -1  fail
 * 
 */
int I2C_read_byte(drvI2cPort_e i2c_port, uint8_t slaveaddress, uint8_t addr);

/**
 * @brief I2C write data by short
 *
 * @param i2c_no    		    I2C port, range 1-4
 * @param slaveaddress          slaveaddress
 * @param addr           	    2 byte address
 * @param data           	    2 byte data
 * 
 * @return
 *      -  0  success
 *      - -1  fail
 * 
 */
int I2C_write_short(drvI2cPort_e i2c_porto, uint8_t slaveaddress, uint16_t addr, uint16_t data);

/**
 * @brief I2C read data by short
 *
 * @param i2c_no    		    I2C port, range 1-4
 * @param slaveaddress          slaveaddress
 * @param addr           	    2 byte address
 * 
 * @return
 *      -  0  success
 *      - -1  fail
 * 
 */
int I2C_read_short(drvI2cPort_e i2c_port, uint8_t slaveaddress, uint16_t addr);

/**
 * @brief I2C write data by word
 *
 * @param i2c_no    		    I2C port, range 1-4
 * @param slaveaddress          slaveaddress
 * @param addr           	    4 byte address
 * @param data           	    4 byte data
 * 
 * @return
 *      -  0  success
 *      - -1  fail
 * 
 */
int I2C_write_word(drvI2cPort_e i2c_port, uint8_t slaveaddress, uint16_t addr, uint32_t data);

/**
 * @brief I2C read data by word
 *
 * @param i2c_no    		    I2C port, range 1-4
 * @param slaveaddress          slaveaddress
 * @param addr           	    4 byte address
 * 
 * @return
 *      -  0  success
 *      - -1  fail
 * 
 */
int I2C_read_word(drvI2cPort_e i2c_port, uint8_t slaveaddress, uint16_t addr);

#ifdef __cplusplus
}
#endif
#endif
