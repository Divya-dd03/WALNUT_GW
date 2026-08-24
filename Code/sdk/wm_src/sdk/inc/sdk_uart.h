/**
 ******************************************************************************
 * @file    sdk_uart.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - UART API.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __SDK_UART_H__
#define __SDK_UART_H__

#include "sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * UART port id. A single application UART port is exposed (port 1); any other
 * value returns SDK_RESULT_INVALID_PARAM.
 */
#define SDK_UART_PORT_1   (0u)

/* Control command selectors for sdk_uart_control(). */
#define SDK_UART_CTRL_CLOSE      (0u)  /* close the port, purge tx/rx buffers  */

/**
 * @brief  Configure (open) a UART port (baud, data bits, stop, parity, flow
 *         control). Opening a port that is already open reconfigures it.
 * @param  port    UART port id (SDK_UART_PORT_1).
 * @param  config  configuration structure.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_uart_set_config(UINT32 port, const SdkUartConfig *config);

/**
 * @brief  Read bytes from a UART port.
 * @note   Not supported: received data is delivered asynchronously through the
 *         callback registered with sdk_uart_set_rx_callback(); there is no
 *         polling read path. This entry point always returns
 *         SDK_RESULT_NOT_SUPPORTED.
 * @param  port        UART port id.
 * @param  buffer      [out] receive buffer.
 * @param  size        buffer size.
 * @param  bytes_read  [out] bytes actually read.
 * @return SdkResult - SDK_RESULT_NOT_SUPPORTED.
 */
SdkResult sdk_uart_read(UINT32 port, void *buffer, UINT32 size, UINT32 *bytes_read);

/**
 * @brief  Write bytes to a UART port.
 * @param  port           UART port id.
 * @param  buffer         data to send.
 * @param  size           number of bytes.
 * @param  bytes_written  [out] bytes actually written.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_uart_write(UINT32 port, const void *buffer, UINT32 size, UINT32 *bytes_written);

/**
 * @brief  Perform a UART control operation on an open port.
 * @param  port     UART port id (SDK_UART_PORT_1).
 * @param  command  control command selector (SDK_UART_CTRL_*).
 * @return SdkResult - 0 success; SDK_RESULT_NOT_SUPPORTED for an unknown
 *         command; negative on failure.
 */
SdkResult sdk_uart_control(UINT32 port, UINT32 command);

/**
 * @brief  Register (or clear) a receive callback for a UART port. Once set, the
 *         callback is invoked with each block of received data (pointer +
 *         length) as it arrives - the SDK performs the read for you. This is
 *         the only receive path (see sdk_uart_read()). Registration is
 *         independent of open/close order and persists across a reconfigure.
 * @param  port  UART port id (SDK_UART_PORT_1).
 * @param  cb    callback to invoke on receive, or NULL to disable.
 * @param  arg   user pointer passed back to @p cb.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_uart_set_rx_callback(UINT32 port, SdkUartRxCallback cb, void *arg);

#ifdef __cplusplus
}
#endif

#endif /* __SDK_UART_H__ */
