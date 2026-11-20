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
 *          and a central holding up to WM_SDK_BLE_MAX_LINKS links. Each link is
 *          identified by the channel id (cid, 0..2) wm_sdk_ble_connect() returns;
 *          wm_sdk_ble_send() writes to the target characteristic on that link
 *          and replies arrive on the data callback with the same cid.
 *
 *          Payloads are carried as raw bytes. Any framing on top of them
 *          belongs to the application.
 *
 *          When the radio rejects or does not answer a request, the call
 *          returns WM_SDK_RESULT_INVALID_PARAM (bad parameter), _BUSY (wrong
 *          state, or no free resource), _NOT_SUPPORTED (no link), _TIMEOUT (no
 *          answer) or _ERROR (any other failure).
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

/* Default target for wm_sdk_ble_set_target(). */
#define WM_SDK_BLE_DEFAULT_SERVICE  "00FF"
#define WM_SDK_BLE_DEFAULT_CHAR     "FF01"

/*******************************************************************************
** Type Definitions
**
** All four callbacks run on the SDK's BLE service context. Keep them short and
** make no blocking wm_sdk_ble_* call from inside one. Pointers passed in are
** valid only for the duration of the call; copy out whatever must be kept.
******************************************************************************/
/* One advertisement or scan response passing the scan filter. */
typedef void (*wm_sdk_ble_scan_cb_t)(const wm_SdkBleScanResult *res);

/* Link state change on channel @p cid. After WM_SDK_BLE_EVT_READY no link
 * exists, scanning has stopped and the radio settings below are back to the
 * module's stored values. */
typedef void (*wm_sdk_ble_evt_cb_t)(wm_SdkBleEvent evt, int cid);

/* Bytes received on link @p cid, one call per notification. One application
 * message may arrive split across several calls, so a framed protocol needs
 * its own reassembly. */
typedef void (*wm_sdk_ble_data_cb_t)(int cid, const UINT8 *data, UINT16 len);

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
 * @brief  Power BLE on or off. Powering off drops every link and any scan in
 *         progress. Idempotent: asking for the current state does nothing.
 *         After power-on, the first command waits until the radio reports
 *         ready.
 * @param  power_on  1=power on, 0=power off.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM if @p power_on is not
 *                     0 or 1; WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_set_power_status(UINT8 power_on);

/*******************************************************************************
** Identity and radio settings
**
** The link to the radio opens at 115200 after every power-on. These calls
** block, so make them from an application task.
******************************************************************************/
/**
 * @brief  Read the BLE firmware version. @p out is NUL-terminated on success.
 * @param  out       [out] buffer for the version string.
 * @param  out_size  size of the buffer in bytes.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL pointer or
 *                     zero size; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_get_version(char *out, UINT32 out_size);

/**
 * @brief  Read this device's own BLE address. @p out is NUL-terminated on
 *         success.
 * @param  out       [out] buffer for the address.
 * @param  out_size  size of the buffer (>= WM_SDK_BLE_ADDR_STR_LEN).
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL pointer or
 *                     too small a buffer; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_get_address(char *out, UINT32 out_size);

/**
 * @brief  Read / set the baud rate of the link to the radio.
 * @param  baud  9600, 19200, 38400, 115200 (default), 230400, 460800 or 921600.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL pointer or
 *                     an unsupported rate; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_get_baud(UINT32 *baud);
wm_SdkResult wm_sdk_ble_set_baud(UINT32 baud);

/**
 * @brief  Read / set the transmit power in dBm. Applies to later scanning,
 *         connecting and connected transmissions.
 * @param  dbm  a value the radio supports; others are WM_SDK_RESULT_INVALID_PARAM.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL pointer or
 *                     unsupported value; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_get_tx_power(INT8 *dbm);
wm_SdkResult wm_sdk_ble_set_tx_power(INT8 dbm);

/**
 * @brief  Read / set whether the radio may enter low-power mode on its own.
 * @param  enable  TRUE to allow, FALSE to keep it awake.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL pointer;
 *                     WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_get_low_power(BOOL *enable);
wm_SdkResult wm_sdk_ble_set_low_power(BOOL enable);

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
 * @brief  Set the scan timing. Default interval 96, window 48. Applied to the
 *         next scan, and at once to a scan already running.
 * @param  param  timing; see wm_SdkBleScanParam.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on NULL or an
 *                     out-of-range value; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_scan_set_param(const wm_SdkBleScanParam *param);

/**
 * @brief  Read the scan timing set by wm_sdk_ble_scan_set_param().
 * @param  param  [out] timing.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on NULL;
 *                     WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_scan_get_param(wm_SdkBleScanParam *param);

/**
 * @brief  Restrict scan reports to an allow-list of peer addresses. The
 *         radio applies it, so other devices cost nothing.
 *
 *         The list is validated in full before it is applied, so a malformed
 *         entry leaves the previous filter unchanged. If the radio rejects
 *         the list, the filter is left empty. May be replaced while a scan is
 *         running.
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
 * @brief  Read the filter list as the radio holds it.
 * @param  out    [out] up to @p max addresses.
 * @param  max    entries @p out can hold.
 * @param  count  [out] entries in the radio's list (may exceed @p max).
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL pointer;
 *                     WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_scan_get_filter(char out[][WM_SDK_BLE_ADDR_STR_LEN], UINT8 max,
                                        UINT8 *count);

/**
 * @brief  Start reporting advertisements and scan responses to the scan
 *         callback. Scanning may run while connected, and continues until
 *         wm_sdk_ble_scan_stop().
 *
 *         Note that scanning holds off sleep.
 *
 * @param  mode  WM_SDK_BLE_SCAN_RAW.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on any other mode;
 *                     WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_scan_start(wm_SdkBleScanMode mode);

/**
 * @brief  Stop scanning. Safe to call when no scan is running.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_scan_stop(void);

/*******************************************************************************
** Central - connection and data
******************************************************************************/
/**
 * @brief  Select the remote service and characteristic used on every link
 *         by wm_sdk_ble_send() and for notifications. Set before connecting;
 *         not applied to a live link.
 * @param  service         4 or 32 hex digits, e.g. WM_SDK_BLE_DEFAULT_SERVICE.
 * @param  characteristic  4 or 32 hex digits, e.g. WM_SDK_BLE_DEFAULT_CHAR.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM if either is NULL or
 *                     not 4 or 32 hex digits; WM_SDK_RESULT_NOT_INITIALIZED
 *                     before wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_set_target(const char *service, const char *characteristic);

/**
 * @brief  Read / set the parameters used for new links. A live link keeps the
 *         parameters it was opened with.
 * @param  param  see wm_SdkBleConnParam.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on NULL or an
 *                     invalid combination; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_get_conn_param(wm_SdkBleConnParam *param);
wm_SdkResult wm_sdk_ble_set_conn_param(const wm_SdkBleConnParam *param);

/**
 * @brief  Connect to a peer and wait until its target characteristic is ready.
 *         Blocks for up to ~15 s, so call it from an application task. The
 *         peer should have been seen by a scan first.
 * @param  addr       peer address.
 * @param  addr_type  WM_SDK_BLE_ADDR_PUBLIC or WM_SDK_BLE_ADDR_RANDOM, as the
 *                    peer's scan report gives it (wm_SdkBleScanResult.addr_type).
 * @param  cid        [out] channel id of the new link, 0..2; may be NULL.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on NULL, a
 *                     malformed address or an unknown @p addr_type;
 *                     WM_SDK_RESULT_BUSY when all links are in use;
 *                     WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_ble_init();
 *                     WM_SDK_RESULT_TIMEOUT or WM_SDK_RESULT_ERROR if the
 *                     connection did not come up.
 */
wm_SdkResult wm_sdk_ble_connect(const char *addr, UINT8 addr_type, int *cid);

/**
 * @brief  Drop link @p cid. Completion is reported as WM_SDK_BLE_EVT_DISCONNECTED.
 * @param  cid  0..WM_SDK_BLE_MAX_LINKS-1.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a bad cid;
 *                     WM_SDK_RESULT_NOT_SUPPORTED when that link is down;
 *                     WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_disconnect(int cid);

/**
 * @brief  Drop every link. Succeeds and does nothing when none is up.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_ble_init(); otherwise the first failure.
 */
wm_SdkResult wm_sdk_ble_disconnect_all(void);

/**
 * @brief  Report whether link @p cid is up.
 * @param  cid        0..WM_SDK_BLE_MAX_LINKS-1.
 * @param  connected  [out] TRUE when connected.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a bad cid or a
 *                     NULL pointer; WM_SDK_RESULT_NOT_INITIALIZED before
 *                     wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_is_connected(int cid, BOOL *connected);

/**
 * @brief  Report every link at once.
 * @param  mask  [out] bit n set when link n is up.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL pointer;
 *                     WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_get_link_mask(UINT8 *mask);

/**
 * @brief  Send bytes to the target characteristic on link @p cid. Success
 *         means the bytes were accepted for transmission, not that the peer
 *         acknowledged them; wait for any reply on the data callback.
 * @param  cid   0..WM_SDK_BLE_MAX_LINKS-1.
 * @param  data  bytes to send.
 * @param  len   1..WM_SDK_BLE_SEND_MAX.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a bad cid, NULL
 *                     or a bad length; WM_SDK_RESULT_NOT_SUPPORTED when that link
 *                     is down; WM_SDK_RESULT_BUSY when the radio's buffer is full;
 *                     WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_send(int cid, const UINT8 *data, UINT16 len);

/**
 * @brief  List the peer's primary services on link @p cid, in handle order.
 *         Blocks for up to ~10 s; one discovery runs at a time.
 * @param  cid           0..WM_SDK_BLE_MAX_LINKS-1.
 * @param  service_uuid  4 or 32 hex digits to look for one service, or NULL for
 *                       all.
 * @param  out           [out] up to @p max services; may be NULL when @p max is 0.
 * @param  max           entries @p out can hold.
 * @param  count         [out] services found (may exceed @p max); 0 is a valid
 *                       answer.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a bad cid, UUID
 *                     or pointer; WM_SDK_RESULT_NOT_SUPPORTED when that link is
 *                     down; WM_SDK_RESULT_BUSY while another discovery runs;
 *                     WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_gatt_discover(int cid, const char *service_uuid,
                                      wm_SdkBleGattService *out, UINT8 max,
                                      UINT8 *count);

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
 * @param  cmd         NUL-terminated command body, e.g. "AT+GVER". The line
 *                     terminator is appended for you.
 * @param  resp        [out] buffer for any data returned before the final
 *                     status, NUL-terminated on success. NULL to discard it.
 * @param  resp_size   size of @p resp; ignored when @p resp is NULL.
 * @param  timeout_ms  reply timeout; 0 uses the default.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on a NULL or empty
 *                     command, or a non-NULL @p resp with zero size;
 *                     WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_ble_init().
 */
wm_SdkResult wm_sdk_ble_send_at(const char *cmd, char *resp, UINT32 resp_size,
                          UINT32 timeout_ms);

/*******************************************************************************
** Radio firmware update
******************************************************************************/
/**
 * @brief  Replace the radio's firmware with the image at @p path, verify it and
 *         restart the radio. Blocks until the radio is back (about a minute),
 *         so call it from an application task. Scanning is stopped; restart it
 *         afterwards. A failed update leaves the old firmware running.
 * @param  path  file system path of the image.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_INVALID_PARAM on NULL or an
 *                     unreadable image; WM_SDK_RESULT_BUSY while any link is up;
 *                     WM_SDK_RESULT_NOT_INITIALIZED before wm_sdk_ble_init();
 *                     WM_SDK_RESULT_ERROR if the radio rejected the image.
 */
wm_SdkResult wm_sdk_ble_fw_update(const char *path);

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
