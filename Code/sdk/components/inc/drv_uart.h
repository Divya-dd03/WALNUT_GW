/* All rights reserved.
 *
 */

#ifndef _DRV_UART_H_
#define _DRV_UART_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

struct drvUart;

/**
 * @brief UART struct
 *
 * Only a declare here, caller have no necessary to know
 * how it is implemented. And it is a anchor to identify
 * which uart device is to be handled.
 */
typedef struct drvUart drvUart_t;

/**
 * @brief UART data event
 */
typedef enum
{
    DRV_UART_EVENT_RX_ARRIVED = (1 << 0),  ///< Received new data
    DRV_UART_EVENT_RX_OVERFLOW = (1 << 1), ///< Rx fifo overflowed
    DRV_UART_EVENT_TX_COMPLETE = (1 << 2)  ///< All data had been sent
}drvUartEvent_e;

/**
 * @brief UART data bits
 */
typedef enum
{
    DRV_UART_DATA_BITS_7 = 7,
    DRV_UART_DATA_BITS_8 = 8
} drvUartDataBits_t;

/**
 * @brief UART stop bits
 */
typedef enum
{
    DRV_UART_STOP_BITS_1 = 1,
    DRV_UART_STOP_BITS_2 = 2
} drvUartStopBits_t;

/**
 * @brief UART parity check mode
 */
typedef enum
{
    DRV_UART_NO_PARITY,   ///< No parity check
    DRV_UART_ODD_PARITY,  ///< Parity check is odd
    DRV_UART_EVEN_PARITY, ///< Parity check is even
} drvUartParity_t;

/** 
* @brief All the UART Baud Rate that the UART Package supplay 
*/
typedef enum   
{
    DRV_UART_BAUD_150       = 150,                      ///<baudrate 150 Bps
    DRV_UART_BAUD_300       = 300,                      ///<baudrate 300 Bps
    DRV_UART_BAUD_600       = 600,                      ///<baudrate 600 Bps
    DRV_UART_BAUD_1200      = 1200,                     ///<baudrate 1200 Bps
    DRV_UART_BAUD_2400      = 2400,                     ///<baudrate 2400 Bps
    DRV_UART_BAUD_3600      = 3600,                     ///<baudrate 3600 Bps
    DRV_UART_BAUD_4800      = 4800,                     ///<baudrate 4800 Bps
    DRV_UART_BAUD_7200      = 7200,                     ///<baudrate 7200 Bps
    DRV_UART_BAUD_9600      = 9600,                     ///<baudrate 9600 Bps
    DRV_UART_BAUD_14400     = 14400,                    ///<baudrate 14400 Bps
    DRV_UART_BAUD_19200     = 19200,                    ///<baudrate 19200 Bps
    DRV_UART_BAUD_28800     = 28800,                    ///<baudrate 28800 Bps
    DRV_UART_BAUD_38400     = 38400,                    ///<baudrate 38400 Bps
    DRV_UART_BAUD_57600     = 57600,                    ///<baudrate 57600 Bps
    DRV_UART_BAUD_115200    = 115200,                   ///<baudrate 115200 Bps
    DRV_UART_BAUD_230400    = 230400,                   ///<baudrate 230400 Bps
    DRV_UART_BAUD_460800    = 460800,                   ///<baudrate 460800 Bps
    DRV_UART_BAUD_921600    = 921600,                   ///<baudrate 921600 Bps
    DRV_UART_BAUD_1000000   = 1000000,                  ///<baudrate 1000000 Bps
    DRV_UART_BAUD_1125000   = 1125000,                  ///<baudrate 1125000 Bps
    DRV_UART_BAUD_1800000   = 1800000,                  ///<baudrate 1800000 Bps
    DRV_UART_BAUD_3600000   = 3600000,                  ///<baudrate 3600000 Bps
    DRV_UART_MAX_NUM_BAUD
}drvUartBaudRates_e;

/**
 * @brief function type to notify data event
 */
typedef void (*drvUartEventCB_t)(drvUartEvent_e evt, void *ctx);

/**
 * @brief UART config
 */
typedef struct
{
    drvUartBaudRates_e baud;               ///< baudrate
    drvUartDataBits_t data_bits; ///< data bits
    drvUartStopBits_t stop_bits; ///< stop bits
    drvUartParity_t parity;      ///< parity check mode
    bool auto_baud_lc;           ///< "at" ot "AT" for auto baud. 
    bool flow_control_enable;    ///< enable flow Control
    size_t rx_buf_size;          ///< rx buffer size
    size_t tx_buf_size;          ///< tx buffer size
    uint32_t event_mask;         ///< event mask, identify which event will be notified
    drvUartEventCB_t event_cb;   ///< event notify callback function
    void *event_cb_ctx;          ///< app private param (like a context)
} drvUartCfg_t;


typedef enum {
    DRV_UART_PORT_1 = 0,    ///< debug uart , APP can not use
    DRV_UART_PORT_2,        ///< CP uarT(AT)
    DRV_UART_PORT_3,        ///< BT uart
    DRV_UART_PORT_4,        ///< UART 4
    DRV_UART_PORT_MAX,      ///< Max number of ports supported
}drvUartPort_e;


/**
 * @brief open UART driver
 *
 * Open the UART driver, done it can send/receive data via the UART.
 *
 * @param port  the UART port enum
 * @param cfg        the UART port coinfig
 * @return
 *      - true  success
 *      - false fail
 */
drvUart_t* drvUartOpen(drvUartPort_e port, drvUartCfg_t *cfg);

/**
 * @brief close UART driver
 *
 * @note All data in tx/rx buffer will be purged.
 *
 * Close the UART driver, stop all data transfer.
 * But do not release resource.
 *
 * @param uart  the UART driver
 */
void drvUartClose(drvUart_t *uart);


/**
 * @brief send data via the UART
 *
 * @note data may not send to hardware right away
 *
 * Send data via UART, the data may not send to hardware directly
 * because the UART hw may be busy, and the data not sent to hw will
 * be cached to a software tx buffer(see, drvUartCfg_t->tx_buf_size).
 *
 * Therefore the return value include the data had been sent and the
 * data had been cached.
 *
 * @param uart  the UART driver
 * @param data  data buffer to be sent
 * @param size  data buffer size
 * @return
 *      - (-1)  Parameter error
 *      - OTHERS (>=0)  The number of bytes actually sent (or cached)
 */
int drvUartSend(drvUart_t *uart, const void *data, size_t size);

/**
 * @brief send data via the UART without cache in n milliseconds
 *
 * @note data will not cached to tx fifo
 *
 * @param uart          the UART driver
 * @param data          data buffer to be sent
 * @param size          data buffer size
 * @param timeout_ms    timeout milliseconds
 * @return
 *      - (-1) Parameter error
 *      - OTHERS (>=0)  The number of bytes actually sent
 */
int drvUartSendAll(drvUart_t *uart, const void *data, size_t size, uint32_t timeout_ms);

/**
 * @brief receive data from the UART
 *
 * Receive data from the UART driver. Actually caller got data
 * from a software rx fifo but not hw fifo directly.
 *
 * After drvUartOpen the driver will store data from UART hw rx fifo
 * automatically and trigger a DRV_UART_EVENT_RX_ARRIVED, app who use
 * this driver can call the api after receive the event.
 *
 * @param uart  the UART driver
 * @param buf   buffer to store data
 * @param size  buffer size
 * @return
 *      - (-1) Parameter error
 *      - OTHERS (>=0)  The number of bytes actually receive from UART
 */
int drvUartReceive(drvUart_t *uart, void *buf, size_t size);

/**
 * @brief inquire avalable bytes in rx buffer
 *
 * @param uart  the UART driver
 * @return
 *      - (-1) Parameter error
 *      - OTHERS (>=0) Available size in byte
 */
int drvUartReadAvail(drvUart_t *uart);

/**
 * @brief inquire avalable space in tx buffer
 *
 * @param uart  the UART driver
 * @return
 *      - (-1) Parameter error
 *      - OTHERS (>=0) Available size in byte
 */
int drvUartWriteAvail(drvUart_t *uart);

/**
 * @brief set whether UART will auto sleep
 *
 * When enabled, UART will sleep after all bytes are transfered , with
 * specified timeout.
 *
 * To disable auto sleep feature, set \p timeout to -1 (or other negative
 * values).
 *
 * It maybe different among chips, or even different UARTs of one chip
 * whether system will be waken up when there are UART input.
 *
 * @param uart      the UART driver
 * @param timeout   auto sleep wait time after transfer done. It can be
 *                  0 but not recommended. Negative value to disable
 *                  auto sleep feature.
 */
void drvUartSetAutoSleep(drvUart_t *uart, int timeout);

/**
 * @brief get uart configuration, will never return null
 *
 * @param uart  the UART driver
 * @return
 *      - NULL      fail, only when UART is NULL
 *      - otherwise the UART configuration struct point.
 */
const drvUartCfg_t *drvUartConfig(drvUart_t *uart);

/**
 * @brief reconfig the uart
 *
 * @note caller should make sure the UART had already closed,
 *       or it will return false
 *       if `cfg` is NULL, it will do nothing and return true
 *
 * @param uart  the UART driver
 * @param cfg   the configuration
 * @return
 *      - true  sucess
 *      - false fail
 */
bool drvUartReconfig(drvUart_t *uart, drvUartCfg_t *cfg);

/**
 * @brief wait tx data send finished
 *
 * @param uart      the UART driver
 * @param timeout   wait time in millisecond
 * @return
 *      - true      data all sent
 *      - false     data residual
 */
bool drvUartWaitTxFinish(drvUart_t *uart, uint32_t timeout);

#ifdef __cplusplus
}
#endif

#endif
