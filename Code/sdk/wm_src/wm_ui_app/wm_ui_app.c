/**
  ******************************************************************************
  * @file    wm_ui_app.c
  * @author  Walnut Medical
  * @brief   Common Gateway (WEGW) reference application.
  *
  *          Example application demonstrating the Common Gateway SDK (sdk_*)
  *          platform-abstraction API. A serial command menu runs one demo per
  *          API section.
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
#include <stdlib.h>
#include "wm_ui_app.h"
#include "wm_ui_mqtt.h"    /* MQTT demo: NMEA uplink + GPS config downlink   */
#include "wm_ui_tcp.h"     /* TCP demo: event-driven echo exchange           */
#include "wm_ui_https.h"   /* HTTPS demos: GET, POST, async GET, download    */
#include "wm_ui_ota.h"     /* OTA/DFOTA demos: version, download, verify, apply */

/*******************************************************************************
** Menu helper
******************************************************************************/
void PrintfOptionMenu(char *options_list[], int array_size)
{
    int i;
    wm_printf("\r\n===== WEGW Common Gateway - SDK Demo Menu =====\r\n");
    for (i = 0; i < array_size; i++)
        wm_printf("%s\r\n", options_list[i]);
    wm_printf("==============================================\r\n");
}

/*******************************************************************************
** OS / RTOS demo - exercises the sdk_os API set (WEGW_API_REQUIREMENTS_V0)
******************************************************************************/
#define WM_OS_DEMO_TASK_STACK   (1024 * 4)

static void *s_os_demo_mq = NULL;   /* worker -> menu handshake queue */

/* Demo worker task: reports that it started (over the message queue), then
 * loops until the menu task deletes it by handle. */
static void wm_os_demo_task(void *arg)
{
    UINT32 ready = 1;

    (void)arg;
    sdk_task_sleep(10);
    if (s_os_demo_mq != NULL)
        sdk_msgq_send(s_os_demo_mq, &ready, 100);
    while (1)
        sdk_task_sleep(50);
}

/*******************************************************************************
** URC demo - name lookup for events drained from the registered queue
******************************************************************************/
static const char *wm_urc_event_name(urcEvent_e ev)
{
    switch (ev)
    {
    case URC_PDP_ACTIVE:                return "PDP_ACTIVE";
    case URC_PDP_INACTIVE:              return "PDP_INACTIVE";
    case URC_NET_ACTIVE:               return "NET_ACTIVE";
    case URC_NET_DISCONNECTED:         return "NET_DISCONNECTED";
    case URC_SIM_INSERTED:             return "SIM_INSERTED";
    case URC_SIM_REMOVED:              return "SIM_REMOVED";
    case URC_SIM_READY:                return "SIM_READY";
    case URC_SIM_EJECTED_FOR_LONG_TIME:return "SIM_EJECTED_LONG";
    case URC_USB_PLUGGED:              return "USB_PLUGGED";
    case URC_USB_REMOVED:              return "USB_REMOVED";
    case URC_NO_PDP_FOR_LONG_TIME:     return "NO_PDP_LONG";
    case URC_RADIO_REFRESH:            return "RADIO_REFRESH";
    default:                           return "UNKNOWN";
    }
}

/*******************************************************************************
** URC monitor task - owns the registered URC queue and blocks on it, printing
** each event as it arrives. Keeping the blocking receive here (instead of in the
** menu dispatcher) leaves the menu responsive and shows URCs live rather than
** only when the URC option is re-run.
******************************************************************************/
#define WM_URC_MON_TASK_STACK   (1024 * 4)

static void *s_urc_q        = NULL;   /* registered queue; persists once created */
static void *s_urc_mon_task = NULL;   /* monitor task handle; created once       */

static void wm_urc_monitor_task(void *arg)
{
    UINT32 ev;

    (void)arg;
    while (1)
    {
        if (sdk_msgq_recv(s_urc_q, &ev, SC_SUSPEND) == SDK_RESULT_SUCCESS)
            wm_printf("[URC] %s (%lu)\r\n",
                      wm_urc_event_name((urcEvent_e)ev), (unsigned long)ev);
        else
            sdk_task_sleep(100);   /* never spin if the receive errors out */
    }
}

/*******************************************************************************
** GPS demo - the receiver is brought up at boot, so the menu only configures it
** and takes delivery of fixes. The fix callback runs on the driver's GNSS task,
** so short work can be done in it directly; anything heavy or blocking belongs
** in an application task.
******************************************************************************/
static BOOL s_gps_stream_on = FALSE;   /* TRUE while the fix callback is set  */
static BOOL s_gps_nmea_on   = FALSE;   /* TRUE while the raw NMEA tap is set  */

/* Integer-only rendering: wm_printf carries no floating-point formatting, so
 * degrees go out as micro-degrees, altitude as cm and speed/course as 0.1 units. */
static void wm_gps_print_navdata(const char *tag, const SdkGpsNavData *nav)
{
    wm_printf("%s fix=%u sats=%u lat=%ld lon=%ld (1e-6 deg)\r\n",
              tag, (unsigned)(nav->fix_valid ? 1u : 0u), (unsigned)nav->satellites,
              (long)(nav->latitude * 1000000.0), (long)(nav->longitude * 1000000.0));
    wm_printf("%s alt=%ld cm speed=%ld (0.1 km/h) course=%ld (0.1 deg)\r\n",
              tag, (long)(nav->altitude_m * 100.0f), (long)(nav->speed_kmh * 10.0f),
              (long)(nav->course_deg * 10.0f));
    wm_printf("%s utc=%u-%u-%u %u:%u:%u\r\n",
              tag, (unsigned)nav->utc.year, (unsigned)nav->utc.month,
              (unsigned)nav->utc.day, (unsigned)nav->utc.hour,
              (unsigned)nav->utc.minute, (unsigned)nav->utc.second);
}

/* One line per epoch, so the callback stays short even at the highest rate. */
static void wm_gps_demo_fix_cb(const SdkGpsNavData *nav)
{
    wm_printf("[GPS] fix=%u sats=%u lat=%ld lon=%ld (1e-6 deg) spd=%ld (0.1 km/h) %02u:%02u:%02u\r\n",
              (unsigned)(nav->fix_valid ? 1u : 0u), (unsigned)nav->satellites,
              (long)(nav->latitude * 1000000.0), (long)(nav->longitude * 1000000.0),
              (long)(nav->speed_kmh * 10.0f),
              (unsigned)nav->utc.hour, (unsigned)nav->utc.minute,
              (unsigned)nav->utc.second);
}

/* Every sentence as the receiver sent it, checksum not yet verified. */
static void wm_gps_demo_nmea_cb(const char *sentence, UINT16 len)
{
    (void)len;
    wm_printf("[NMEA] %s\r\n", sentence);
}

/*******************************************************************************
** SMS demo - the receive queue persists across menu invocations so asynchronous
** incoming messages are still captured between runs of the SMS options.
******************************************************************************/
static void *s_sms_q = NULL;   /* registered receive queue; created once */

/*******************************************************************************
** USB command dispatcher task
******************************************************************************/
void sTask_WM_UIProcesser(void *arg)
{
    SIM_MSG_T optionMsg = {0, 0, 0, NULL};
    UINT32    opt = 0;
    SC_STATUS status = SC_SUCCESS;

    char *options_list[] =
    {
        "1.  NETWORK",
        "2.  SIM",
        "3.  SMS: Configure",
        "4.  SMS: Storage status",
        "5.  SMS: Send demo message",
        "6.  SMS: Read",
        "7.  SMS: Delete (index)",
        "8.  SMS: Delete all",
        "9.  SMS: Poll + Drain queue",
        "10. GPS: Configure",
        "11. GPS: Read fix",
        "12. GPS: Stream fixes (toggle)",
        "13. GPS: Stream raw NMEA (toggle)",
        "14. GPS: Power off",
        "15. TCP",
        "16. HTTPS: GET (sync)",
        "17. HTTPS: POST (JSON + api key)",
        "18. HTTPS: GET (async, result on queue)",
        "19. HTTPS: Download file + verify SHA-256",
        "20. OTA: Show app + SDK version",
        "21. OTA: Download + verify + apply APP image",
        "22. DFOTA: MINI FOTA kernel patch update (async result)",
        "23. UART",
        "24. FILE SYSTEM",
        "25. STORAGE (NVM)",
        "26. OS / RTOS",
        "27. DEVICE",
        "28. GPIO",
        "29. ADC",
        "30. I2C",
        "31. URC",
        "32. SYSTEM",
        "33. LOG",
        "34. MQTT: Connect + stream raw NMEA (toggle)",
    };

    (void)arg;
    sAPI_TaskSleep(40); /* let the boot settle */

    PrintfOptionMenu(options_list, sizeof(options_list) / sizeof(options_list[0]));

    while (1)
    {
        status = sAPI_MsgQRecv(WM_UI_msgq, &optionMsg, SC_SUSPEND); /* wait for a command */
        if (status != SC_SUCCESS)
        {
            sAPI_Debug("sAPI_MsgQRecv failed in sTask_WM_UIProcesser");
            continue;
        }

        if (SRV_UART != optionMsg.msg_id)
        {
            sAPI_Debug("%s, msg_id error", __func__);
            if (optionMsg.arg3 != NULL)
                sAPI_Free(optionMsg.arg3);
            continue;
        }

        opt = (UINT32)atoi((char *)optionMsg.arg3);
        sAPI_Free(optionMsg.arg3);

        switch (opt)
        {
        /* ---------------------------------------------------------------- NETWORK */
        case WM_DEMO_NETWORK:
        {
            UINT32          cfun = 0, ctzu = 0;
            SdkNetRegStatus creg = SDK_NET_REG_UNKNOWN, cgreg = SDK_NET_REG_UNKNOWN;
            SdkNetAttStatus cgatt = SDK_NET_DETACHED;
            UINT8           pin = 1;
            SdkIpAddress    ip;
            SdkNetworkGpsRadioInfo radio;
            SdkNetworkTime  t;

            wm_printf("\r\n--- NETWORK ---\r\n");

            if (sdk_network_get_cfun(&cfun) == SDK_RESULT_SUCCESS)
                wm_printf("cfun=%lu\r\n", (unsigned long)cfun);
            if (sdk_network_get_creg(&creg) == SDK_RESULT_SUCCESS)
                wm_printf("creg=%d\r\n", (int)creg);
            if (sdk_network_get_cgreg(&cgreg) == SDK_RESULT_SUCCESS)
                wm_printf("cgreg=%d\r\n", (int)cgreg);
            if (sdk_network_get_cgatt(&cgatt) == SDK_RESULT_SUCCESS)
                wm_printf("cgatt=%d\r\n", (int)cgatt);
            wm_printf("net_status=%s\r\n",
                      (sdk_network_get_network_status() == SDK_RESULT_SUCCESS) ? "up" : "down");
            if (sdk_network_get_sim_pin_status(&pin) == SDK_RESULT_SUCCESS)
                wm_printf("pin_status=%u\r\n", (unsigned)pin);
            if (sdk_network_get_ip_address(1, &ip) == SDK_RESULT_SUCCESS)
                wm_printf("ip v4=%s v6=%s\r\n", ip.ipv4, ip.ipv6);
            else
                wm_printf("ip: not available\r\n");
            if (sdk_network_get_gps_radio_info(&radio) == SDK_RESULT_SUCCESS)
                wm_printf("radio csq=%d rsrp=%d rsrq=%d mcc=%d mnc=%d lac=%d cell=%d valid=%d\r\n",
                          radio.csq, radio.rsrp, radio.rsrq, radio.mcc, radio.mnc,
                          radio.lac, radio.cell_id, (int)radio.valid);
            if (sdk_network_get_time(&t) == SDK_RESULT_SUCCESS)
                wm_printf("time=%04u-%02u-%02u %02u:%02u:%02u tz=%d\r\n",
                          (unsigned)t.year, (unsigned)t.month, (unsigned)t.day,
                          (unsigned)t.hour, (unsigned)t.minute, (unsigned)t.second,
                          (int)t.tz_quarter_hours);
            if (sdk_network_get_ctzu(&ctzu) == SDK_RESULT_SUCCESS)
                wm_printf("ctzu=%lu\r\n", (unsigned long)ctzu);
            break;
        }

        /* -------------------------------------------------------------------- SIM */
        case WM_DEMO_SIM:
        {
            SdkSimStatus st = SDK_SIM_ERROR;
            UINT8        pin = 1;
            char         iccid[24] = {0};

            wm_printf("\r\n--- SIM ---\r\n");
            if (sdk_sim_get_status(&st) == SDK_RESULT_SUCCESS)
                wm_printf("status=%d\r\n", (int)st);
            if (sdk_sim_get_pin_status(&pin) == SDK_RESULT_SUCCESS)
                wm_printf("pin_status=%u\r\n", (unsigned)pin);
            if (sdk_sim_get_iccid(iccid, sizeof(iccid)) == SDK_RESULT_SUCCESS)
                wm_printf("iccid=%s\r\n", iccid);
            else
                wm_printf("iccid read failed\r\n");
            break;
        }

        /* ----------------------------------------------------- SMS: Configure */
        case WM_DEMO_SMS_CONFIG:
        {
            UINT32 pending = 0;

            wm_printf("\r\n--- SMS: Configure ---\r\n");

            /* Customer creates the receive queue here (once). */
            if (s_sms_q == NULL)
                s_sms_q = sdk_msgq_create("smsq", sizeof(SdkSmsMessage), 15u, 0);
            if (s_sms_q == NULL)
            {
                wm_printf("SMS queue create failed (needs %lu bytes: 15 x %u-byte msg)\r\n",
                          (unsigned long)(sizeof(SdkSmsMessage) * 15u),
                          (unsigned)sizeof(SdkSmsMessage));
                break;
            }

            /* Attach the queue as the incoming-SMS route (poll remembers it)
             * BEFORE bringing SMS up, so no arrival is missed once +CMTI is on. */
            sdk_sms_msgq_poll(s_sms_q, &pending);

            /* Bring up the SMS subsystem + register the incoming hook. */
            sdk_sms_init();

            /* Apply text mode, GSM charset, new-message indication. */
            sdk_sms_set_format(SDK_SMS_FORMAT_TEXT);
            sdk_sms_set_charset(SDK_SMS_CHARSET_GSM);
            //sdk_sms_set_new_msg_ind(2, 1, 0, 0, 0); // Demo use

            wm_printf("SMS configured (text/GSM), queue attached (pending=%lu)\r\n",
                      (unsigned long)pending);
            break;
        }

        /* ------------------------------------------------ SMS: Storage status */
        case WM_DEMO_SMS_STORAGE:
        {
            UINT32 used = 0, total = 0;
            char   store[12] = {0};

            wm_printf("\r\n--- SMS: Storage status ---\r\n");

            /* Occupancy of the active SMS storage (name + used/total slots). */
            if (sdk_sms_get_storage_status(store, sizeof(store), &used, &total) == SDK_RESULT_SUCCESS)
                wm_printf("storage %s: %lu/%lu used\r\n", store,
                          (unsigned long)used, (unsigned long)total);
            else
                wm_printf("storage SM: status unavailable\r\n");
            break;
        }

        /* --------------------------------------------------------- SMS: Send */
        case WM_DEMO_SMS_SEND:
        {
            const char *sms_dest_number = "9952929341";
            const char *sms_body        = "WEGW Common Gateway SMS demo";
            SdkResult   sr;

            wm_printf("\r\n--- SMS: Send ---\r\n");

            /* Blocks until the network accepts the message or the attempt fails. */
            sr = sdk_sms_send(sms_dest_number, sms_body);
            wm_printf("send \"%s\" to %s -> %s\r\n", sms_body, sms_dest_number,
                      (sr == SDK_RESULT_SUCCESS) ? "ok" : "fail");
            break;
        }

        /* --------------------------------------------------------- SMS: Read */
        case WM_DEMO_SMS_READ:
        {
            const UINT32 sms_read_index = 1u;   /* first stored message slot */

            wm_printf("\r\n--- SMS: Read ---\r\n");

            if (s_sms_q == NULL)
            {
                wm_printf("SMS queue not available (run Configure first)\r\n");
                break;
            }

            /* Read a stored message; the result (and text on success) is
             * delivered to the queue as an SdkSmsMessage. Drain it with the
             * "Poll + drain queue" menu option. */
            wm_printf("read index %lu -> %s\r\n", (unsigned long)sms_read_index,
                      (sdk_sms_read(SDK_SMS_STORAGE_SM, sms_read_index, s_sms_q) == SDK_RESULT_SUCCESS)
                          ? "ok" : "none/fail");
            wm_printf("(result delivered async; run \"Poll + drain queue\" to see it)\r\n");
            break;
        }

        /* ------------------------------------------------------- SMS: Delete */
        case WM_DEMO_SMS_DELETE:
        {
            const UINT32 sms_del_index = 1u;   /* first stored message slot */
            SdkResult    sr;

            wm_printf("\r\n--- SMS: Delete ---\r\n");

            if (s_sms_q == NULL)
            {
                wm_printf("SMS queue not available (run Configure first)\r\n");
                break;
            }

            /* Delete one stored message by index. The outcome is also posted to
             * the queue as an SdkSmsMessage (SDK_SMS_EVT_DELETE_RESULT) - drain
             * it with the "Poll + drain queue" menu option. */
            sr = sdk_sms_delete(sms_del_index, s_sms_q);
            wm_printf("delete index %lu -> %s\r\n", (unsigned long)sms_del_index,
                      (sr == SDK_RESULT_SUCCESS) ? "ok" : "none/fail");
            break;
        }

        /* --------------------------------------------------- SMS: Delete all */
        case WM_DEMO_SMS_DELETE_ALL:
        {
            UINT32    used = 0, total = 0;
            char      store[12] = {0};
            SdkResult sr;

            wm_printf("\r\n--- SMS: Delete all ---\r\n");

            if (s_sms_q == NULL)
            {
                wm_printf("SMS queue not available (run Configure first)\r\n");
                break;
            }

            /* Wipe the whole store; the result is posted to the queue with
             * index -1 (SDK_SMS_EVT_DELETE_RESULT). */
            sr = sdk_sms_delete_all(s_sms_q);
            wm_printf("delete all -> %s\r\n",
                      (sr == SDK_RESULT_SUCCESS) ? "ok" : "fail");

            /* Confirm the store is empty afterwards. */
            if (sdk_sms_get_storage_status(store, sizeof(store), &used, &total) == SDK_RESULT_SUCCESS)
                wm_printf("storage %s: %lu/%lu used\r\n", store,
                          (unsigned long)used, (unsigned long)total);
            break;
        }

        /* ---------------------------------------------- SMS: Poll + drain queue */
        case WM_DEMO_SMS_DRAIN:
        {
            SdkSmsMessage m;
            UINT32        pending = 0;
            int           drained = 0;

            wm_printf("\r\n--- SMS: Poll + drain queue ---\r\n");

            if (s_sms_q == NULL)
            {
                wm_printf("SMS queue not available (run Configure first)\r\n");
                break;
            }

            /* How many events are pending right now. */
            if (sdk_sms_msgq_poll(s_sms_q, &pending) == SDK_RESULT_SUCCESS)
                wm_printf("pending=%lu\r\n", (unsigned long)pending);

            /* Drain and print every queued event (read results + async incoming).
             * Each message's text is heap-owned - release it with
             * sdk_sms_msg_free() after use. */
            while (sdk_msgq_recv(s_sms_q, &m, 0) == SDK_RESULT_SUCCESS)
            {
                const char *kind = (m.type == SDK_SMS_EVT_INCOMING)    ? "INCOMING" :
                                   (m.type == SDK_SMS_EVT_READ_RESULT) ? "READ"     : "DELETE";
                wm_printf("[SMS %s] idx=%ld status=%d\r\n",
                          kind, (long)m.index, (int)m.status);
                if (m.text != NULL)
                    wm_printf("  text: %s\r\n", m.text);
                sdk_sms_msg_free(&m);
                drained++;
            }
            if (drained == 0)
                wm_printf("no SMS events pending (incoming arrive async; re-run to drain)\r\n");
            break;
        }

        /* ----------------------------------------------------------- GPS: Configure */
        case WM_DEMO_GPS_CONFIG:
        {
            const UINT32 gps_mode      = SDK_GPS_SYS_GPS | SDK_GPS_SYS_GLO | SDK_GPS_SYS_GAL;
            const UINT32 gps_start_hot = 0u;   /* 0=HOT, 1=WARM, 2=COLD          */
            const UINT32 gps_out_port  = 0u;   /* 0=serial port, 1=URC           */
            const UINT32 gps_rate_hz   = 1u;   /* only 1/5/10/20 Hz are accepted */
            UINT8        power         = 0;

            wm_printf("\r\n--- GPS: Configure ---\r\n");

            if (sdk_gps_get_power_status(&power) != SDK_RESULT_SUCCESS)
            {
                wm_printf("GPS not initialised (WM_GPS_SUPPORT off or board not validated?)\r\n");
                break;
            }

            /* One restart either way: powering on already resets the receiver. */
            if (power == 0u)
                wm_printf("power on -> rc=%ld\r\n", (long)sdk_gps_set_power_status(1));
            else
                wm_printf("hot start -> rc=%ld\r\n",
                          (long)sdk_gps_start_mode(gps_start_hot));
            sdk_task_sleep(500);   /* let the receiver finish rebooting */

            /* Configure after the restart: it comes back up on its saved
             * settings, so anything applied before would be discarded. */
            wm_printf("mode GPS|GLONASS|Galileo -> rc=%ld\r\n",
                      (long)sdk_gps_set_mode(gps_mode));
            /* Rate last: enabling output re-applies the receiver default rate. */
            wm_printf("nmea output -> rc=%ld\r\n",
                      (long)sdk_gps_enable_nmea_output(gps_out_port));
            wm_printf("nmea rate %lu Hz -> rc=%ld\r\n", (unsigned long)gps_rate_hz,
                      (long)sdk_gps_set_nmea_rate(gps_rate_hz));

            /* sdk_gps_set_gnss_info_period / _open_agps_service /
             * _set_ap_flash_hot_start are not features of this receiver
             * (SDK_RESULT_NOT_SUPPORTED), so the demo does not call them. */

            wm_printf("(run 'GPS: Stream fixes' to print every epoch)\r\n");
            break;
        }

        /* ------------------------------------------------------------ GPS: Read fix */
        case WM_DEMO_GPS_FIX:
        {
            SdkGpsNavData nav = {0};
            SdkResult     sr;
            UINT8         power = 0;

            wm_printf("\r\n--- GPS: Read fix ---\r\n");

            /* A fix survives a power off, so show the power state next to it. */
            if (sdk_gps_get_power_status(&power) == SDK_RESULT_SUCCESS)
                wm_printf("power=%u\r\n", (unsigned)power);

            sr = sdk_gps_get_navdata(&nav);
            if (sr == SDK_RESULT_SUCCESS)
                wm_gps_print_navdata("fix", &nav);
            else if (sr == SDK_RESULT_BUSY)
            {
                wm_printf("no valid fix yet (acquiring)\r\n");
                wm_gps_print_navdata("last", &nav);
            }
            else
                wm_printf("get_navdata failed rc=%ld\r\n", (long)sr);
            break;
        }

        /* -------------------------------------------------------- GPS: Stream fixes */
        case WM_DEMO_GPS_STREAM:
        {
            SdkResult sr;

            wm_printf("\r\n--- GPS: Stream fixes ---\r\n");

            /* Register the callback to start, clear it to stop. */
            sr = sdk_gps_set_fix_callback(s_gps_stream_on ? NULL : wm_gps_demo_fix_cb);
            if (sr != SDK_RESULT_SUCCESS)
            {
                wm_printf("GPS not initialised (WM_GPS_SUPPORT off or board not validated?)\r\n");
                break;
            }

            s_gps_stream_on = s_gps_stream_on ? FALSE : TRUE;
            wm_printf("streaming %s (re-run this option to %s)\r\n",
                      s_gps_stream_on ? "ON"   : "OFF",
                      s_gps_stream_on ? "stop" : "start");
            break;
        }

        /* ----------------------------------------------------- GPS: Stream raw NMEA */
        case WM_DEMO_GPS_NMEA:
        {
            SdkResult sr;

            wm_printf("\r\n--- GPS: Stream raw NMEA ---\r\n");

            /* Independent of the fix stream: both can run at the same time. */
            sr = sdk_gps_set_nmea_callback(s_gps_nmea_on ? NULL : wm_gps_demo_nmea_cb);
            if (sr != SDK_RESULT_SUCCESS)
            {
                wm_printf("GPS not initialised (WM_GPS_SUPPORT off or board not validated?)\r\n");
                break;
            }

            s_gps_nmea_on = s_gps_nmea_on ? FALSE : TRUE;
            wm_printf("raw NMEA %s (re-run this option to %s)\r\n",
                      s_gps_nmea_on ? "ON"   : "OFF",
                      s_gps_nmea_on ? "stop" : "start");
            break;
        }

        /* ----------------------------------------------------------- GPS: Power off */
        case WM_DEMO_GPS_POWER_OFF:
            wm_printf("\r\n--- GPS: Power off ---\r\n");
            wm_printf("fix callback cleared -> rc=%ld\r\n",
                      (long)sdk_gps_set_fix_callback(NULL));
            sdk_gps_set_nmea_callback(NULL);
            s_gps_stream_on = FALSE;
            s_gps_nmea_on   = FALSE;
            wm_printf("power off -> rc=%ld\r\n", (long)sdk_gps_set_power_status(0));

            /* 'GPS: Read fix' still reports the last fix while powered down. */
            wm_printf("(re-run 'GPS: Configure' to power the receiver back up)\r\n");
            break;

        /* -------------------------------------------------------------------- TCP */
        case WM_DEMO_TCP:
            wm_ui_tcp_demo();
            break;

        /* ------------------------------------------------------------------ HTTPS */
        case WM_DEMO_HTTPS:
            wm_ui_https_get_demo();
            break;

        case WM_DEMO_HTTPS_POST:
            wm_ui_https_post_demo();
            break;

        case WM_DEMO_HTTPS_ASYNC:
            wm_ui_https_async_demo();
            break;

        case WM_DEMO_HTTPS_DOWNLOAD:
            wm_ui_https_download_demo();
            break;

        /* ------------------------------------------------------------ OTA / DFOTA */
        case WM_DEMO_OTA_VERSION:
            wm_ui_ota_version_demo();
            break;

        /* Both prompt, so they run here on the dispatcher and hold the menu. */
        case WM_DEMO_OTA_UPDATE:
            wm_ui_ota_update_demo();
            break;

        case WM_DEMO_DFOTA_UPDATE:
            wm_ui_dfota_update_demo();
            break;

        /* ------------------------------------------------------------------- UART */
        case WM_DEMO_UART:
            /* demo not included in this build */
            break;

        /* ------------------------------------------------------------ FILE SYSTEM */
        case WM_DEMO_FILE:
        {
            const char *dir   = "C:/wegwdir";
            const char *path  = "C:/wegwdir/demo.txt";
            const char *path2 = "C:/wegwdir/demo_renamed.txt";
            const char *text  = "Hello WEGW file system!";
            void  *f;
            UINT32 n = 0, size = 0;
            char   buf[64] = {0};

            wm_printf("\r\n--- FILE SYSTEM ---\r\n");

            /* mkdir */
            wm_printf("mkdir %s -> %s\r\n", dir,
                      (sdk_file_mkdir(dir) == SDK_RESULT_SUCCESS) ? "ok" : "fail/exists");

            /* open (write) -> write -> close */
            f = sdk_file_open(path, "wb+");
            if (f == NULL)
            {
                wm_printf("open(w) failed\r\n");
                break;
            }
            if (sdk_file_write(f, text, (UINT32)strlen(text), &n) == SDK_RESULT_SUCCESS)
                wm_printf("wrote %lu bytes\r\n", (unsigned long)n);
            else
                wm_printf("write failed\r\n");
            sdk_file_close(f);

            /* exists */
            wm_printf("exists=%s\r\n",
                      (sdk_file_exists(path) == SDK_RESULT_SUCCESS) ? "yes" : "no");

            /* open (read) -> size -> seek(0) -> read -> close */
            f = sdk_file_open(path, "rb");
            if (f != NULL)
            {
                if (sdk_file_get_size(f, &size) == SDK_RESULT_SUCCESS)
                    wm_printf("size=%lu\r\n", (unsigned long)size);

                sdk_file_seek(f, 0, 0);   /* SEEK_SET */

                n = 0;
                if (sdk_file_read(f, buf, sizeof(buf) - 1, &n) == SDK_RESULT_SUCCESS)
                {
                    buf[n] = '\0';
                    wm_printf("read %lu bytes: %s\r\n", (unsigned long)n, buf);
                }
                else
                {
                    wm_printf("read failed\r\n");
                }
                sdk_file_close(f);
            }
            else
            {
                wm_printf("open(r) failed\r\n");
            }

            /* rename -> delete */
            wm_printf("rename -> %s\r\n",
                      (sdk_file_rename(path, path2) == SDK_RESULT_SUCCESS) ? "ok" : "fail");
            wm_printf("delete -> %s\r\n",
                      (sdk_file_delete(path2) == SDK_RESULT_SUCCESS) ? "ok" : "fail");
            break;
        }

        /* --------------------------------------------------------- STORAGE (NVM) */
        case WM_DEMO_STORAGE:
        {
            /* Credential write/read round-trip on the client-cert slot. The
             * original value is saved first and restored at the end so the
             * device's provisioned credential is left unchanged. */
            const SdkStorageCredential cred = SDK_STORAGE_CRED_CLIENT_CERT;
            const char *test = "WEGW-STORAGE-DEMO-CREDENTIAL";
            static char backup[SDK_STORAGE_CRED_MAX_SIZE];
            static char readback[SDK_STORAGE_CRED_MAX_SIZE];
            BOOL have_backup;

            wm_printf("\r\n--- STORAGE (credentials) ---\r\n");

            /* save the current value so it can be restored afterwards */
            memset(backup, 0, sizeof(backup));
            have_backup = (sdk_storage_cred_read(cred, backup, sizeof(backup)) == SDK_RESULT_SUCCESS);

            /* write a test value */
            wm_printf("write -> %s\r\n",
                      (sdk_storage_cred_write(cred, test) == SDK_RESULT_SUCCESS) ? "ok" : "fail");

            /* read it back and compare */
            memset(readback, 0, sizeof(readback));
            if (sdk_storage_cred_read(cred, readback, sizeof(readback)) == SDK_RESULT_SUCCESS)
                wm_printf("read  -> \"%s\" (%s)\r\n", readback,
                          (strcmp(readback, test) == 0) ? "match" : "mismatch");
            else
                wm_printf("read  -> fail\r\n");

            /* restore the original value */
            if (have_backup)
                wm_printf("restore -> %s\r\n",
                          (sdk_storage_cred_write(cred, backup) == SDK_RESULT_SUCCESS) ? "ok" : "fail");
            break;
        }

        /* --------------------------------------------------------------- OS / RTOS */
        case WM_DEMO_OS:
        {
            void  *task = NULL, *mtx = NULL, *mq, *mem;
            UINT32 v, msg = 0;
            UINT32 ram_total = 0, ram_free = 0;
            UINT32 st_size = 0, st_used = 0, st_peak = 0;
            INT64  flash_total = 0, flash_free = 0;
            UINT8  cpu = 0;

            wm_printf("\r\n--- OS / RTOS ---\r\n");

            /* Ticks ------------------------------------------------------- */
            wm_printf("ticks=%lu\r\n", (unsigned long)sdk_get_ticks());

            /* Heap memory: alloc / free ---------------------------------- */
            mem = sdk_memory_alloc(64);
            wm_printf("alloc(64)=%s\r\n", (mem != NULL) ? "ok" : "fail");
            if (mem != NULL)
                sdk_memory_free(mem);

            /* Mutex: create / lock / unlock / delete --------------------- */
            if (sdk_mutex_create(&mtx, 0) == SDK_RESULT_SUCCESS)
            {
                sdk_mutex_lock(mtx, 1000);
                sdk_mutex_unlock(mtx);
                sdk_mutex_delete(mtx);
                wm_printf("mutex create/lock/unlock/delete ok\r\n");
            }

            /* Message queue: create / send / recv / delete --------------- */
            mq = sdk_msgq_create("osdemo", sizeof(UINT32), 4, 0);
            if (mq != NULL)
            {
                v = 0xABCDu;
                sdk_msgq_send(mq, &v, 100);
                v = 0;
                wm_printf("msgq recv=%s\r\n",
                          (sdk_msgq_recv(mq, &v, 100) == SDK_RESULT_SUCCESS) ? "ok" : "timeout");
                sdk_msgq_delete(mq);
            }

            /* Task: create / stack info / delete ------------------------- */
            s_os_demo_mq = sdk_msgq_create("osdemoq", sizeof(UINT32), 2, 0);
            task = sdk_task_create(wm_os_demo_task, NULL, "OSDEMO", NULL,
                                   WM_OS_DEMO_TASK_STACK, TP_TIMED_ACTIVITY);
            if (task != NULL && s_os_demo_mq != NULL &&
                sdk_msgq_recv(s_os_demo_mq, &msg, 1000) == SDK_RESULT_SUCCESS)
            {
                wm_printf("worker task started\r\n");
                if (sdk_task_get_stack_info(task, &st_size, &st_used, &st_peak) == SDK_RESULT_SUCCESS)
                    wm_printf("worker stack size=%lu used=%lu peak=%lu\r\n",
                              (unsigned long)st_size, (unsigned long)st_used, (unsigned long)st_peak);
                sdk_task_sleep(20);
                wm_printf("worker delete=%s\r\n",
                          (sdk_task_delete(task) == SDK_RESULT_SUCCESS) ? "ok" : "fail");
            }
            if (s_os_demo_mq != NULL)
            {
                sdk_msgq_delete(s_os_demo_mq);
                s_os_demo_mq = NULL;
            }

            /* System statistics ------------------------------------------ */
            sdk_system_get_stats(&ram_total, &ram_free, &flash_total, &flash_free, &cpu);
            wm_printf("ram=%lu/%luKB flash=%ld/%ldKB cpu=%u%%\r\n",
                      (unsigned long)ram_free, (unsigned long)ram_total,
                      (long)flash_free, (long)flash_total, (unsigned)cpu);
            break;
        }

        /* ----------------------------------------------------------------- DEVICE */
        case WM_DEMO_DEVICE:
        {
            char imei[20] = {0};   /* IMEI is 15 digits + NUL */

            wm_printf("\r\n--- DEVICE ---\r\n");
            if (sdk_device_get_imei(imei, sizeof(imei)) == SDK_RESULT_SUCCESS)
                wm_printf("imei=%s\r\n", imei);
            else
                wm_printf("imei read failed\r\n");
            break;
        }

        /* ------------------------------------------------------------------- GPIO */
        case WM_DEMO_GPIO:
        {
            const UINT32 pins[2]     = { 69u, 70u };          /* LED output pins */
            const char  *names[2]    = { "BLUE", "RED" };     /* 69=blue, 70=red */
            const UINT32 blink_count = 3u;                    /* on/off cycles   */
            const UINT32 blink_ms    = 1000u;                 /* on/off gap (ms) */
            UINT32 level = 0;
            UINT32 i, cycle;

            wm_printf("\r\n--- GPIO (69=BLUE LED, 70=RED LED) ---\r\n");

            for (i = 0; i < 2u; i++)
            {
                if (sdk_gpio_set_direction(pins[i], 1) == SDK_RESULT_SUCCESS)
                    wm_printf("%s LED (pin %lu) -> output\r\n",
                              names[i], (unsigned long)pins[i]);
                else
                    wm_printf("%s LED (pin %lu) set output FAILED\r\n",
                              names[i], (unsigned long)pins[i]);
            }

            /* Blink each LED one by one with a gap between on/off so each LED
             * is visibly toggling in turn. */
            for (cycle = 0; cycle < blink_count; cycle++)
            {
                for (i = 0; i < 2u; i++)
                {
                    sdk_gpio_set_level(pins[i], 1);
                    wm_printf("%s LED ON  (%lu/%lu)\r\n",
                              names[i],
                              (unsigned long)(cycle + 1u), (unsigned long)blink_count);
                    sdk_task_sleep(blink_ms);

                    sdk_gpio_set_level(pins[i], 0);
                    wm_printf("%s LED OFF (%lu/%lu)\r\n",
                              names[i],
                              (unsigned long)(cycle + 1u), (unsigned long)blink_count);
                    sdk_task_sleep(blink_ms);
                }
            }

            for (i = 0; i < 2u; i++)
            {
                if (sdk_gpio_get_level(pins[i], &level) == SDK_RESULT_SUCCESS)
                    wm_printf("%s LED (pin %lu) level=%lu\r\n",
                              names[i], (unsigned long)pins[i], (unsigned long)level);
            }
            break;
        }

        /* -------------------------------------------------------------------- ADC */
        case WM_DEMO_ADC:
        {
            UINT16 vbat = 0, ch0 = 0, ch1 = 0;

            wm_printf("\r\n--- ADC ---\r\n");

            if (sdk_adc_read_vbat_voltage(&vbat) == SDK_RESULT_SUCCESS)
                wm_printf("vbat=%u mV\r\n", (unsigned)vbat);
            else
                wm_printf("vbat read failed\r\n");

            if (sdk_adc_read_voltage(0, &ch0) == SDK_RESULT_SUCCESS)
                wm_printf("adc0=%u mV\r\n", (unsigned)ch0);
            else
                wm_printf("adc0 read failed\r\n");

            if (sdk_adc_read_voltage(1, &ch1) == SDK_RESULT_SUCCESS)
                wm_printf("adc1=%u mV\r\n", (unsigned)ch1);
            else
                wm_printf("adc1 read failed\r\n");
            break;
        }

        /* -------------------------------------------------------------------- I2C */
        case WM_DEMO_I2C:
            /* I2C bus is handled internally by the SDK; no app-level demo. */
            break;

        /* -------------------------------------------------------------------- URC */
        case WM_DEMO_URC:
        {
            wm_printf("\r\n--- URC ---\r\n");

            if (s_urc_q == NULL)
            {
                s_urc_q = sdk_msgq_create("urcq", sizeof(UINT32), 8, 0);
                if (s_urc_q == NULL)
                {
                    wm_printf("URC queue create failed\r\n");
                    break;
                }
                sdk_urc_register(s_urc_q, 0xFFFFFFFFu);   /* all event types */
                wm_printf("URC queue registered (all events)\r\n");
            }

            /* Hand the queue to the monitor task: it does the blocking receive,
             * so this option returns straight away and the menu stays usable. */
            if (s_urc_mon_task == NULL)
            {
                s_urc_mon_task = sdk_task_create(wm_urc_monitor_task, NULL, "URCMON", NULL,
                                                 WM_URC_MON_TASK_STACK, TP_TIMED_ACTIVITY);
                wm_printf("URC monitor task %s\r\n",
                          (s_urc_mon_task != NULL) ? "started" : "create failed");
            }
            else
            {
                wm_printf("URC monitor task already running\r\n");
            }
            wm_printf("(events print as they arrive)\r\n");
            break;
        }

        /* ----------------------------------------------------------------- SYSTEM */
        case WM_DEMO_SYSTEM:
        {
            UINT32 reason;
            wm_printf("\r\n--- SYSTEM ---\r\n");
            reason = sdk_get_reset_reason();
            wm_printf("reset_reason=%lu (%s)\r\n",
                      (unsigned long)reason, sdk_get_reset_reason_string(reason));
            wm_printf("(reset/reboot/power_off not invoked in demo)\r\n");
            break;
        }

        /* -------------------------------------------------------------------- LOG */
        case WM_DEMO_LOG:
            wm_printf("\r\n--- LOG ---\r\n");
            sdk_log_info("info line %d", 1);
            sdk_log_warning("warning line");
            sdk_log_error("error line");
            sdk_debug_print("raw debug print\r\n");
            break;

        /* ------------------------------------------------------------------- MQTT */
        /* Implemented in wm_ui_mqtt.c - the MQTT demo owns a background
         * publisher task and a batching buffer, so it lives in its own file. */
        case WM_DEMO_MQTT:
            /* Takes over the raw-NMEA callback from 'GPS: Stream raw NMEA', so
             * keep that option's state honest. */
            s_gps_nmea_on = FALSE;
            wm_ui_mqtt_demo();
            break;

        default:
            wm_printf("Unknown option %lu\r\n", (unsigned long)opt);
            break;
        }
    }
}

/*******************************************************************************
** Customer application entry.
** Called from appimg_enter() after wm_system_init(). Customers extend their
** application logic from here.
******************************************************************************/
void WM_Entry_Task_Top_Most(void)
{
    wm_logger_mode(TRUE);   /* enable log output                     */
	sdk_gps_set_power_status(0);  /* GPS is off at startup          */
    wm_ui_app_init();       /* create WM_UI_msgq + UIPROC dispatcher */
    wm_ui_dfota_init();     /* MINI FOTA status callback - register at boot */
    RTI_LOG("WEGW Common Gateway app: WM_Entry_Task_Top_Most done");
}
