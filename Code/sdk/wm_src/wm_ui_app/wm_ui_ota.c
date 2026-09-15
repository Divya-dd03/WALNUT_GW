/**
  ******************************************************************************
  * @file    wm_ui_ota.c
  * @author  Walnut Medical
  * @brief   Common Gateway (WEGW) reference application - OTA / DFOTA demos.
  *
  *          Three menu handlers:
  *
  *            versions  read the application and platform SDK version, and
  *                      report what is currently staged
  *            OTA       whole application update over the wm_sdk_ota_*
  *                      file-staging API: prompt for the URL -> download to
  *                      C: -> prompt for SHA-256 -> verify -> arm -> restart
  *            DFOTA     kernel delta patch over MINI FOTA
  *                      (wm_sdk_ota_mini_dfota_*): prompt for the URL and hand
  *                      it to the module, which fetches and applies it
  *                      itself over HTTP - no local staging or verify
  *
  *          MINI FOTA needs kernel g496 or newer.
  *
  *          Both options prompt, so they run on the "UIPROC" dispatcher and
  *          hold the menu until answered (OTA also blocks for the download).
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "wm_ui_ota.h"
#include "wm_demo_certs.h"   /* wm_cacert - Amazon Root CA 1 */

/*******************************************************************************
** Trust anchor for the image host
**
** NULL by default, deliberately: the URL is typed in at run time, so no
** compiled-in anchor can be assumed to sign it. The transfer still negotiates
** TLS, but the server is not authenticated - which is why the SHA-256 step is
** mandatory rather than advisory here. Each run prints which mode it used.
**
** Set this to wm_cacert when the image host chains to Amazon Root CA 1 (the
** anchor this image already carries for the MQTT and API demos). To use the
** anchor provisioned on the unit instead, read it into the file-scope buffer
** below with wm_sdk_storage_cred_read(WM_SDK_STORAGE_CRED_ROOT_CA, ...) and point
** s_ota_ca at that: the PEM is held by reference for the life of the session,
** so it must not be a buffer local to a function.
******************************************************************************/
static const char *s_ota_ca = NULL;   /* or wm_cacert - see above */

#define WM_OTA_TIMEOUT      (30u)     /* seconds, per chunk request            */

/*******************************************************************************
** Demo image - a bench artefact for trying the OTA flow end to end
**
** Paste the link at the first prompt and the digest at the second:
**
**   OTA LINK:
**     https://walnutmedical.website/downloadDevData/1785333970441_customer_app.bin?token=WgMLYoPjS56HqtD
**   SHA256:
**     d8d4a591b3ec0757c78d8561b7b5ea707abd1b36ba67ff6af447973829a16474
**
** IMPORTANT: in the SPT tool, UNCHECK "send with \r\n" before sending either
** value. Left checked, the line terminator travels as part of the string, and a
** URL or digest with a terminator glued to it is not the one you typed.
**
** This host is signed by a root this image does not carry, which is why
** s_ota_ca above is NULL - the transfer is encrypted but unauthenticated, and
** the SHA-256 step is what makes it safe to apply.
******************************************************************************/

/*******************************************************************************
** Shared helpers
******************************************************************************/
/* Read one line from the console. Returns the message so the caller can free
 * arg3, or a zeroed message when nothing arrived. The trailing newline is
 * stripped so the value can be compared and printed directly. */
static SIM_MSG_T wm_ota_prompt(const char *what)
{
    SIM_MSG_T msg;

    wm_printf("%s", what);

    msg = GetParamFromUart();

    if (msg.arg3 != NULL)
        (void)strtok((char *)msg.arg3, "\r\n");

    return msg;
}

/* Report whether an image is staged, and how big it is. */
static void wm_ota_report_staged(UINT32 type)
{
    const char *path = wm_sdk_ota_get_image_path(type);
    void       *f;
    UINT32      size = 0;

    if (wm_sdk_file_exists(path) != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("staged: none at %s\r\n", path);
        return;
    }

    f = wm_sdk_file_open(path, "rb");
    if (f != NULL)
    {
        (void)wm_sdk_file_get_size(f, &size);
        (void)wm_sdk_file_close(f);
    }

    wm_printf("staged: %s (%lu bytes)\r\n", path, (unsigned long)size);
}

/* Free space on the staging volume in KB, or -1 if it cannot be determined. */
static INT64 wm_ota_free_kb(void)
{
    UINT32 ram_total = 0, ram_free = 0;
    INT64  flash_total = 0, flash_free = 0;
    UINT8  cpu = 0;

    if (wm_sdk_system_get_stats(&ram_total, &ram_free, &flash_total, &flash_free, &cpu)
            != WM_SDK_RESULT_SUCCESS)
        return -1;

    return flash_free;
}

/* Download @p url into the staging file for @p type. */
static BOOL wm_ota_download(UINT32 type, const char *url)
{
    UINT32    size = 0;
    INT64     free_kb;
    wm_SdkResult rc;
    BOOL      ok = FALSE;

    wm_printf("-> %s\r\n", wm_sdk_ota_get_image_path(type));

    if (!gf_pdp_ready)
        wm_printf("note: no PDP context is up, so this will fail\r\n");

    /* The download path is synchronous whichever mode is selected; picking sync
     * leaves the HTTPS client where the other demos expect it. */
    if (wm_sdk_ota_download_init(WM_SDK_HTTPS_MODE_SYNC, NULL) != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("init failed\r\n");
        return FALSE;
    }

    if (wm_sdk_ota_set_image_type(type) != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("set_image_type failed\r\n");
        return FALSE;
    }

    wm_printf("tls: server verification %s\r\n", (s_ota_ca != NULL) ? "on" : "OFF");

    if (wm_sdk_ota_download_configure_ssl(WM_SDK_HTTPS_DOWNLOAD_INDEX, s_ota_ca)
            != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("configure_ssl failed\r\n");
        return FALSE;
    }

    if (wm_sdk_ota_download_set_params(WM_SDK_HTTPS_DOWNLOAD_INDEX, url, WM_OTA_TIMEOUT)
            != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("set_params failed (is the URL too long?)\r\n");
        return FALSE;
    }

    /* Size first, so a transfer is never started against a volume that cannot
     * hold the result. */
    if (wm_sdk_ota_download_get_file_size(WM_SDK_HTTPS_DOWNLOAD_INDEX, &size)
            != WM_SDK_RESULT_SUCCESS || size == 0u)
    {
        wm_printf("size query failed - either the server does not support ranged "
                  "requests, or the trust anchor does not sign it\r\n");
        (void)wm_sdk_ota_download_terminate(WM_SDK_HTTPS_DOWNLOAD_INDEX);
        return FALSE;
    }

    free_kb = wm_ota_free_kb();
    wm_printf("image %lu bytes, free flash ", (unsigned long)size);
    if (free_kb < 0)
        wm_printf("unknown\r\n");
    else
        wm_printf("%ld KB\r\n", (long)free_kb);

    if (free_kb >= 0 && free_kb < (INT64)(size / 1024u))
    {
        wm_printf("not enough free flash to stage this image\r\n");
        (void)wm_sdk_ota_download_terminate(WM_SDK_HTTPS_DOWNLOAD_INDEX);
        return FALSE;
    }

    wm_printf("downloading - this takes minutes for a full image\r\n");

    rc = wm_sdk_ota_download_action(WM_SDK_HTTPS_DOWNLOAD_INDEX);

    if (rc == WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("download SUCCESS\r\n");
        ok = TRUE;
    }
    else
    {
        wm_printf("download FAILED -> rc=%ld\r\n", (long)rc);
        wm_printf("any partial file left staged will not verify\r\n");
    }

    (void)wm_sdk_ota_download_terminate(WM_SDK_HTTPS_DOWNLOAD_INDEX);

    wm_ota_report_staged(type);
    return ok;
}

/* Hash the staged image, show the digest, and compare it with @p expected. */
static BOOL wm_ota_verify(UINT32 type, const char *expected)
{
    char      actual[WM_SDK_OTA_SHA256_HEX_LEN + 1] = {0};
    wm_SdkResult rc;

    if (wm_sdk_ota_get_image_hash(type, actual, sizeof(actual)) != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("could not hash %s\r\n", wm_sdk_ota_get_image_path(type));
        return FALSE;
    }

    wm_printf("computed = %s\r\n", actual);
    wm_printf("expected = %s\r\n", expected);

    rc = wm_sdk_ota_verify_image(type, expected);

    if (rc == WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("SHA256 MATCHES - image verified\r\n");
        return TRUE;
    }

    if (rc == WM_SDK_RESULT_INVALID_PARAM)
        wm_printf("SHA256 not checked: the value entered is not 64 hex chars\r\n");
    else
        wm_printf("SHA256 DOES NOT MATCH\r\n");

    return FALSE;
}

/* The APP image update flow: download to C: staging, verify, arm, reboot. */
static void wm_ota_run_update(void)
{
    const UINT32 type = (UINT32)WM_SDK_OTA_IMAGE_APP;
    SIM_MSG_T msg;
    char      expected[WM_SDK_OTA_SHA256_HEX_LEN + 1] = {0};
    wm_SdkResult rc;

    wm_ota_report_staged(type);

    /* The prompts take the value exactly as it arrives, so a line terminator
     * sent along with it becomes part of the URL or the digest. */
    wm_printf("NOTE: uncheck \"send with \\r\\n\" in the SPT tool before sending "
              "the link and the SHA256\r\n\r\n");

    /* --- 1. URL ---------------------------------------------------------- */
    msg = wm_ota_prompt("Enter download link of the image file: ");
    if (msg.arg3 == NULL)
    {
        wm_printf("\r\nno link entered\r\n");
        return;
    }
    wm_printf("\r\n%s\r\n", (char *)msg.arg3);

    /* --- 2. Download ----------------------------------------------------- */
    if (!wm_ota_download(type, (const char *)msg.arg3))
    {
        wm_sdk_memory_free(msg.arg3);
        return;
    }
    wm_sdk_memory_free(msg.arg3);

    /* --- 3. Expected digest ---------------------------------------------- */
    msg = wm_ota_prompt("Enter SHA256 of the image file: ");
    if (msg.arg3 == NULL)
    {
        wm_printf("\r\nno digest entered; the image is staged but not applied\r\n");
        return;
    }
    strncpy(expected, (const char *)msg.arg3, sizeof(expected) - 1);
    wm_sdk_memory_free(msg.arg3);
    wm_printf("\r\n");

    /* --- 4. Verify ------------------------------------------------------- */
    if (!wm_ota_verify(type, expected))
    {
        wm_printf("refusing to apply an unverified APP_OTA image\r\n");
        return;
    }

    /* --- 5. Arm ---------------------------------------------------------- */
    rc = wm_sdk_ota_app_update();
    if (rc != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("APP_OTA FAILED -> rc=%ld (nothing applied, no reboot)\r\n",
                  (long)rc);
        return;
    }

    wm_printf("APP_OTA SUCCESS - rebooting into the update now\r\n");

    /* --- 6. Reboot ------------------------------------------------------- */
    wm_sdk_task_sleep(200);   /* let the console drain before the reset */

    wm_sdk_ota_app_update_restart();

    /* Not reached. */
    wm_printf("restart did not take effect\r\n");
}

/*******************************************************************************
** Versions
******************************************************************************/
void wm_ui_ota_version_demo(void)
{
    char      app[WM_SDK_OTA_VERSION_MAX] = {0};
    char      sdk[WM_SDK_OTA_VERSION_MAX] = {0};
    wm_SdkResult rc;

    wm_printf("\r\n--- OTA: versions ---\r\n");

    rc = wm_sdk_ota_get_app_version(app, sizeof(app));
    wm_printf("app version: %s\r\n",
              (rc == WM_SDK_RESULT_SUCCESS) ? app : "<unavailable>");

    rc = wm_sdk_ota_get_sdk_version(sdk, sizeof(sdk));
    wm_printf("sdk version: %s\r\n",
              (rc == WM_SDK_RESULT_SUCCESS) ? sdk : "<unavailable>");

    /* MINI FOTA stages no local file, so only the APP image has a path. */
    wm_ota_report_staged((UINT32)WM_SDK_OTA_IMAGE_APP);
}

/*******************************************************************************
** OTA - application image
******************************************************************************/
void wm_ui_ota_update_demo(void)
{
    wm_printf("\r\n**** WM OTA: download + verify + apply APP image ****\r\n");
    wm_printf("the image must be the signed customer_app.bin, not a raw build\r\n");

    wm_ota_run_update();
}

/*******************************************************************************
** DFOTA - kernel patch, MINI FOTA (module-driven HTTP fetch)
******************************************************************************/
/* Kernel callback context, possibly a later boot - keep it short. */
static void wm_ui_dfota_status_cb(int status)
{
    if (status == 0)
        wm_printf("\r\n[DFOTA] MINI FOTA result: SUCCESS\r\n");
    else
        wm_printf("\r\n[DFOTA] MINI FOTA result: FAILED (status=%d)\r\n", status);
}

void wm_ui_dfota_init(void)
{
    (void)wm_sdk_ota_mini_dfota_init(wm_ui_dfota_status_cb);
}

void wm_ui_dfota_update_demo(void)
{
    SIM_MSG_T msg;
    int       rc;

    wm_printf("\r\n**** WM DFOTA: MINI FOTA kernel patch update ****\r\n");
    wm_printf("the patch is the adiff MINI output (system_patch.bin)\r\n");
    wm_printf("the module applies it itself and reboots - keep it powered; the "
              "result prints here later\r\n");

    if (!gf_pdp_ready)
        wm_printf("note: no PDP context is up, so this will fail\r\n");

    wm_printf("NOTE: uncheck \"send with \\r\\n\" in the SPT tool before sending "
              "the link\r\n\r\n");

    msg = wm_ota_prompt("Enter download link of the MINI FOTA patch: ");
    if (msg.arg3 == NULL)
    {
        wm_printf("\r\nno link entered\r\n");
        return;
    }
    wm_printf("\r\n%s\r\n", (char *)msg.arg3);

    rc = wm_sdk_ota_mini_dfota_start((const char *)msg.arg3);
    wm_sdk_memory_free(msg.arg3);

    if (rc == WM_MINI_FOTA_OK)
        wm_printf("MINI FOTA request ACCEPTED - running in the background\r\n");
    else
        wm_printf("MINI FOTA request FAILED -> rc=%d\r\n", rc);
}
