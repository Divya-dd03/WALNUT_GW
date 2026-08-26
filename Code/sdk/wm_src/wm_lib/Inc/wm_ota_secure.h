/**
 ******************************************************************************
 * @file    wm_ota_secure.h
 * @author  Walnut Medical
 * @brief   Public OTA API - MINI FOTA over HTTP, plus local package update.
 *
 *          MINI FOTA is the upgrade path for builds whose partition table has
 *          no 'updater' partition (no external flash). The package cannot be
 *          staged in the file system, so the module pulls it over HTTP itself.
 *          Build the package with wm_tools/wm_dfota_tool/wm_dfota_patch_make.bat
 *          (-MINI is the default), upload the resulting system_patch.bin, and
 *          hand the URL to wm_mini_dfota_start().
 *
 *          The module reboots into the mini system part way through the
 *          upgrade, so the outcome is delivered asynchronously through the
 *          kernel's miniFota result callback - typically in a LATER boot than
 *          the wm_mini_dfota_start() that triggered it. wm_mini_dfota_init()
 *          must therefore be called during start-up, not just before an
 *          upgrade.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical.
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __WM_OTA_SECURE_H__
#define __WM_OTA_SECURE_H__

/*******************************************************************************
** Header Files
******************************************************************************/
#include "sc_os.h"      /* SC_STATUS, BOOL, and osi_api.h via zx_api.h */

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** Defines
******************************************************************************/
/* Longest <url> accepted by wm_mini_dfota_start(), terminator excluded. */
#define WM_MINI_FOTA_URL_MAX    512

/* wm_mini_dfota_start() return codes. A request the module never saw is
 * reported with the module's own codes from osi_api.h, so the caller handles
 * one value space throughout:
 *      FOTA_MINI_ERR_PARAM (-1)  URL missing, empty or too long
 *      FOTA_MINI_ERR_NET   (-2)  PDP not active
 *      FOTA_MINI_ERR_PACKAGE (-3) reported by the module, via the callback
 * WM_MINI_FOTA_ERR_AT is ours: the module refused the request itself. */
#define WM_MINI_FOTA_OK          0
#define WM_MINI_FOTA_ERR_AT     -4

/*******************************************************************************
** Type Definitions
******************************************************************************/
/* Upgrade outcome, forwarded from the kernel's miniFota result callback.
 * 'status' is 0 on success, otherwise one of FOTA_MINI_ERR_* (osi_api.h).
 * Runs in the kernel callback context: keep it short, do not start another
 * upgrade from inside it. */
typedef void (*wm_mini_dfota_cb)(int status);

/*******************************************************************************
** Functions
******************************************************************************/
/* Hook the kernel's miniFota result callback and register the application
 * handler ('cb' may be NULL to only log the result). Call once during boot -
 * the upgrade reboots the module, so the result of an upgrade started in a
 * previous boot is delivered shortly after start-up. Idempotent. */
void wm_mini_dfota_init(wm_mini_dfota_cb cb);

/* Start a MINI FOTA upgrade of the package at 'url'.
 *
 * Rejects the request without touching the network if the URL is missing,
 * empty or too long (FOTA_MINI_ERR_PARAM), or if the PDP context is not active
 * (FOTA_MINI_ERR_NET) - the module fetches the package itself, so there is no
 * point starting without a data call.
 *
 * Returns WM_MINI_FOTA_OK once the module has ACCEPTED the request. The
 * download and upgrade then run in the background for roughly two minutes and
 * the module reboots part way through, so this is "started", not "upgraded" -
 * the real outcome arrives on the callback registered by wm_mini_dfota_init().
 * Keep the module powered for the whole upgrade. */
int wm_mini_dfota_start(const char *url);

#ifdef __cplusplus
}
#endif
#endif /* __WM_OTA_SECURE_H__ */
