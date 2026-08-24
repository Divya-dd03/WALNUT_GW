/* All rights reserved.
 *
 */

#ifndef _SPI_API_H_
#define _SPI_API_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "typedef.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
* @brief CPOL/CPHA modes of SPI function
*
* @note default parameter is SPI_MODE0
*
*/
typedef enum{
	SPI_MODE0,		///<CPOL=0, CPHA=0
	SPI_MODE1,		///<CPOL=0, CPHA=1
	SPI_MODE2,		///<CPOL=1, CPHA=0
	SPI_MODE3,		///<CPOL=1, CPHA=1
	SPI_MODE_MAX,	///<max number of modes
}SPI_MODE_T;

/**
* @brief Port chosen of SPI function
*
* @note default parameter is SPI_PORT_0
*
*/
typedef enum
{
    SPI_PORT_0,			    ///<SSP Port0: GPIO 33/34/35/36
    SPI_PORT_1,			    ///<SSP Port1: GPIO 04/05/06/07
    SPI_PORT_2,			    ///<SSP Port2: GPIO 12/13/14/15
    SPI_PORT_MAX,			///<numbers of SSP Ports
}SPI_PORT_T;

/**
* @brief screen supporting list
*
* @note default parameter is SPI_CLK_26MHZ
*
*/

typedef enum {
	SPI_CLK_812_5KHZ,		///<812.5kHz
	SPI_CLK_1_625MHZ,		///<1.625MHz
	SPI_CLK_3_25MHZ,		///<3.25MHz
	SPI_CLK_6_5MHZ,			///<6.5MHz
	SPI_CLK_13MHZ,			///<13MHz
	SPI_CLK_26MHZ,			///<26MKHz
	SPI_CLK_52MHZ,			///<52MHz
	SPI_CLK_MAX,			///<numbers of CLK modes
}SPI_CLK_T;

typedef enum {
	GCS_DISABLE,
	GCS_ENABLE
}SPI_GCS_T;

/**
 * @brief SPI interface initialization.
 *
 * @note this interface configs GPIO as CLK/CS/RX/TX
 *
 * @param spiport    		set port, supports elements in SPI_CLK_T
 * @param spiclk           	set clock, supports elements in SPI_CLK_T
 *
 */
void spi_init(SPI_PORT_T spiport, SPI_MODE_T spimode, SPI_CLK_T spiclk, SPI_GCS_T gpiocs);

/**
 * @brief SPI data transport.
 *
 * @note this interface supports reading and writing at the same time
 * datas should be saved into the buffer before using this interface
 *
 * @param inbuf    			buffer used to receive datas
 * @param outbuf           	buffer used to send datas
 * @param len           	length for transporting
 *
 * @return
 *      - 0  Success
 *      - OTHERS  Error codes
 */
uint32_t spi_transfer(SPI_PORT_T spiport, unsigned char *inbuf, unsigned char *outbuf, unsigned int len);

/**
 * @brief SPI send data.
 *
 * @note  datas should be saved into the buffer before using this interface
 *
 * @param buf           	buffer used to send datas
 * @param len           	length for transporting
 *
 * @return
 *      - 0  Success
 *      - OTHERS  Error codes
 */
uint32_t spi_write(SPI_PORT_T spiport, unsigned char *buf, unsigned int len);

/**
 * @brief SPI receive data.
 *
 * @note  datas should be saved into the buffer before using this interface
 *
 * @param buf           	buffer used to receive datas
 * @param len           	length for transporting
 *
 * @return
 *      - 0  Success
 *      - OTHERS  Error codes
 */
uint32_t spi_read(SPI_PORT_T spiport, unsigned char *buf, unsigned int len);

#ifdef __cplusplus
}
#endif
#endif
