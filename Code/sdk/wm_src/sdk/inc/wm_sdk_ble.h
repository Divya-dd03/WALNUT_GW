/**
 ******************************************************************************
 * @file    wm_sdk_ble.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - BLE API.
 *
 *          The radio is brought up at boot by wm_sdk_ble_init(); every other call
 *          reports WM_SDK_RESULT_NOT_INITIALIZED until that has happened.
 *
 *          Two roles are available and may be used together: an observer
 *          (wm_sdk_ble_scan_start() reports advertisements to the scan callback)
 *          and a central (wm_sdk_ble_connect() opens one link, after which
 *          wm_sdk_ble_send() writes to the target characteristic and replies
 *          arrive on the data callback).
 *
 *          Payloads are carried as raw bytes. Any framing on top of them
 *          belongs to the application.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __WM_SDK_BLE_H__
#define __WM_SDK_BLE_H__

#include "wm_sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** Addresses
**
** Every address in this API is "AA:BB:CC:DD:EE:FF" - the form a scanner app
** such as nRF Connect displays. Addresses are also accepted without the
** colons, and in either case; anything else is WM_SDK_RESULT_INVALID_PARAM.
******************************************************************************/

/* Default target for wm_sdk_ble_set_target(). 16-bit UUIDs only. */
#define WM_SDK_BLE_DEFAULT_SERVICE  "00FF"
#define WM_SDK_BLE_DEFAULT_CHAR     "FF01"

/*******************************************************************************
** Type Definitions
**
** All four callbacks run on the SDK's BLE service context. Keep them short and
** make no blocking call from inside one - in particular wm_sdk_ble_connect(),
** wm_sdk_ble_disconnect(), wm_sdk_ble_send(), wm_sdk_ble_scan_start() and
** wm_sdk_ble_send_at(). Pointers passed in are valid only for the duration of the
** call; copy out whatever must be kept.
******************************************************************************/
/* One advertisement or scan response passing the scan filter. */
typedef void (*wm_sdk_ble_scan_cb_t)(const wm_SdkBleScanResult *res);

/* Link state change on channel @p cid. After WM_SDK_BLE_EVT_READY no link exists. */
typedef void (*wm_sdk_ble_evt_cb_t)(wm_SdkBleEvent evt, int cid);

/* Bytes received from the connected peer. One application message may arrive
 * split across several calls, so a framed protocol needs its own reassembly. */
typedef void (*wm_sdk_ble_data_cb_t)(const UINT8 *data, UINT16 len);

/* Diagnostics only: each raw line received from the radio, unparsed.
 * 'line' is NUL-terminated; 'len' excludes the terminator. */
typedef void (*wm_sdk_ble_line_cb_t)(const char *line, UINT16 len);

/*******************************************************************************
** Lifecycle and power
******************************************************************************/
/**
 * @brief  Bring up the BLE subsystem. Called once from wm_system_init() at boot
 *         (gated by WM_BLE_SUPPORT), so the application normally never calls
 *         it. Idempotent.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_NOT_SUPPORTED when the board has no
 *                     BLE radio; WM_SDK_RESULT_ERROR when the board has not been
 *                     validated.
 */
wm_SdkResult wm_sdk_ble_init(void);

/**
 * @brief  Get BLE power state as tracked by the SDK.
 * @param  power_on  [out] 1=on, 0=off.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL pointer;
 *                     WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_get_power_status(UINT8 *power_on);

/**
 * @brief  Power BLE on or off. Powering off drops any link and any scan in
 *         progress. Idempotent: asking for the current state does nothing.
 * @param  power_on  1=power on, 0=power off.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM if @p power_on is not
 *                     0 or 1; WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_set_power_status(UINT8 power_on);

/*******************************************************************************
** Identity and configuration
******************************************************************************/
/**
 * @brief  Read the BLE firmware version. @p out is NUL-terminated on success.
 * @param  out       [out] buffer for the version string.
 * @param  out_size  size of the buffer in bytes.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL pointer or
 *                     zero size; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_ble_init(); WM_SDK_RESULT_ERROR on no answer.
 */
wm_SdkResult wm_sdk_ble_get_version(char *out, UINT32 out_size);

/**
 * @brief  Read the advertised local name. @p out is NUL-terminated on success.
 * @param  out       [out] buffer for the name.
 * @param  out_size  size of the buffer (WM_SDK_BLE_NAME_MAX is enough).
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL pointer or
 *                     zero size; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_ble_init(); WM_SDK_RESULT_ERROR on no answer.
 */
wm_SdkResult wm_sdk_ble_get_name(char *out, UINT32 out_size);

/**
 * @brief  Set the advertised local name.
 * @param  name  NUL-terminated, at most 20 bytes, no comma.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on NULL, an empty or
 *                     over-long name, or a name containing a comma;
 *                     WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_set_name(const char *name);

/**
 * @brief  Read this device's own BLE address. @p out is NUL-terminated on
 *         success.
 * @param  out       [out] buffer for the address.
 * @param  out_size  size of the buffer (>= WM_SDK_BLE_ADDR_STR_LEN).
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL pointer or
 *                     too small a buffer; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_ble_init(); WM_SDK_RESULT_ERROR on no answer.
 */
wm_SdkResult wm_sdk_ble_get_address(char *out, UINT32 out_size);

/*******************************************************************************
** Observer - scanning
******************************************************************************/
/**
 * @brief  Register (or clear, with NULL) the scan report callback. Set it
 *         before starting a scan; reports with no callback are discarded.
 * @param  cb  callback, or NULL to stop notifications.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_set_scan_callback(wm_sdk_ble_scan_cb_t cb);

/**
 * @brief  Restrict scan reports to an allow-list of peer addresses.
 *
 *         The list is validated in full before it is applied, so a malformed
 *         entry leaves the previous filter unchanged. May be replaced while a
 *         scan is running; it takes effect on the next report.
 *
 *         Matching is on the address only. A peer that uses a rotating private
 *         address stops matching once it rotates - identify such devices by
 *         advertising payload instead (see wm_sdk_ble_adv_find).
 *
 * @param  addrs  array of @p count addresses, or NULL to clear the filter.
 * @param  count  0..WM_SDK_BLE_FILTER_MAX. 0 clears the filter, after which every
 *                report is delivered.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM if @p count exceeds
 *                     WM_SDK_BLE_FILTER_MAX or any entry is NULL or malformed;
 *                     WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_scan_set_filter(const char *addrs[], UINT8 count);

/**
 * @brief  Clear the scan filter. Equivalent to wm_sdk_ble_scan_set_filter(NULL, 0).
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_scan_clear_filter(void);

/**
 * @brief  Start reporting advertisements to the scan callback. Scanning may run
 *         while connected, and continues until wm_sdk_ble_scan_stop().
 *
 *         Use WM_SDK_BLE_SCAN_RAW to reach an advertising data type the radio does
 *         not decode; WM_SDK_BLE_SCAN_DECODED is enough when only the advertised
 *         name matters. Note that scanning holds off sleep.
 *
 * @param  mode  WM_SDK_BLE_SCAN_DECODED or WM_SDK_BLE_SCAN_RAW.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on any other mode;
 *                     WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_ble_init();
 *                     WM_SDK_RESULT_ERROR if the request was rejected.
 */
wm_SdkResult wm_sdk_ble_scan_start(wm_SdkBleScanMode mode);

/**
 * @brief  Stop scanning. Safe to call when no scan is running.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_ble_init(); WM_SDK_RESULT_ERROR if the request was
 *                     rejected.
 */
wm_SdkResult wm_sdk_ble_scan_stop(void);

/*******************************************************************************
** Central - connection and data
******************************************************************************/
/**
 * @brief  Select the remote service and characteristic used by wm_sdk_ble_send()
 *         and for notifications. Set before connecting; not applied to a live
 *         link.
 * @param  service         4 hex digits, e.g. WM_SDK_BLE_DEFAULT_SERVICE.
 * @param  characteristic  4 hex digits, e.g. WM_SDK_BLE_DEFAULT_CHAR.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM if either is NULL or
 *                     not exactly 4 hex digits; WM_SDK_RESULT_NOT_INITIALIZED
 *                     before wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_set_target(const char *service, const char *characteristic);

/**
 * @brief  Connect to a peer and wait for the link to become ready. Blocks for
 *         up to ~15 s, so call it from an application task.
 *
 *         Only one link exists at a time; connecting while already connected is
 *         rejected rather than replacing the existing link.
 *
 * @param  addr  peer address.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on NULL or a
 *                     malformed address; WM_SDK_RESULT_BUSY if a link is already
 *                     up; WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_ble_init();
 *                     WM_SDK_RESULT_ERROR if the connection did not come up.
 */
wm_SdkResult wm_sdk_ble_connect(const char *addr);

/**
 * @brief  Drop the current link. Succeeds and does nothing when not connected.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_ble_init(); WM_SDK_RESULT_ERROR if the request was
 *                     rejected.
 */
wm_SdkResult wm_sdk_ble_disconnect(void);

/**
 * @brief  Report whether a link is currently up.
 * @param  connected  [out] TRUE when connected.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL pointer;
 *                     WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_is_connected(BOOL *connected);

/**
 * @brief  Send bytes to the connected peer's target characteristic. Success
 *         means the bytes were accepted for transmission, not that the peer
 *         acknowledged them; wait for any reply on the data callback.
 * @param  data  bytes to send.
 * @param  len   number of bytes; must be non-zero. An over-long write is
 *               rejected.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on NULL or zero
 *                     length; WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_ble_init();
 *                     WM_SDK_RESULT_NOT_SUPPORTED when no link is up;
 *                     WM_SDK_RESULT_ERROR if the write was rejected.
 */
wm_SdkResult wm_sdk_ble_send(const UINT8 *data, UINT16 len);

/**
 * @brief  Register (or clear, with NULL) the link-event callback.
 * @param  cb  callback, or NULL.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_set_event_callback(wm_sdk_ble_evt_cb_t cb);

/**
 * @brief  Register (or clear, with NULL) the received-data callback.
 * @param  cb  callback, or NULL.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_set_data_callback(wm_sdk_ble_data_cb_t cb);

/**
 * @brief  Register (or clear, with NULL) the raw line diagnostics callback.
 *         Independent of the other callbacks and does not affect them.
 * @param  cb  callback, or NULL to stop notifications.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_set_line_callback(wm_sdk_ble_line_cb_t cb);

/**
 * @brief  Issue a raw BLE command with no wrapper of its own. Blocks until an
 *         answer arrives or the timeout expires, so call it from an application
 *         task.
 * @param  cmd         NUL-terminated command body, e.g. "AT+ADV?". The line
 *                     terminator is appended for you.
 * @param  resp        [out] buffer for any data returned before the final
 *                     status, NUL-terminated on success. NULL to discard it.
 * @param  resp_size   size of @p resp; ignored when @p resp is NULL.
 * @param  timeout_ms  reply timeout; 0 uses the default.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL or empty
 *                     command, or a non-NULL @p resp with zero size;
 *                     WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_ble_init();
 *                     WM_SDK_RESULT_ERROR on an error or no answer.
 */
wm_SdkResult wm_sdk_ble_send_at(const char *cmd, char *resp, UINT32 resp_size,
                          UINT32 timeout_ms);

/*******************************************************************************
** Advertising-payload helper
******************************************************************************/
/**
 * @brief  Find one advertising data structure inside a raw advertising payload.
 *
 *         A payload is a run of [length][type][data]. This returns the first
 *         structure of the requested type without copying, so the returned
 *         pointer is valid only as long as @p adv - inside a scan callback,
 *         only until it returns. A malformed payload reports "not found"
 *         rather than being read past.
 *
 *         Only meaningful for a report captured in WM_SDK_BLE_SCAN_RAW mode.
 *
 * @param  adv       advertising payload, e.g. wm_SdkBleScanResult.data.
 * @param  adv_len   valid bytes in @p adv, e.g. wm_SdkBleScanResult.len.
 * @param  ad_type   advertising data type, e.g. 0x09 (complete local name).
 * @param  out_data  [out] the structure's data bytes, type byte excluded.
 * @param  out_len   [out] number of data bytes; may be 0.
 * @return wm_SdkResult - 0 found; WM_SDK_RESULT_INVALID_PARAM on a NULL pointer;
 *                     WM_SDK_RESULT_ERROR when no such structure is present.
 */
wm_SdkResult wm_sdk_ble_adv_find(const UINT8 *adv, UINT8 adv_len, UINT8 ad_type,
                           const UINT8 **out_data, UINT8 *out_len);

#ifdef __cplusplus
}
#endif

#endif /* __WM_SDK_BLE_H__ */
