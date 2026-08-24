/**
  ******************************************************************************
  * @file    wm_ui_ota.c
  * @author  Walnut Medical
  * @brief   Common Gateway (WEGW) reference application - OTA / DFOTA demos.
  *
  *          Three menu handlers over the sdk_ota_* API:
  *
  *            versions  read the application and platform SDK version, and
  *                      report what is currently staged
  *            OTA       one option for a whole application update
  *            DFOTA     the same for a kernel delta patch
  *
  *          Each update option runs the complete sequence, prompting for the two
  *          things that cannot be compiled in:
  *
  *            prompt for the image URL
  *              -> sdk_ota_download_* : ranged HTTPS fetch into C: staging
  *            prompt for the expected SHA-256
  *              -> sdk_ota_get_image_hash + sdk_ota_verify_image
  *            -> sdk_ota_app_update / sdk_ota_dfota_update  (arm the bootloader)
  *            -> the matching _restart() to reboot into the update
  *
  *          Because the flow prompts, it runs on the "UIPROC" dispatcher - the
  *          task that owns the console - and the menu is unavailable until it
  *          finishes. A full image is hundreds of kilobytes fetched a range at a
  *          time, so expect the download step to take minutes.
  *
  *          Neither option will arm an image whose digest does not match. Nothing
  *          below this layer checks the image: the vendor's own validity call is
  *          declared but not linkable on this platform, so this SHA-256 step is
  *          the only thing standing between a corrupt download and a brick.
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
** below with sdk_storage_cred_read(SDK_STORAGE_CRED_ROOT_CA, ...) and point
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
    const char *path = sdk_ota_get_image_path(type);
    void       *f;
    UINT32      size = 0;

    if (sdk_file_exists(path) != SDK_RESULT_SUCCESS)
    {
        wm_printf("staged: none at %s\r\n", path);
        return;
    }

    f = sdk_file_open(path, "rb");
    if (f != NULL)
    {
        (void)sdk_file_get_size(f, &size);
        (void)sdk_file_close(f);
    }

    wm_printf("staged: %s (%lu bytes)\r\n", path, (unsigned long)size);
}

/* Free space on the staging volume in KB, or -1 if it cannot be determined. */
static INT64 wm_ota_free_kb(void)
{
    UINT32 ram_total = 0, ram_free = 0;
    INT64  flash_total = 0, flash_free = 0;
    UINT8  cpu = 0;

    if (sdk_system_get_stats(&ram_total, &ram_free, &flash_total, &flash_free, &cpu)
            != SDK_RESULT_SUCCESS)
        return -1;

    return flash_free;
}

/* Download @p url into the staging file for @p type. */
static BOOL wm_ota_download(UINT32 type, const char *url)
{
    UINT32    size = 0;
    INT64     free_kb;
    SdkResult rc;
    BOOL      ok = FALSE;

    wm_printf("-> %s\r\n", sdk_ota_get_image_path(type));

    if (!gf_pdp_ready)
        wm_printf("note: no PDP context is up, so this will fail\r\n");

    /* The download path is synchronous whichever mode is selected; picking sync
     * leaves the HTTPS client where the other demos expect it. */
    if (sdk_ota_download_init(SDK_HTTPS_MODE_SYNC, NULL) != SDK_RESULT_SUCCESS)
    {
        wm_printf("init failed\r\n");
        return FALSE;
    }

    if (sdk_ota_set_image_type(type) != SDK_RESULT_SUCCESS)
    {
        wm_printf("set_image_type failed\r\n");
        return FALSE;
    }

    wm_printf("tls: server verification %s\r\n", (s_ota_ca != NULL) ? "on" : "OFF");

    if (sdk_ota_download_configure_ssl(SDK_HTTPS_DOWNLOAD_INDEX, s_ota_ca)
            != SDK_RESULT_SUCCESS)
    {
        wm_printf("configure_ssl failed\r\n");
        return FALSE;
    }

    if (sdk_ota_download_set_params(SDK_HTTPS_DOWNLOAD_INDEX, url, WM_OTA_TIMEOUT)
            != SDK_RESULT_SUCCESS)
    {
        wm_printf("set_params failed (is the URL too long?)\r\n");
        return FALSE;
    }

    /* Size first, so a transfer is never started against a volume that cannot
     * hold the result. */
    if (sdk_ota_download_get_file_size(SDK_HTTPS_DOWNLOAD_INDEX, &size)
            != SDK_RESULT_SUCCESS || size == 0u)
    {
        wm_printf("size query failed - either the server does not support ranged "
                  "requests, or the trust anchor does not sign it\r\n");
        (void)sdk_ota_download_terminate(SDK_HTTPS_DOWNLOAD_INDEX);
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
        (void)sdk_ota_download_terminate(SDK_HTTPS_DOWNLOAD_INDEX);
        return FALSE;
    }

    wm_printf("downloading - this takes minutes for a full image\r\n");

    rc = sdk_ota_download_action(SDK_HTTPS_DOWNLOAD_INDEX);

    if (rc == SDK_RESULT_SUCCESS)
    {
        wm_printf("download SUCCESS\r\n");
        ok = TRUE;
    }
    else
    {
        wm_printf("download FAILED -> rc=%ld\r\n", (long)rc);
        wm_printf("any partial file left staged will not verify\r\n");
    }

    (void)sdk_ota_download_terminate(SDK_HTTPS_DOWNLOAD_INDEX);

    wm_ota_report_staged(type);
    return ok;
}

/* Hash the staged image, show the digest, and compare it with @p expected. */
static BOOL wm_ota_verify(UINT32 type, const char *expected)
{
    char      actual[SDK_OTA_SHA256_HEX_LEN + 1] = {0};
    SdkResult rc;

    if (sdk_ota_get_image_hash(type, actual, sizeof(actual)) != SDK_RESULT_SUCCESS)
    {
        wm_printf("could not hash %s\r\n", sdk_ota_get_image_path(type));
        return FALSE;
    }

    wm_printf("computed = %s\r\n", actual);
    wm_printf("expected = %s\r\n", expected);

    rc = sdk_ota_verify_image(type, expected);

    if (rc == SDK_RESULT_SUCCESS)
    {
        wm_printf("SHA256 MATCHES - image verified\r\n");
        return TRUE;
    }

    if (rc == SDK_RESULT_INVALID_PARAM)
        wm_printf("SHA256 not checked: the value entered is not 64 hex chars\r\n");
    else
        wm_printf("SHA256 DOES NOT MATCH\r\n");

    return FALSE;
}

/* The whole update flow, shared by both options: the only differences are which
 * image type is being handled and which pair of apply calls to make. */
static void wm_ota_run_update(UINT32 type, const char *label)
{
    SIM_MSG_T msg;
    char      expected[SDK_OTA_SHA256_HEX_LEN + 1] = {0};
    SdkResult rc;

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
        sdk_memory_free(msg.arg3);
        return;
    }
    sdk_memory_free(msg.arg3);

    /* --- 3. Expected digest ---------------------------------------------- */
    msg = wm_ota_prompt("Enter SHA256 of the image file: ");
    if (msg.arg3 == NULL)
    {
        wm_printf("\r\nno digest entered; the image is staged but not applied\r\n");
        return;
    }
    strncpy(expected, (const char *)msg.arg3, sizeof(expected) - 1);
    sdk_memory_free(msg.arg3);
    wm_printf("\r\n");

    /* --- 4. Verify ------------------------------------------------------- */
    if (!wm_ota_verify(type, expected))
    {
        wm_printf("refusing to apply an unverified %s image\r\n", label);
        return;
    }

    /* --- 5. Arm ---------------------------------------------------------- */
    rc = (type == (UINT32)SDK_OTA_IMAGE_KERNEL) ? sdk_ota_dfota_update()
                                                : sdk_ota_app_update();
    if (rc != SDK_RESULT_SUCCESS)
    {
        wm_printf("%s FAILED -> rc=%ld (nothing applied, no reboot)\r\n",
                  label, (long)rc);
        return;
    }

    wm_printf("%s SUCCESS - rebooting into the update now\r\n", label);

    /* --- 6. Reboot ------------------------------------------------------- */
    sdk_task_sleep(200);   /* let the console drain before the reset */

    if (type == (UINT32)SDK_OTA_IMAGE_KERNEL)
        sdk_ota_dfota_restart();
    else
        sdk_ota_app_update_restart();

    /* Not reached. */
    wm_printf("restart did not take effect\r\n");
}

/*******************************************************************************
** Versions
******************************************************************************/
void wm_ui_ota_version_demo(void)
{
    char      app[SDK_OTA_VERSION_MAX] = {0};
    char      sdk[SDK_OTA_VERSION_MAX] = {0};
    SdkResult rc;

    wm_printf("\r\n--- OTA: versions ---\r\n");

    rc = sdk_ota_get_app_version(app, sizeof(app));
    wm_printf("app version: %s\r\n",
              (rc == SDK_RESULT_SUCCESS) ? app : "<unavailable>");

    rc = sdk_ota_get_sdk_version(sdk, sizeof(sdk));
    wm_printf("sdk version: %s\r\n",
              (rc == SDK_RESULT_SUCCESS) ? sdk : "<unavailable>");

    wm_ota_report_staged((UINT32)SDK_OTA_IMAGE_APP);
    wm_ota_report_staged((UINT32)SDK_OTA_IMAGE_KERNEL);
}

/*******************************************************************************
** OTA - application image
******************************************************************************/
void wm_ui_ota_update_demo(void)
{
    wm_printf("\r\n**** WM OTA: download + verify + apply APP image ****\r\n");
    wm_printf("the image must be the signed customer_app.bin, not a raw build\r\n");

    wm_ota_run_update((UINT32)SDK_OTA_IMAGE_APP, "APP_OTA");
}

/*******************************************************************************
** DFOTA - kernel delta patch
******************************************************************************/
void wm_ui_dfota_update_demo(void)
{
    wm_printf("\r\n**** WM DFOTA: download + verify + apply kernel patch ****\r\n");
    wm_printf("the patch is the adiff output from wm_tools/wm_dfota_tool\r\n");

    wm_ota_run_update((UINT32)SDK_OTA_IMAGE_KERNEL, "DFOTA");
}
