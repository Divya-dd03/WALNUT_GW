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
* @brief screen supporting list 
*
* @note default parameter is SPI_CLK_26MHZ
*
*/
typedef enum {
	SPI_CLK_812_5KHZ,		///<812.5KHz
	SPI_CLK_1_625MHZ,		///<1.625MHz
	SPI_CLK_3_25MHZ,		///<3.25MHz
	SPI_CLK_6_5MHZ,			///<6.5MHz
	SPI_CLK_13MHZ,			///<13MHz
	SPI_CLK_26MHZ,			///<26MKHz
	SPI_CLK_52MHZ,			///<52MHz
	SPI_CLK_MAX,			///<numbers of CLK modes
}SPI_CLK_T;

/**
 * @brief SPI interface initialization.
 * 
 * @note this interface configs GPIO12/13/14/15 as CLK/CS/RX/TX
 *
 * @param spimode    		set mode, supports elements in SPI_MODE_T
 * @param spiclk           	set clock, supports elements in SPI_CLK_T
 * 
 */
void CIS_SPI_Initialize(SPI_MODE_T spimode, SPI_CLK_T spiclk);

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
UINT32 CIS_SPI_DataTransfer(unsigned char *inbuf, unsigned char *outbuf, unsigned int len);

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
UINT32 CIS_SPI_WriteData(unsigned char *buf, unsigned int len);

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
UINT32 CIS_SPI_ReadData(unsigned char *buf, unsigned int len);




#ifdef __cplusplus
}
#endif
#endif
