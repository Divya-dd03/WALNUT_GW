/**
 * @file uart_manager.h
 * @brief UART manager for weware platform - handles data sending and receiving over the app UART (SDK_UART_PORT_1)
 *
 * Walnut port of the reference firmware's module/uart/uart_manager.h. The
 * public API is unchanged; only the SDK-facing include and the port/param
 * constants differ (see uart_manager.c for the full API mapping):
 *   CG "functionality/sdk_functionality_uart.h"  ->  walnut "sdk_uart.h"
 *   SDK_UART_PORT_MAIN / SDK_UART_PORT_LOG       ->  SDK_UART_PORT_1 (the
 *                                                    adapter's single logical
 *                                                    port; see below)
 *   SDK_UART_BAUD_115200 / SDK_UART_WORD_LEN_8 / SDK_UART_ONE_STOP_BIT /
 *   SDK_UART_NO_PARITY_BITS                      ->  plain numeric values
 *                                                    (walnut defines no such
 *                                                    enums; see sdk_types.h
 *                                                    SdkUartConfig)
 *
 * This module provides a simple interface for UART communication using the app port.
 * It handles initialization, data sending, and routes received responses to destination modules.
 *
 * Request/Response Pattern:
 * - Modules send requests to UART queue using ModuleMessage (source, destination, address, message)
 * - UART task processes requests from queue and sends the raw binary STM frame over UART
 * - On receive, UART parses the response frame and routes to destination module's queue
 */

#ifndef WEWARE_UART_MANAGER_H
#define WEWARE_UART_MANAGER_H

#include "common/types.h"
#include "module/module_manager.h"
#include "sdk_platform.h"
#include "sdk_uart.h"   /* reference: functionality/sdk_functionality_uart.h */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** UART configuration (port and params for walnut sdk_uart_set_config()).
 *  @note @c port must be SDK_UART_PORT_1 - the only id sdk_uart.h defines and
 *        the only one its sdk_uart_set_config() accepts. Be aware that id maps
 *        to drvUart port 0, the CP debug console, which the driver refuses to
 *        open; see the blocker note at the top of uart_manager.c. */
typedef struct {
    UINT32 port;       /**< SDK_UART_PORT_1 (the only id sdk_uart.h defines) */
    UINT32 baud_rate;  /**< e.g. 115200 */
    UINT8 data_bits;   /**< 5..8 */
    UINT8 stop_bits;   /**< 1 or 2 */
    UINT8 parity;      /**< 0=none, 1=odd, 2=even */
} UartConfig;

/* UartRequest is now replaced by ModuleMessage from module_manager.h */

/*---------------------------------------------------------------
 * UART Manager Statistics
 *--------------------------------------------------------------*/
/**
 * @brief UART manager statistics structure
 */
typedef struct {
    /* Task Statistics */
    UINT32 task_stack_size;        /**< Total task stack size (bytes) */
    UINT32 task_stack_used;        /**< Current task stack usage (bytes) */
    UINT32 task_stack_peak;        /**< Peak task stack usage (bytes) */
    UINT32 task_stack_free;        /**< Free task stack space (bytes) */
    UINT32 memory_allocated;       /**< Total memory allocated by module (bytes) */
} uart_manager_stats_t;

/**
 * @brief Initialize UART manager
 * @return RESULT_SUCCESS on success, RESULT_ERROR on failure
 */
Result uart_manager_init(void);

/**
 * @brief Deinitialize UART manager
 * @return Result status
 */
Result uart_manager_deinit(void);

/**
 * @brief Send command request to UART queue
 * @param request Request structure with source, address, cmd
 * @return Result status
 * @note The request will be queued and processed by UART task state machine
 */
Result uart_manager_send_request(const ModuleMessage *request);

/**
 * @brief Start a one-shot STM responsiveness test (PING-STM command).
 *
 * Probes the STM with a binary get-device-info frame, waits up to 5 s, retries up to
 * 3 times. On the first response it sends "STM: OK" to @p requester; if all attempts
 * time out it sends "STM: FAIL (no response)". Non-blocking: driven by the UART task.
 *
 * @param requester Module to route the verdict back to.
 * @param addr      Reply address (e.g. SMS number); may be NULL/empty.
 * @return RESULT_SUCCESS if armed; RESULT_BUSY if a probe is already running.
 */
Result uart_manager_start_ping_test(ModuleId requester, const char *addr);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_UART_MANAGER_H */
