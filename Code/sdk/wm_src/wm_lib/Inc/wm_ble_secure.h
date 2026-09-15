/**
 ******************************************************************************
 * @file    wm_ble_secure.h
 * @author  Walnut Medical
 * @brief   Public BLE API for the external Jieli BLE module.
 *
 *          Drives the module over the wm_uart BLE channel (115200 8N1) using
 *          its AT command set. Commands are serialised and block until the
 *          module answers OK/ERR or the timeout expires; asynchronous events
 *          (connection, incoming data, scan reports) arrive on callbacks from
 *          a dedicated RTOS task. Payloads are raw bytes - any application
 *          framing on top belongs to the caller.
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
#define WM_BLE_ADDR_STR_LEN   13    /* 12 hex digits + NUL                    */
#define WM_BLE_NAME_MAX       21    /* 20 UTF-8 bytes + NUL                   */
#define WM_BLE_ADV_MAX        31    /* one BLE advertising payload            */
#define WM_BLE_UUID_STR_LEN   5     /* 4 hex digits + NUL (16-bit UUID only)  */

/* Channel id reserved by the module for AT-command mode. */
#define WM_BLE_CHANNEL_AT     9

/* Timeouts (ms). A connect is slower than an ordinary command: AT+CONN returns
 * OK as soon as the request is accepted, and the link is only ready when the
 * module reports it. */
#define WM_BLE_CMD_TIMEOUT_MS   3000
#define WM_BLE_CONN_TIMEOUT_MS  15000

/* Default Auto Guard target, for wm_ble_set_target_uuid(). 16-bit UUIDs only. */
#define WM_BLE_DEFAULT_SERVICE  "00FF"
#define WM_BLE_DEFAULT_CHAR     "FF01"

/*******************************************************************************
** Type Definitions
******************************************************************************/
typedef enum
{
    WM_BLE_EVT_READY = 0,       /* IM_READY - module (re)initialised; all link
                                 * state is void and must be rebuilt          */
    WM_BLE_EVT_CONNECTED,       /* IM_CONN:<cid> - link ready on that channel */
    WM_BLE_EVT_DISCONNECTED     /* IM_DISC:<cid> - channel went down          */
} wm_ble_evt_e;

typedef enum
{
    WM_BLE_SCAN_OFF     = 0,
    WM_BLE_SCAN_DECODED = 1,    /* module decodes NAME/UUID/MANU for us       */
    WM_BLE_SCAN_RAW     = 2     /* whole advertising payload per report       */
} wm_ble_scan_mode_e;

/* One scan report. In raw mode 'data' holds the complete advertising payload
 * and 'name' is empty; in decoded mode 'data' is empty and 'name' carries the
 * advertised local name when the module reported one. */
typedef struct
{
    char    addr[WM_BLE_ADDR_STR_LEN];  /* 12 uppercase hex digits            */
    UINT8   addr_type;
    INT16   rssi;                       /* dBm, negative                      */
    UINT8   pkt_type;                   /* 0 = advertisement, 1 = scan response */
    UINT8   data[WM_BLE_ADV_MAX];       /* raw AD structures (raw mode)        */
    UINT8   len;                        /* bytes valid in 'data'               */
    char    name[WM_BLE_NAME_MAX];      /* advertised name (decoded mode)      */
} wm_ble_scan_result_t;

/* All three run on the BLE task (safe context). Pointers are valid only for the
 * duration of the call - copy out anything you need to keep. Keep the work
 * short: the parser task is blocked while a callback runs, and no blocking
 * wm_ble_* call may be made from inside one. */
typedef void (*wm_ble_evt_cb)(wm_ble_evt_e evt, int cid);
typedef void (*wm_ble_data_cb)(int cid, const UINT8 *data, UINT16 len);
typedef void (*wm_ble_scan_cb)(const wm_ble_scan_result_t *res);

/* Every complete line the module sends, before it is classified or parsed - so
 * unrecognised and malformed lines are visible too. Diagnostics only; parsing
 * is unaffected by whether this is set. */
typedef void (*wm_ble_line_cb)(const char *line, UINT16 len);

/*******************************************************************************
** Functions
******************************************************************************/
/* Create the mutexes/flag/queue/task and power the module on. Idempotent, and
 * a no-op on a board whose BLE_SUPPORT flag is FALSE. */
void      wm_ble_init(void);

/* Power the module on (enable pin + UART open) / off (UART close + disable). */
SC_STATUS wm_ble_power_on(void);
SC_STATUS wm_ble_power_off(void);

/* Identity / configuration. 'out' is always NUL-terminated on success. */
SC_STATUS wm_ble_get_version(char *out, int out_sz);    /* AT+GVER    */
SC_STATUS wm_ble_get_name(char *out, int out_sz);       /* AT+NAME?   */
SC_STATUS wm_ble_set_name(const char *name);            /* <= 20 bytes, no comma */
SC_STATUS wm_ble_get_addr(char *out, int out_sz);       /* AT+LBDADDR? */

/* Observer scanning. Reports arrive on the scan callback until scan_stop().
 * Note scanning keeps the UART busy, which holds off sleep - scan in windows
 * rather than leaving it on. */
SC_STATUS wm_ble_scan_start(wm_ble_scan_mode_e mode);
SC_STATUS wm_ble_scan_stop(void);
void      wm_ble_set_scan_cb(wm_ble_scan_cb cb);

/* Central connection. Both UUIDs are exactly 4 hex digits - the module has no
 * 128-bit form. Set the target before connecting. */
SC_STATUS wm_ble_set_target_uuid(const char *service, const char *characteristic);

/* Connect to a peer and wait for the link to become ready. Returns SC_SUCCESS
 * only after the module reports the connection up; a request the module accepts
 * but cannot complete returns SC_FAIL. 'addr' is 12 hex digits, exactly as the
 * scanner reported it (do not reverse it). */
SC_STATUS wm_ble_connect(const char *addr);
SC_STATUS wm_ble_disconnect(int cid);

bool      wm_ble_is_connected(void);
int       wm_ble_get_cid(void);     /* live channel id, or -1 when not connected */

/* Send application bytes to the connected peer's target characteristic.
 * SC_SUCCESS means the module accepted them - it is not a peer acknowledgement;
 * wait for the reply on the data callback when the protocol expects one. */
SC_STATUS wm_ble_send(int cid, const UINT8 *data, UINT16 len);

/* Register (or clear, with NULL) the asynchronous callbacks. */
void      wm_ble_set_evt_cb(wm_ble_evt_cb cb);
void      wm_ble_set_data_cb(wm_ble_data_cb cb);
void      wm_ble_set_line_cb(wm_ble_line_cb cb);

/* Escape hatch for a command with no wrapper above. Pass the command body only
 * ("AT+ADV?"); the UART terminator is appended for you. Any data record the
 * module returns before OK is copied into 'resp' (pass NULL to discard it).
 * timeout_ms of 0 uses the module default. */
SC_STATUS wm_ble_send_at(const char *cmd, char *resp, int resp_sz, UINT32 timeout_ms);

#ifdef __cplusplus
}
#endif
#endif /* __WM_BLE_SECURE_H__ */
