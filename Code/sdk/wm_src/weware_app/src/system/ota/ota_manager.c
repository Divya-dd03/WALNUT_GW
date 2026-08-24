/**
 * @file ota_manager.c
 * @brief OTA (Over-The-Air) update manager implementation
 *
 * WALNUT port scope (2026-08-24): SIMCOM (modem-application) OTA only, up to
 * the file download. Structure and kept code are verbatim from the reference
 * (common-gateway-1.5). Trimmed against the reference, marked WALNUT below:
 *  - ST + peripheral device paths (file_transfer / peri_ota not ported);
 *  - the network-stability and motion prerequisite gates (network_manager /
 *    command_config not ported);
 *  - the post-download EVENT_RESET_SOFT broadcast - the downloaded image is
 *    written to the app package and left staged (download-only scope).
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>

/* SDK Platform Abstraction Layer */
#include "sdk_platform.h"
#include "functionality/sdk_functionality_os.h"
#include "functionality/sdk_functionality_file.h"
#include "functionality/sdk_functionality_ota.h"

#include "module/https/https_ops.h"
#include "common/utils.h"
#include "common/event_manager.h"
#include "system/storage/file_system.h"
#include "system/device_utils.h"
#include "config/config.h"
#include "system/storage/flash_paths.h"
#include "weware_version.h"
#include "system/ota/ota_manager.h"
#include "system/ota/ota_file_download.h"

/*---------------------------------------------------------------
 * Log Configuration
 *--------------------------------------------------------------*/
#define LOG_TAG "OTA"
#define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

/*---------------------------------------------------------------
 * Constants
 *--------------------------------------------------------------*/

#define OTA_CHECK_COOLDOWN_MS (12UL * 60 * 60 * 1000)
#define OTA_ARTIFACT_TYPE_BIN "BIN"

/* Artifact keys in API response */
#define OTA_ARTIFACT_KEY_SIMCOM "cgsc"
#define OTA_ARTIFACT_KEY_ST "cgst"

/*---------------------------------------------------------------
 * Internal State
 *--------------------------------------------------------------*/

typedef struct
{
    BOOL initialized;
    BOOL network_connected;
    BOOL gps_configured;
    UINT32 last_check_ticks;
    UINT32 st_ota_crc32;
    UINT32 st_firmware_file_size;  /* Store ST firmware file size for transfer progress */
    char imei[16];                 /* Cached IMEI — fetched once (never changes at runtime) */
    BOOL imei_valid;               /* TRUE once imei[] has been populated */
    BOOL peri_in_progress;         /* TRUE while a multi-tick peripheral OTA cycle is running */
    BOOL force_active;             /* TRUE for a one-shot forced cycle: bypasses prereq + cooldown gates */
    BOOL session_active;           /* TRUE for the whole OTA session (download+transfer+peripheral) — blocks re-entry */
    BOOL st_in_progress;           /* TRUE while the ST file transfer runs; peripheral waits until it finishes */
    UINT8 st_last_reason;          /* OtaReason: last STM OTA outcome (field readback)  */
    UINT8 sim_last_reason;         /* OtaReason: last SIMCOM OTA outcome (field readback) */
    UINT16 st_last_chunk;          /* chunk reached on last STM transfer outcome         */
    UINT16 st_last_total_chunks;   /* total chunks for last STM transfer                 */
    UINT8  st_last_pct;            /* percent transferred on last STM transfer outcome   */
} OtaState;

static OtaState g_ota_state = {0};

/* Resolved per-cycle device information, passed to every device handler.
 * imei points at the cached g_ota_state.imei; fw/hw are compile-time constants. */
typedef struct
{
    const char *imei;
    const char *firmware_version;
    const char *hardware_version;
} OtaContext;

/*
 * OTA work buffers — heap-allocated only while an OTA cycle is active, then freed.
 * Keeping them off the permanently-resident BSS saves ~1.5 KB of RAM when idle
 * (an OTA runs for minutes per month; the buffers needn't sit in RAM 24/7).
 * The peripheral OTA path (peri_ota_manager.c) already uses this same pattern.
 *
 *   g_ota_shared_buf — HTTP version-check response, then download chunk scratch
 *                      (used transiently within a single call; never across calls)
 *   g_ota_bin_url    — selected download URL; must persist from the version check
 *                      through the download, so it shares the cycle lifetime
 */
#define OTA_SHARED_BUF_SIZE 1024
#define OTA_BIN_URL_SIZE    512
static char *g_ota_shared_buf = NULL;
static char *g_ota_bin_url    = NULL;

/**
 * @brief Allocate the OTA work buffers if not already held (idempotent).
 * @return TRUE if both buffers are available, FALSE on allocation failure.
 */
static BOOL ota_buffers_acquire(void)
{
    if (!g_ota_shared_buf) g_ota_shared_buf = (char *)malloc(OTA_SHARED_BUF_SIZE);
    if (!g_ota_bin_url)    g_ota_bin_url    = (char *)malloc(OTA_BIN_URL_SIZE);
    if (!g_ota_shared_buf || !g_ota_bin_url)
    {
        LOG_ERROR("OTA: failed to allocate work buffers");
        free(g_ota_shared_buf); g_ota_shared_buf = NULL;
        free(g_ota_bin_url);    g_ota_bin_url    = NULL;
        return FALSE;
    }
    return TRUE;
}

/**
 * @brief Free the OTA work buffers. Safe to call when nothing is allocated.
 * @note Owned by the per-device process functions (and ota_manager_deinit);
 *       ota_check_update_required()/ota_download_and_write_firmware() only
 *       acquire-if-needed and never release, so the URL survives between them.
 */
static void ota_buffers_release(void)
{
    free(g_ota_shared_buf); g_ota_shared_buf = NULL;
    free(g_ota_bin_url);    g_ota_bin_url    = NULL;
}

/*---------------------------------------------------------------
 * Version Formatting Module
 *--------------------------------------------------------------*/

/**
 * @brief Remove leading zeros from version string
 */
static void ota_version_remove_leading_zeros(const char *version, char *output, size_t output_size)
{
    if (!version || !output || output_size == 0)
    {
        return;
    }

    const char *ptr = version;
    while (*ptr == '0' && *(ptr + 1) != '\0')
    {
        ptr++;
    }

    utils_strncpy_safe(output, ptr, output_size);
}

/**
 * @brief Format firmware version for API payload
 */
static void ota_version_format_firmware(const char *version, char *output, size_t output_size)
{
    if (!version || !output || output_size == 0)
    {
        return;
    }

    if (strchr(version, '.') != NULL)
    {
        utils_strncpy_safe(output, version, output_size);
        return;
    }

    char version_no_zero[16] = {0};
    ota_version_remove_leading_zeros(version, version_no_zero, sizeof(version_no_zero));

    if (strlen(version_no_zero) >= 2)
    {
        size_t remaining_len = strlen(&version_no_zero[1]);
        if (remaining_len > (output_size - 3))
        {
            remaining_len = output_size - 3;
        }
        snprintf(output, output_size, "%.1s.%.*s",
                 &version_no_zero[0], (int)remaining_len, &version_no_zero[1]);
    }
    else
    {
        utils_strncpy_safe(output, version_no_zero, output_size);
    }
}

/**
 * @brief Format hardware version for API payload
 */
static void ota_version_format_hardware(const char *version, char *output, size_t output_size)
{
    if (!version || !output || output_size == 0)
    {
        return;
    }

    char version_no_zero[16] = {0};
    ota_version_remove_leading_zeros(version, version_no_zero, sizeof(version_no_zero));

    if (strlen(version_no_zero) >= 2)
    {
        size_t remaining_len = strlen(&version_no_zero[1]);
        if (remaining_len > (output_size - 6))
        {
            remaining_len = output_size - 6;
        }
        snprintf(output, output_size, "CG_%.1s.%.*s",
                 &version_no_zero[0], (int)remaining_len, &version_no_zero[1]);
    }
    else
    {
        snprintf(output, output_size, "CG_%s", version_no_zero);
    }
}


/*---------------------------------------------------------------
 * ST Version Check Module
 *--------------------------------------------------------------*/

/**
 * @brief Format version for URL match tokens (e.g. 0106 -> 106, 0101 -> 101); dotted versions unchanged.
 */
static void ota_version_format_url_token(const char *version, char *output, size_t output_size)
{
    if (!version || !output || output_size == 0)
        return;
    if (strchr(version, '.') != NULL)
        utils_strncpy_safe(output, version, output_size);
    else
        ota_version_remove_leading_zeros(version, output, output_size);
}

/**
 * @brief Check if ST firmware update is needed by comparing URL with current HW/FW tokens.
 * @return 1 update required, 0 not required (URL matches current), -1 error
 */
static int ota_st_check_version_match(const char *download_url,
                                      const char *hardware_version,
                                      const char *firmware_version)
{
    if (!download_url)
    {
        SDK_DEBUG_PRINT("ota_st_check_version_match: Invalid URL");
        return -1;
    }

    if (strstr(download_url, OTA_ARTIFACT_KEY_ST) == NULL)
    {
        SDK_DEBUG_PRINT("URL does not contain '%s', proceeding with update", OTA_ARTIFACT_KEY_ST);
        return 1;
    }

    if (!hardware_version || !firmware_version || firmware_version[0] == '\0')
    {
        SDK_DEBUG_PRINT("ST version check: missing HW/FW, proceeding with update");
        return 1;
    }

    SDK_DEBUG_PRINT("ST URL detected, checking HW/FW tokens (hw=%s fw=%s)...",
                    hardware_version, firmware_version);

    char hw_token[16] = {0};
    char fw_token[32] = {0};
    ota_version_format_url_token(hardware_version, hw_token, sizeof(hw_token));
    ota_version_format_url_token(firmware_version, fw_token, sizeof(fw_token));

    char combo_pattern[96] = {0};
    int combo_len = snprintf(combo_pattern, sizeof(combo_pattern),
                             "%s_hwv%s_fwv%s", OTA_ARTIFACT_KEY_ST, hw_token, fw_token);
    if (combo_len < 0 || combo_len >= (int)sizeof(combo_pattern))
    {
        SDK_DEBUG_PRINT("ST combo version pattern buffer overflow");
        return -1;
    }

    if (strstr(download_url, combo_pattern) != NULL)
    {
        SDK_DEBUG_PRINT("URL contains '%s' - update not required", combo_pattern);
        return 0;
    }

    char version_pattern[64] = {0};
    int pattern_len = snprintf(version_pattern, sizeof(version_pattern), "_fwv%s", firmware_version);
    if (pattern_len < 0 || pattern_len >= (int)sizeof(version_pattern))
    {
        SDK_DEBUG_PRINT("ST _fwv pattern buffer overflow");
        return -1;
    }

    if (strstr(download_url, version_pattern) != NULL)
    {
        SDK_DEBUG_PRINT("URL contains '%s' - update not required", version_pattern);
        return 0;
    }

    SDK_DEBUG_PRINT("URL does not match current HW/FW tokens - update required");
    return 1;
}

/* WALNUT: ota_st_file_transfer_callback omitted - ST file transfer to the
 * STM (file_transfer_manager) is not ported. */

static void ota_handle_network_connected(const EventData *event, void *user_data)
{
    (void)user_data;
    (void)event;

    g_ota_state.network_connected = TRUE;
    LOG_INFO("OTA: Network connected event received");
}

static void ota_handle_network_disconnected(const EventData *event, void *user_data)
{
    (void)user_data;
    (void)event;

    g_ota_state.network_connected = FALSE;
    LOG_INFO("OTA: Network disconnected event received");
}

static void ota_handle_gps_configured(const EventData *event, void *user_data)
{
    (void)user_data;
    (void)event;

    g_ota_state.gps_configured = TRUE;
    LOG_INFO("OTA: GPS configured event received");
}

static void ota_handle_gps_disconnected(const EventData *event, void *user_data)
{
    (void)user_data;
    (void)event;

    //g_ota_state.gps_configured = FALSE;
    LOG_INFO("OTA: GPS disconnected event received");
}

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/

static void ota_purge_stale_fota_files(void)
{
    UINT32 deleted = 0U;
    UINT32 failed  = 0U;
    Result r       = file_system_delete_all_files_in_directory(FLASH_DIR_FOTA, &deleted, &failed);

    if (r == RESULT_NOT_SUPPORTED) {
        LOG_WARN("OTA: fota dir purge not supported on this platform");
        return;
    }
    if (r != RESULT_SUCCESS) {
        LOG_WARN("OTA: fota dir purge failed (deleted=%u failed=%u)",
                 (unsigned)deleted, (unsigned)failed);
        return;
    }
    if (deleted > 0U || failed > 0U)
        LOG_INFO("OTA: fota dir purge at init (deleted=%u failed=%u)",
                 (unsigned)deleted, (unsigned)failed);
    else
        LOG_DEBUG("OTA: fota dir empty at init");
}

Result ota_manager_init(void)
{
    LOG_DEBUG("OTA: ota_manager_init entry");
    if (g_ota_state.initialized)
    {
        LOG_DEBUG("OTA manager already initialized");
        return RESULT_ALREADY_INITIALIZED;
    }

    ota_purge_stale_fota_files();

    memset(&g_ota_state, 0, sizeof(g_ota_state));

    /* Register for network events */
    Result result = event_manager_register(EVENT_NETWORK_CONNECTED,
                                           ota_handle_network_connected,
                                           NULL,
                                           "ota_manager");
    if (result != RESULT_SUCCESS)
    {
        LOG_ERROR("Failed to register for EVENT_NETWORK_CONNECTED");
        return RESULT_ERROR;
    }

    result = event_manager_register(EVENT_NETWORK_DISCONNECTED,
                                    ota_handle_network_disconnected,
                                    NULL,
                                    "ota_manager");
    if (result != RESULT_SUCCESS)
    {
        LOG_ERROR("Failed to register for EVENT_NETWORK_DISCONNECTED");
        event_manager_unregister(EVENT_NETWORK_CONNECTED, ota_handle_network_connected);
        return RESULT_ERROR;
    }

    /* Register for GPS events */
    result = event_manager_register(EVENT_GPS_CONFIGURED,
                                     ota_handle_gps_configured,
                                     NULL,
                                     "ota_manager");
    if (result != RESULT_SUCCESS)
    {
        LOG_ERROR("Failed to register for EVENT_GPS_CONFIGURED");
        event_manager_unregister(EVENT_NETWORK_CONNECTED, ota_handle_network_connected);
        event_manager_unregister(EVENT_NETWORK_DISCONNECTED, ota_handle_network_disconnected);
        return RESULT_ERROR;
    }

    result = event_manager_register(EVENT_GPS_DISCONNECTED,
                                     ota_handle_gps_disconnected,
                                     NULL,
                                     "ota_manager");
    if (result != RESULT_SUCCESS)
    {
        LOG_ERROR("Failed to register for EVENT_GPS_DISCONNECTED");
        event_manager_unregister(EVENT_NETWORK_CONNECTED, ota_handle_network_connected);
        event_manager_unregister(EVENT_NETWORK_DISCONNECTED, ota_handle_network_disconnected);
        event_manager_unregister(EVENT_GPS_CONFIGURED, ota_handle_gps_configured);
        return RESULT_ERROR;
    }

    g_ota_state.initialized = TRUE;
    LOG_INFO("OTA manager initialized and registered for events");

    return RESULT_SUCCESS;
}

Result ota_manager_deinit(void)
{
    if (!g_ota_state.initialized)
    {
        return RESULT_SUCCESS;
    }

    event_manager_unregister(EVENT_NETWORK_CONNECTED, ota_handle_network_connected);
    event_manager_unregister(EVENT_NETWORK_DISCONNECTED, ota_handle_network_disconnected);
    event_manager_unregister(EVENT_GPS_CONFIGURED, ota_handle_gps_configured);
    event_manager_unregister(EVENT_GPS_DISCONNECTED, ota_handle_gps_disconnected);

    ota_buffers_release();
    memset(&g_ota_state, 0, sizeof(g_ota_state));

    LOG_INFO("OTA manager deinitialized");
    return RESULT_SUCCESS;
}

int ota_check_update_required(const char *firmware_version, const char *hardware_version,
                               const char *imei, int upgrade_type)
{
    LOG_INFO("OTA: check_update_required fw=%s hw=%s type=%d imei=%s",
              firmware_version ? firmware_version : "(null)",
              hardware_version ? hardware_version : "(null)", upgrade_type, imei);
    if (!firmware_version || !hardware_version || !imei)
    {
        LOG_DEBUG("OTA: check_update_required invalid parameters");
        return -1;
    }

    if (upgrade_type != OTA_UPGRADE_SIMCOM && upgrade_type != OTA_UPGRADE_ST)
    {
        LOG_DEBUG("OTA: check_update_required invalid upgrade type %d", upgrade_type);
        return -1;
    }

    /* Acquire the heap work buffers for this cycle (idempotent if already held). */
    if (!ota_buffers_acquire())
    {
        return -1;
    }

    /* Clear stored URL */
    memset(g_ota_bin_url, 0, OTA_BIN_URL_SIZE);
    g_ota_state.st_ota_crc32 = 0;

    /* Format versions for the API (dotted firmware, CG_ hardware prefix). */
    char fw_fmt[32] = {0};
    char hw_fmt[32] = {0};
    ota_version_format_firmware(firmware_version, fw_fmt, sizeof(fw_fmt));
    ota_version_format_hardware(hardware_version, hw_fmt, sizeof(hw_fmt));

    /* Unified version check (modem path: imei real, macId empty). */
    const char *expected_artifact_key =
        (upgrade_type == OTA_UPGRADE_SIMCOM) ? OTA_ARTIFACT_KEY_SIMCOM : OTA_ARTIFACT_KEY_ST;
    char download_url[512] = {0};
    UINT32 *crc32_ptr = (upgrade_type == OTA_UPGRADE_ST) ? &g_ota_state.st_ota_crc32 : NULL;

    int found = ota_http_check_version(fw_fmt, hw_fmt, imei, NULL,
                                       expected_artifact_key,
                                       g_ota_shared_buf, OTA_SHARED_BUF_SIZE,
                                       download_url, sizeof(download_url), crc32_ptr);
    if (found <= 0)
    {
        return found;   /* 0 = no update available, -1 = request/parse error */
    }

    LOG_DEBUG("OTA: download URL found for key %s", expected_artifact_key);

    /* For ST platform, check if update is actually needed */
    if (upgrade_type == OTA_UPGRADE_ST)
    {
        if (ota_st_check_version_match(download_url, hardware_version, firmware_version) == 0)
        {
            LOG_DEBUG("OTA: ST version matches URL, no update required");
            return 0;
        }
    }

    /* Store URL */
    if (strlen(download_url) >= OTA_BIN_URL_SIZE)
    {
        LOG_DEBUG("OTA: URL too long to store");
        return -1;
    }

    utils_strncpy_safe(g_ota_bin_url, download_url, OTA_BIN_URL_SIZE);
    LOG_DEBUG("OTA: update required, URL stored");
    return 1;
}


/*---------------------------------------------------------------
 * Main OTA Check Function
 *--------------------------------------------------------------*/

/**
 * @brief Check whether prerequisites are met to attempt an OTA right now.
 *
 * Re-evaluated before EVERY device dispatch so a mid-sequence change (e.g. the
 * GSM link dropping after the SIMCOM step but before the ST step) cleanly aborts
 * the rest of the cycle instead of pushing into a doomed retry loop.
 *
 * @return 1 if all prerequisites pass, 0 otherwise.
 * @note Does NOT initialise the manager — that is the orchestrator's job.
 */
static int ota_prerequisites_ok(void)
{
    /* WALNUT: reference also gates on file_transfer_get_state() (ST transfer
     * in progress), network_manager_is_stable_network() and
     * command_config_adoc_net_task_blocked() (motion gate) - those modules
     * are not ported yet. */

    /* Check network connection */
    if (g_ota_state.network_connected == FALSE)
    {
        LOG_DEBUG("Network not connected");
        return 0;
    }

    /* Check GPS configuration */
    if (g_ota_state.gps_configured == FALSE)
    {
        LOG_DEBUG("GPS not configured");
        return 0;
    }

    return 1;
}

/**
 * @brief Check if cooldown period has elapsed
 */
static int ota_check_cooldown(void)
{
    UINT32 current_ticks = SDK_GET_TICKS();

    if (g_ota_state.last_check_ticks != 0)
    {
        UINT32 elapsed_ms = utils_elapsed_ms_since(g_ota_state.last_check_ticks);
        if (elapsed_ms < OTA_CHECK_COOLDOWN_MS)
        {
            LOG_DEBUG("OTA check cooldown active: %u seconds remaining",
                      (OTA_CHECK_COOLDOWN_MS - elapsed_ms) / 1000);
            return 0;
        }
    }

    g_ota_state.last_check_ticks = current_ticks;
    return 1;
}

/**
 * @brief Process SIMCOM firmware update
 */
static int ota_process_simcom_update(const char *firmware_version, const char *hardware_version, const char *imei)
{
    LOG_DEBUG("OTA: process_simcom_update entry");
    int check_result = ota_check_update_required(firmware_version, hardware_version, imei, OTA_UPGRADE_SIMCOM);
    if (check_result <= 0)
    {
        g_ota_state.sim_last_reason = (check_result == 0) ? OTA_REASON_NO_UPDATE : OTA_REASON_VERSION_CHECK;
        LOG_DEBUG("OTA: SIMCOM check_result=%d, skip download", check_result);
        ota_buffers_release();
        return check_result;
    }

    LOG_DEBUG("OTA: SIMCOM update required, starting download");
    check_result = ota_download_and_write_firmware(OTA_UPGRADE_SIMCOM, g_ota_bin_url,
                                                   g_ota_shared_buf, OTA_SHARED_BUF_SIZE, NULL);
    if (check_result)
    {
        g_ota_state.sim_last_reason = OTA_REASON_OK;   /* image written; soft reset next */
        /* WALNUT: download-only scope - the reference broadcasts
         * EVENT_RESET_SOFT here to trigger the staged upgrade on reboot.
         * The image is downloaded and written to the app package; apply is
         * intentionally not wired yet. */
        LOG_INFO("OTA: SIMCOM download complete - image staged, apply not wired (download-only)");
    }
    else
    {
        g_ota_state.sim_last_reason = OTA_REASON_DOWNLOAD;
    }

    ota_buffers_release();
    return check_result;
}

/* WALNUT: ota_process_st_update omitted - ST path not ported. */

static BOOL ota_context_ensure(OtaContext *ctx)
{
    if (!ctx)
    {
        return FALSE;
    }

    /* IMEI never changes at runtime — fetch it once and reuse forever. */
    if (!g_ota_state.imei_valid)
    {
        char imei[16] = {0};
        if (!device_utils_get_imei(imei) || strlen(imei) == 0)
        {
            LOG_DEBUG("OTA: IMEI not available yet, deferring");
            return FALSE;
        }
        utils_strncpy_safe(g_ota_state.imei, imei, sizeof(g_ota_state.imei));
        g_ota_state.imei_valid = TRUE;
    }

    ctx->imei = g_ota_state.imei;
    ctx->firmware_version = FIRMWARE_VERSION;
    ctx->hardware_version = HARDWARE_VERSION;

    if (!ctx->firmware_version || !ctx->hardware_version)
    {
        LOG_ERROR("OTA: missing firmware/hardware version");
        return FALSE;
    }

    return TRUE;
}

/**
 * @brief Per-device handler: SIMCOM (this modem's own application firmware).
 */
static OtaResult ota_run_simcom(const OtaContext *ctx)
{
    int r = ota_process_simcom_update(ctx->firmware_version, ctx->hardware_version, ctx->imei);
    if (r > 0) return OTA_RESULT_UPDATED;     /* image written, soft reset incoming */
    if (r < 0) return OTA_RESULT_ERROR;
    return OTA_RESULT_NO_UPDATE;
}

/* WALNUT: ota_run_st / ota_run_peripheral omitted - not ported. */

/**
 * @brief Route a single OTA request to the correct device handler.
 *
 * Prerequisites are re-checked here on EVERY call, so any caller (periodic
 * driver or on-demand command) is guaranteed the link/GPS/transfer state is
 * still valid for THIS device before work begins.
 */
static OtaResult ota_dispatch(OtaDevice device, const OtaContext *ctx)
{
    if (!ctx)
    {
        return OTA_RESULT_ERROR;
    }

    if (!g_ota_state.force_active && !ota_prerequisites_ok())
    {
        LOG_DEBUG("OTA: prerequisites not met, skipping %s",
                  device == OTA_DEVICE_SIMCOM     ? "SIMCOM" :
                  device == OTA_DEVICE_ST         ? "ST" :
                  device == OTA_DEVICE_PERIPHERAL ? "PERIPHERAL" : "?");
        return OTA_RESULT_SKIPPED;
    }

    switch (device)
    {
        case OTA_DEVICE_SIMCOM:     return ota_run_simcom(ctx);
        /* WALNUT: ST and PERIPHERAL handlers not ported - report no-update
         * so a cycle walk continues/ends cleanly. */
        case OTA_DEVICE_ST:
        case OTA_DEVICE_PERIPHERAL:
            LOG_DEBUG("OTA: device %d not ported on walnut, skipping", (int)device);
            return OTA_RESULT_NO_UPDATE;
        default:
            LOG_ERROR("OTA: unknown device %d", (int)device);
            return OTA_RESULT_ERROR;
    }
}

int ota_manager_check_and_update(void)
{
    OtaContext ctx;
    LOG_DEBUG("OTA: check_and_update entry");

    if (!g_ota_state.initialized)
    {
        ota_manager_init();
    }

    if (!ota_context_ensure(&ctx))
    {
        return 0;   /* IMEI/version not ready — retry on a later tick */
    }

    /* WALNUT: reference branches (A) peripheral-cycle continuation and
     * (A2) ST-transfer wait are omitted - those device paths are not ported. */

    /* FORCED-OTA: a one-shot forced cycle bypasses the prerequisite (B) and cooldown
     * (C) gates below. The server version check still runs (it provides the download
     * URL), so force only skips the gates — it does not re-flash an identical build. */
    if (g_ota_state.force_active)
    {
        LOG_WARN("OTA: FORCED cycle — bypassing prerequisite + cooldown gates");
    }

    /* (B) Gate on prerequisites BEFORE the cooldown. ota_check_cooldown() arms the
     * 12-hour timer as soon as it passes, so if we let it run while the network/GPS
     * aren't ready yet, one early failed tick would lock OTA out for 12 hours.
     * Checking prerequisites first means the cooldown only arms when we can actually
     * proceed. (Prerequisites are re-checked per-device inside ota_dispatch() too.) */
    if (!g_ota_state.force_active && !ota_prerequisites_ok())
    {
        LOG_DEBUG("OTA: prerequisites not met, not starting cycle");
        return 0;
    }

    /* (C) Start a fresh cycle only when the 12-hour cooldown has elapsed. */
    if (!g_ota_state.force_active && !ota_check_cooldown())
    {
        return 0;
    }

    LOG_DEBUG("OTA: prerequisites + cooldown gates passed, starting cycle");

    /* Mark the session busy for its full duration (download + any ST transfer /
     * peripheral). Cleared at walk end (if nothing async started), in the peripheral
     * continuation, and in the ST transfer callback. This is what makes a second
     * FORCED-OTA arriving mid-download/mid-transfer get rejected instead of restarting. */
    g_ota_state.session_active = TRUE;

    /* (D) Walk devices in priority order. Prerequisites are re-checked inside
     * ota_dispatch() for each device, so if the link drops mid-cycle the
     * remaining devices are abandoned cleanly instead of forced into retries.
     * WALNUT: SIMCOM only - ST/peripheral are not ported. */
    static const OtaDevice k_order[] = {
        OTA_DEVICE_SIMCOM
    };

    for (size_t i = 0; i < sizeof(k_order) / sizeof(k_order[0]); i++)
    {
        OtaDevice dev = k_order[i];
        OtaResult r = ota_dispatch(dev, &ctx);

        if (r == OTA_RESULT_SKIPPED)
        {
            LOG_DEBUG("OTA: prerequisites lost mid-cycle, aborting remaining devices");
            break;
        }

        if (dev == OTA_DEVICE_SIMCOM && r == OTA_RESULT_UPDATED)
        {
            /* SIMCOM image written; download-only scope, so the session simply
             * ends here (reference: soft reset is on its way — stop here). */
            LOG_DEBUG("OTA: SIMCOM image downloaded and staged");
            g_ota_state.force_active   = FALSE;
            g_ota_state.session_active = FALSE;
            return 1;
        }
    }

    /* Reached the end with no async work pending (no ST transfer, no peripheral cycle —
     * those paths returned early above). The session — forced or normal — ends here. */
    g_ota_state.force_active   = FALSE;
    g_ota_state.session_active = FALSE;
    LOG_DEBUG("OTA: check_and_update cycle complete");
    return 0;
}

BOOL ota_manager_is_busy(void)
{
    /* A cycle is committed — spans the whole session: version-check, download, ST
     * file transfer, and peripheral. Set at cycle start, cleared only when the
     * session truly ends. This is the primary guard (covers the download window that
     * peri_in_progress / file-transfer state do not). */
    if (g_ota_state.session_active)
    {
        return TRUE;
    }
    /* An ST file transfer is running (peripheral deferred behind it). */
    if (g_ota_state.st_in_progress)
    {
        return TRUE;
    }
    /* A peripheral OTA cycle is mid-flight (multi-tick). */
    if (g_ota_state.peri_in_progress)
    {
        return TRUE;
    }
    /* WALNUT: reference also reports busy while the ST file transfer to the
     * STM is actively sending/awaiting (file_transfer_get_state) - not ported. */
    return FALSE;
}

Result ota_manager_force_update(void)
{
    if (!g_ota_state.initialized)
    {
        ota_manager_init();
    }

    /* Never restart an OTA on top of one already running. Unlike the normal cycle,
     * the forced path bypasses ota_prerequisites_ok() (which holds the "transfer in
     * progress" guard), so we must check it explicitly here. */
    if (ota_manager_is_busy())
    {
        LOG_WARN("OTA: FORCED-OTA rejected — an OTA is already in progress");
        return RESULT_BUSY;
    }

    /* Arm a one-shot forced cycle. The next periodic ota_manager_check_and_update()
     * tick (driven from the system manager loop) bypasses the prerequisite and
     * cooldown gates and walks SIMCOM -> ST -> peripheral. Clearing last_check_ticks
     * also satisfies the cooldown directly. The flag self-clears when the cycle ends. */
    g_ota_state.force_active    = TRUE;
    g_ota_state.last_check_ticks = 0;
    LOG_WARN("OTA: FORCED-OTA armed (gates + cooldown bypassed for one cycle)");
    return RESULT_SUCCESS;
}

/* WALNUT: ota_reason_str / ota_manager_get_status_string omitted - they
 * report ST-transfer progress (device_utils_get_st_firmware_version,
 * file_transfer_get_active_progress), which is not ported. */

OtaResult ota_manager_run_device(OtaDevice device)
{
    OtaContext ctx;

    if (!g_ota_state.initialized)
    {
        ota_manager_init();
    }

    if (!ota_context_ensure(&ctx))
    {
        return OTA_RESULT_ERROR;
    }

    OtaResult r = ota_dispatch(device, &ctx);

    /* Reuse the periodic driver's in-progress machinery: a command-triggered
     * peripheral OTA continues to completion on subsequent periodic ticks. No
     * new state and no second code path are introduced. */
    if (device == OTA_DEVICE_PERIPHERAL && r == OTA_RESULT_IN_PROGRESS)
    {
        g_ota_state.peri_in_progress = TRUE;
    }

    return r;
}
