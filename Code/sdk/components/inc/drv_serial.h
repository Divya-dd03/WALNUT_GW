/* All rights reserved.
 *
 */

#ifndef _DRV_SERIAL_H_
#define _DRV_SERIAL_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "osi_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void drvSerial_t;

/**
 * @brief serial port supported
 */
typedef enum {
    USERIAL_PORT_AT = 0,                    ///<ASR Moderm Device
	USERIAL_PORT_EXT_AT,                    ///<ASR Moderm Device2
	USERIAL_PORT_MAX,                       ///<max port numbers
}drvSerialPort_e;

/**
 * @brief serial event type enum
 */
typedef enum
{
    DRV_SERIAL_EVENT_RX_ARRIVED = (1 << 0),  ///< received new data
    DRV_SERIAL_EVENT_RX_OVERFLOW = (1 << 1), ///< rx overflow
    DRV_SERIAL_EVENT_TX_COMPLETE = (1 << 2), ///< all data had been sent
    DRV_SERIAL_EVENT_BROKEN = (1 << 3),      ///< serial broken
    DRV_SERIAL_EVENT_READY = (1 << 4),       ///< serial ready
    DRV_SERIAL_EVENT_OPEN = (1 << 5),        ///< open by host
    DRV_SERIAL_EVENT_CLOSE = (1 << 6),       ///< close by host
} drvSerialEvent_t;

/**
 * @brief function type seraial event callback
 */
typedef void (*drvSerialEventCB_t)(drvSerialEvent_t event, unsigned long p);


/**
 * @brief serial config info structure
 */
typedef struct
{
	uint32_t tx_buf_size;        ///< tx buffer size
    uint32_t rx_buf_size;        ///< rx buffer size
    drvSerialEvent_t event_mask; ///< event mask
    unsigned long param;         ///< caller context
    drvSerialEventCB_t event_cb; ///< event callback, it won't be called in ISR
}drvSerialCfg_t;


/**
 * @brief open a serial
 *
 * @param port    the serial port
 * @param cfg    the serial config
 * @param
 *      - NULL  fail
 *      - other     success
 */
drvSerial_t* drvSerialOpen(drvSerialPort_e port, drvSerialCfg_t *cfg);

/**
 * @brief close the serial
 *
 * @param serial    the serial
 */
void drvSerialClose(drvSerial_t *serial);

/**
 * @brief write to the serial
 *
 * @param serial    the serial
 * @param data      data address
 * @param size      data buffer size
 * @return
 *      - (-1)       fail
 *      - (positive) the bytes of data actually writen
 */
int drvSerialWrite(drvSerial_t *serial, const void *data, size_t size);

/**
 * @brief write to serial directly (do not cache data to software fifo)
 *
 * @param serial    the serial
 * @param data      the data buffer, max be 4 bytes aligned
 * @param size      data buffer size
 * @return          true if success else false
 */
bool drvSerialWriteDirect(drvSerial_t *serial, const void *data, size_t size);

/**
 * @brief get how many data avail to be read
 *
 * @param serial    the serial
 * @return
 *      - (-1)  fail
 *      - other avail size in bytes
 */
int drvSerialReadAvail(drvSerial_t *serial);


/**
 * @brief read data from the serial
 *
 * @param serial    the serial
 * @param buffer    the buffer to read data
 * @param size      the size of the buffer
 * @return
 *      - (-1)       fail
 *      - (positive) the bytes of data actually read
 */
int drvSerialRead(drvSerial_t *serial, void *buffer, size_t size);
#ifdef __cplusplus
}
#endif
#endif
