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

/**
 * @brief modes of I2C interface function
 */
typedef enum
{
    STANDARD_MODE = 0,	///<100Kbps
    FAST_MODE = 1,		///<400Kbps
    HS_MODE = 2,		///<3.4 Mbps slave/3.3 Mbps master,standard mode when not doing a high speed transfer
    HS_MODE_FAST = 3,	///<3.4 Mbps slave/3.3 Mbps master,fast mode when not doing a high speed transfer
} I2C_FAST_MODE;

/**
 * @brief I2C interface initialization
 *
 * @param i2c_no    		    I2C port, range 0-3
 * @param fastMode           	I2C clk mode, supports elements in I2C_FAST_MODE
 * 
 */
void i2cc_init(uint8_t i2c_no, I2C_FAST_MODE fastMode);

/**
 * @brief I2C write data by byte
 *
 * @param i2c_no    		    I2C port, range 0-3
 * @param slaveaddress          slaveaddress
 * @param addr           	    1 byte address
 * @param data           	    1 byte data
 * 
 * @return
 *      -  0  success
 *      - -1  fail
 * 
 */
int i2cc_write_byte(uint8_t i2c_no, uint8_t slaveaddress, uint8_t addr, uint8_t data);

/**
 * @brief I2C read data by byte
 *
 * @param i2c_no    		    I2C port, range 0-3
 * @param slaveaddress          slaveaddress
 * @param addr           	    1 byte address
 * 
 * @return
 *      -  0  success
 *      - -1  fail
 * 
 */
int i2cc_read_byte(uint8_t i2c_no, uint8_t slaveaddress, uint8_t addr);

/**
 * @brief I2C write data by short
 *
 * @param i2c_no    		    I2C port, range 0-3
 * @param slaveaddress          slaveaddress
 * @param addr           	    2 byte address
 * @param data           	    2 byte data
 * 
 * @return
 *      -  0  success
 *      - -1  fail
 * 
 */
int i2cc_write_short(uint8_t i2c_no, uint8_t slaveaddress, uint16_t addr, uint16_t data);

/**
 * @brief I2C read data by short
 *
 * @param i2c_no    		    I2C port, range 0-3
 * @param slaveaddress          slaveaddress
 * @param addr           	    2 byte address
 * 
 * @return
 *      -  0  success
 *      - -1  fail
 * 
 */
int i2cc_read_short(uint8_t i2c_no, uint8_t slaveaddress, uint16_t addr);

/**
 * @brief I2C write data by word
 *
 * @param i2c_no    		    I2C port, range 0-3
 * @param slaveaddress          slaveaddress
 * @param addr           	    4 byte address
 * @param data           	    4 byte data
 * 
 * @return
 *      -  0  success
 *      - -1  fail
 * 
 */
int i2cc_write_word(uint8_t i2c_no, uint8_t slaveaddress, uint16_t addr, uint32_t data);

/**
 * @brief I2C read data by word
 *
 * @param i2c_no    		    I2C port, range 0-3
 * @param slaveaddress          slaveaddress
 * @param addr           	    4 byte address
 * 
 * @return
 *      -  0  success
 *      - -1  fail
 * 
 */
int i2cc_read_word(uint8_t i2c_no, uint8_t slaveaddress, uint16_t addr);

#ifdef __cplusplus
}
#endif
#endif
