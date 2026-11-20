/**
 ******************************************************************************
 * @file    wm_ble_secure.h
 * @author  Walnut Medical
 * @brief   Public BLE API for the external Jieli BLE module (AT command
 *          set rev 0.0.1).
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical.
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __WM_BLE_SECURE_H__
#define __WM_BLE_SECURE_H__

/*******************************************************************************
** Header Files
******************************************************************************/
#include "sc_os.h"      /* SC_STATUS, UINT types */

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** Defines
******************************************************************************/
#define WM_BLE_ADDR_STR_LEN     13      /* 12 hex digits + NUL              */
#define WM_BLE_ADV_MAX          31
#define WM_BLE_UUID_STR_LEN     33      /* 4 or 32 hex digits + NUL         */
#define WM_BLE_MAX_LINKS        3       /* central CIDs 0..2                */
#define WM_BLE_PAYLOAD_MAX      250
#define WM_BLE_SCAN_FILTER_MAX  16
#define WM_BLE_FW_CHUNK_MAX     128

#define WM_BLE_CMD_TIMEOUT_MS     3000
#define WM_BLE_CONN_TIMEOUT_MS    15000
#define WM_BLE_GATT_TIMEOUT_MS    10000
#define WM_BLE_FW_END_TIMEOUT_MS  30000
#define WM_BLE_READY_TIMEOUT_MS   30000

/* wm_ble_scan_start(NULL) timing, 1.25 ms units */
#define WM_BLE_SCAN_INTERVAL_DEF  96
#define WM_BLE_SCAN_WINDOW_DEF    48

#define WM_BLE_DEFAULT_SERVICE  "00FF"
#define WM_BLE_DEFAULT_CHAR     "FF01"

/* Peer address type, as in +SCANRAW and AT+CONN */
#define WM_BLE_ADDR_PUBLIC      0
#define WM_BLE_ADDR_RANDOM      1

/* Failure reason from wm_ble_get_last_err(): ERR:<code> 1..7, or local */
#define WM_BLE_ERR_NONE              0
#define WM_BLE_ERR_INVALID_COMMAND   1
#define WM_BLE_ERR_INVALID_PARAMETER 2
#define WM_BLE_ERR_INVALID_STATE     3
#define WM_BLE_ERR_NO_RESOURCES      4
#define WM_BLE_ERR_NOT_CONNECTED     5
#define WM_BLE_ERR_BLE_FAILURE       6
#define WM_BLE_ERR_FW_VERIFY         7
#define WM_BLE_ERR_TIMEOUT           (-1)   /* no answer from the module */

/*******************************************************************************
** Type Definitions
******************************************************************************/
typedef enum
{
    WM_BLE_EVT_READY = 0,       /* IM_READY - all link state is void   */
    WM_BLE_EVT_CONNECTED,       /* IM_CONN:<cid>                       */
    WM_BLE_EVT_DISCONNECTED     /* IM_DISC:<cid>                       */
} wm_ble_evt_e;

typedef struct
{
    char    addr[WM_BLE_ADDR_STR_LEN];
    UINT8   addr_type;                  /* 0 = public, 1 = random      */
    INT16   rssi;
    UINT8   pkt_type;                   /* 0 = adv, 1 = scan response  */
    UINT8   data[WM_BLE_ADV_MAX];
    UINT8   len;
} wm_ble_scan_result_t;

typedef struct
{
    UINT16  interval;           /* 2..8192                             */
    UINT16  window;             /* 2..interval                         */
    UINT8   filter_policy;      /* 0 = all, 1 = scan filter list only  */
} wm_ble_scan_param_t;

typedef struct
{
    UINT16  interval_min;       /* 6..3200                             */
    UINT16  interval_max;       /* interval_min..3200                  */
    UINT16  latency;            /* 0..499                              */
    UINT16  timeout;            /* 80..25600, multiple of 8            */
} wm_ble_conn_param_t;

typedef struct
{
    UINT16  start_handle;
    UINT16  end_handle;
    char    uuid[WM_BLE_UUID_STR_LEN];
} wm_ble_gatt_svc_t;

/* Run on the BLE task; make no blocking wm_ble_* call from inside one. */
typedef void (*wm_ble_evt_cb)(wm_ble_evt_e evt, int cid);
typedef void (*wm_ble_data_cb)(int cid, const UINT8 *data, UINT16 len);
typedef void (*wm_ble_scan_cb)(const wm_ble_scan_result_t *res);
typedef void (*wm_ble_line_cb)(const char *line, UINT16 len);

/*******************************************************************************
** Functions
******************************************************************************/
void      wm_ble_init(void);
SC_STATUS wm_ble_power_on(void);
SC_STATUS wm_ble_power_off(void);

/* System */
SC_STATUS wm_ble_get_version(char *out, int out_sz);
SC_STATUS wm_ble_get_addr(char *out, int out_sz);
SC_STATUS wm_ble_get_baud(UINT32 *baud);
SC_STATUS wm_ble_set_baud(UINT32 baud);        /* also moves the host UART */
SC_STATUS wm_ble_get_txpower(INT8 *dbm);
SC_STATUS wm_ble_set_txpower(INT8 dbm);
SC_STATUS wm_ble_get_lowpower(bool *enable);
SC_STATUS wm_ble_set_lowpower(bool enable);

/* Observer. NULL param = default timing, filter policy 0. */
SC_STATUS wm_ble_scan_start(const wm_ble_scan_param_t *param);
SC_STATUS wm_ble_scan_stop(void);
void      wm_ble_set_scan_cb(wm_ble_scan_cb cb);

SC_STATUS wm_ble_scan_filter_add(const char *addr);
SC_STATUS wm_ble_scan_filter_remove(const char *addr);
SC_STATUS wm_ble_scan_filter_clear(void);
SC_STATUS wm_ble_scan_filter_get(char out[][WM_BLE_ADDR_STR_LEN], int max, int *count);

/* Central */
SC_STATUS wm_ble_get_conn_param(wm_ble_conn_param_t *param);
SC_STATUS wm_ble_set_conn_param(const wm_ble_conn_param_t *param);
SC_STATUS wm_ble_set_target_uuid(const char *service, const char *characteristic);

/* Blocks until IM_CONN; addr_type is WM_BLE_ADDR_PUBLIC/RANDOM. The allocated
 * CID goes to *cid_out (may be NULL). */
SC_STATUS wm_ble_connect(const char *addr, UINT8 addr_type, int *cid_out);
SC_STATUS wm_ble_disconnect(int cid);

bool      wm_ble_is_connected(void);            /* any CID up          */
bool      wm_ble_is_cid_connected(int cid);
UINT8     wm_ble_get_link_mask(void);           /* bit n = CID n up    */
int       wm_ble_get_cid(void);                 /* lowest live CID, -1 */

SC_STATUS wm_ble_send(int cid, const UINT8 *data, UINT16 len);

/* Blocks until +GATTDONE. service_uuid NULL = all services. */
SC_STATUS wm_ble_gatt_discover(int cid, const char *service_uuid,
                               wm_ble_gatt_svc_t *out, int max,
                               int *count, int *status);

void      wm_ble_set_evt_cb(wm_ble_evt_cb cb);
void      wm_ble_set_data_cb(wm_ble_data_cb cb);
void      wm_ble_set_line_cb(wm_ble_line_cb cb);

/* Firmware update */
SC_STATUS wm_ble_fw_start(UINT32 size, UINT32 crc32);
SC_STATUS wm_ble_fw_data(UINT32 offset, const UINT8 *data, UINT16 len);
SC_STATUS wm_ble_fw_end(void);
SC_STATUS wm_ble_fw_update_file(const char *path);  /* waits for IM_READY */

/* Raw command; CR is appended. Last +record value is copied to 'resp'. */
SC_STATUS wm_ble_send_at(const char *cmd, char *resp, int resp_sz, UINT32 timeout_ms);

/* WM_BLE_ERR_* for the most recent wm_ble_* call that returned SC_FAIL.
 * Shared by all tasks: read it straight after the failing call. */
int       wm_ble_get_last_err(void);

#ifdef __cplusplus
}
#endif
#endif /* __WM_BLE_SECURE_H__ */
