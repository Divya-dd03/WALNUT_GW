/**
******************************************************************************
* @file    wm_uart.h
* @author  Walnut Medical
* @brief   External fucntion calls of GPIO.
******************************************************************************
* @attention
*
* Copyright (c) 2022 Walnut Medical.
* All rights reserved.
*
******************************************************************************
*/

#ifndef __WM_UART_H__
#define __WM_UART_H__

/*******************************************************************************
** Header Files
******************************************************************************/
#include "sc_os.h"
#include "zx_api.h"
#include "wm_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** Defines
******************************************************************************/
#include "wm_gpio.h"
#include "drv_uart.h"

typedef void(*wm_uart_callback_t)(uint8_t* data, uint16_t len);

/*******************************************************************************
 ** External Functions
 ******************************************************************************/
/* Port and TX/RX pads are board data (GPS_UART_* / BLE_UART_* in wm_gpio.c). */

/* GPS channel (port = GPS_UART_PORT) */
int wm_gps_uart_init(void);
int wm_gps_uart_deinit(void);
int wm_gps_uart_send(const uint8_t* data, uint16_t len);
/* Blocking, uncached: 0 only when every byte reached the wire. Use instead of
 * wm_gps_uart_send() for payloads larger than the tx ring, which that call
 * truncates while still returning success. */
int wm_gps_uart_send_all(const uint8_t* data, uint32_t len, uint32_t timeout_ms);
/* FALSE while the port is closed - every send fails until it is opened. */
BOOL wm_gps_uart_is_open(void);
/* Discard whatever is already buffered on the receive side. */
int wm_gps_uart_flush_rx(void);
int wm_gps_uart_recv_cb_attach(wm_uart_callback_t cb);
int wm_gps_uart_set_baudrate(drvUartBaudRates_e baud);
int wm_gps_uart_set_pin_pull(GPIOPullUpDown tx_pull, GPIOPullUpDown rx_pull);

/* BLE channel (port = BLE_UART_PORT). Transport only. */
int wm_ble_uart_init(void);
int wm_ble_uart_deinit(void);
int wm_ble_uart_send(const uint8_t* data, uint16_t len);
int wm_ble_uart_recv_cb_attach(wm_uart_callback_t cb);
int wm_ble_uart_set_baudrate(drvUartBaudRates_e baud);
int wm_ble_uart_set_pin_pull(GPIOPullUpDown tx_pull, GPIOPullUpDown rx_pull);

/*******************************************************************************
** Type Definitions
******************************************************************************/


#ifdef __cplusplus
}
#endif
#endif/*__WM_UART_H__*/
