/**
  ******************************************************************************
  * @file    wm_ui_smp.h
  * @author  Walnut Medical
  * @brief   Common Gateway (WEGW) reference application - MCUmgr SMP client.
  *
  *          Drives device firmware update on an MCUboot / nRF Connect SDK
  *          peripheral over one BLE link, using the SMP transport.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 Walnut Medical.
  * All rights reserved.
  *
  ******************************************************************************
  */
#ifndef __WM_UI_SMP__H__
#define __WM_UI_SMP__H__

/* Header Files */
#include "wm_global.h"
#include "wm_sdk_api.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Transport */
/* The SMP service and characteristic, in the order wm_sdk_ble_set_target wants. */
#define WM_SMP_SERVICE_UUID   "8D53DC1D1DB74CD3868B8A527460AA84"
#define WM_SMP_CHAR_UUID      "DA2E7828FBCE4E01AE9E261174997C48"

/* Length of the MCUboot image hash the image group reports. */
#define WM_SMP_HASH_LEN       32u

/* Largest SMP packet sent, held to what one wm_sdk_ble_send call accepts. */
#define WM_SMP_PKT_MAX        WM_SDK_BLE_SEND_MAX

/* A reply is reassembled from several notifications, so it may be larger. */
#define WM_SMP_RX_MAX         768u

/* Client */
/* Bind the client to a live link and create its response queue. */
wm_SdkResult wm_ui_smp_open(int cid);

/* Release the queue binding. The link itself is the caller's to drop. */
void wm_ui_smp_close(void);

/* Feed bytes from the BLE data callback. Callback-safe: never blocks. */
void wm_ui_smp_rx(const UINT8 *data, UINT16 len);

/* Round-trip an OS echo, to prove framing before a long transfer. */
/* A reply refusing the command still passes; only silence is a failure. */
wm_SdkResult wm_ui_smp_echo(UINT32 timeout_ms);

/* Upload the file at 'path' into the secondary slot, printing progress. */
wm_SdkResult wm_ui_smp_upload(const char *path, UINT32 total, UINT32 timeout_ms);

/* Read the image state and copy the hash the device reports for 'slot'. */
wm_SdkResult wm_ui_smp_image_hash(UINT8 slot, UINT8 *hash_out, UINT32 timeout_ms);

/* Mark the image with this hash for test: MCUboot boots it once, and reverts */
/* on the next reboot unless it is confirmed after it comes up. */
wm_SdkResult wm_ui_smp_image_test(const UINT8 *hash, UINT32 timeout_ms);

/* Make an image permanent. A NULL hash confirms the one already running. */
wm_SdkResult wm_ui_smp_image_confirm(const UINT8 *hash, UINT32 timeout_ms);

/* Reboot the peripheral. The reply may be lost as it goes down; that is fine. */
wm_SdkResult wm_ui_smp_reset(UINT32 timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* __WM_UI_SMP__H__ */
