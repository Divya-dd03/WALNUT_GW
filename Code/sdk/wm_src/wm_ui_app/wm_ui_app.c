/**
  ******************************************************************************
  * @file    wm_ui_app.c
  * @author  Walnut Medical
  * @brief   Common Gateway (WEGW) reference application.
  *
  *          Example application demonstrating the Common Gateway SDK (wm_sdk_*)
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
#include "wm_ui_ble.h"     /* BLE demos: scan, Autoguard health, fuel probes  */

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
** OS / RTOS demo - exercises the wm_sdk_os API set (WEGW_API_REQUIREMENTS_V0)
******************************************************************************/
#define WM_OS_DEMO_TASK_STACK   (1024 * 4)

static void *s_os_demo_mq = NULL;   /* worker -> menu handshake queue */

/* Demo worker task: reports that it started (over the message queue), then
 * loops until the menu task deletes it by handle. */
static void wm_os_demo_task(void *arg)
{
    UINT32 ready = 1;

    (void)arg;
    wm_sdk_task_sleep(10);
    if (s_os_demo_mq != NULL)
        wm_sdk_msgq_send(s_os_demo_mq, &ready, 100);
    while (1)
        wm_sdk_task_sleep(50);
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
        if (wm_sdk_msgq_recv(s_urc_q, &ev, SC_SUSPEND) == WM_SDK_RESULT_SUCCESS)
            wm_printf("[URC] %s (%lu)\r\n",
                      wm_urc_event_name((urcEvent_e)ev), (unsigned long)ev);
        else
            wm_sdk_task_sleep(100);   /* never spin if the receive errors out */
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
static BOOL s_gps_mode_narrow = FALSE; /* TRUE while narrowed to GPS+GLONASS  */

/* Integer-only rendering: wm_printf carries no floating-point formatting, so
 * degrees go out as micro-degrees, altitude as cm and speed/course as 0.1 units. */
static void wm_gps_print_navdata(const char *tag, const wm_SdkGpsNavData *nav)
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

/* Name what is enabled rather than echoing the raw mask. */
static void wm_gps_print_constellations(UINT32 sys)
{
    static const struct { UINT32 bit; const char *name; } SYS[] =
    {
        { WM_SDK_GPS_SYS_GPS,     "GPS"     },
        { WM_SDK_GPS_SYS_BDS,     "BDS"     },
#if (WM_SDK_GPS_CURRENT_CHIP == WM_SDK_GPS_CHIP_CC1161W)
        { WM_SDK_GPS_SYS_BDS_B1C, "BDS-B1C" },
#endif
        { WM_SDK_GPS_SYS_GLO,     "GLONASS" },
        { WM_SDK_GPS_SYS_GAL,     "Galileo" },
#if (WM_SDK_GPS_CURRENT_CHIP == WM_SDK_GPS_CHIP_CC1161W)
        { WM_SDK_GPS_SYS_QZSS,    "QZSS"    },
        { WM_SDK_GPS_SYS_SBAS,    "SBAS"    },
#endif
    };
    char     line[96];
    unsigned i;

    line[0] = '\0';
    for (i = 0; i < sizeof(SYS) / sizeof(SYS[0]); i++)
    {
        if (0 == (sys & SYS[i].bit))
            continue;
        if (line[0] != '\0')
            strcat(line, ", ");
        strcat(line, SYS[i].name);
    }

    /* The receiver supports signals this list does not name, so show the
     * leftovers rather than dropping them silently. */
    if (0 != (sys & ~(UINT32)WM_SDK_GPS_SYS_ALL))
        wm_printf("constellations: %s +0x%lX\r\n",
                  (line[0] != '\0') ? line : "none",
                  (unsigned long)(sys & ~(UINT32)WM_SDK_GPS_SYS_ALL));
    else
        wm_printf("constellations: %s\r\n", (line[0] != '\0') ? line : "none");
}

/* One line per epoch, so the callback stays short even at the highest rate. */
static void wm_gps_demo_fix_cb(const wm_SdkGpsNavData *nav)
{
    wm_printf("[GPS] fix=%u sats=%u lat=%ld lon=%ld (1e-6 deg) spd=%ld (0.1 km/h) %02u:%02u:%02u\r\n",
              (unsigned)(nav->fix_valid ? 1u : 0u), (unsigned)nav->satellites,
              (long)(nav->latitude * 1000000.0), (long)(nav->longitude * 1000000.0),
              (long)(nav->speed_kmh * 10.0f),
              (unsigned)nav->utc.hour, (unsigned)nav->utc.minute,
              (unsigned)nav->utc.second);
}

/* Every sentence as the receiver sent it, checksum not yet verified. Printed
 * bare - no tag - so a host NMEA parser can consume the port directly; this
 * matches what the MQTT path publishes (wm_ui_mqtt.c). */
static void wm_gps_demo_nmea_cb(const char *sentence, UINT16 len)
{
    (void)len;
    wm_printf("%s\r\n", sentence);
}

/*******************************************************************************
** SMS demo - the receive queue persists across menu invocations so asynchronous
** incoming messages are still captured between runs of the SMS options.
******************************************************************************/
static void *s_sms_q = NULL;   /* registered receive queue; created once */

/*******************************************************************************
** Relay demo - the outputs latch in hardware, so the menu keeps the state it
** last drove in order to report (and toggle) it.
******************************************************************************/
static BOOL s_relay_1_on = FALSE;
static BOOL s_relay_2_on = FALSE;

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
        "15. GPS: Cold start + AGNSS (TTFF test)",
        "16. GPS: Set constellations (GPS + GLONASS, toggle)",
        "17. TCP",
        "18. HTTPS: GET (sync)",
        "19. HTTPS: POST (JSON + api key)",
        "20. HTTPS: GET (async, result on queue)",
        "21. HTTPS: Download file + verify SHA-256",
        "22. OTA: Show app + SDK version",
        "23. OTA: Download + verify + apply APP image",
        "24. DFOTA: MINI FOTA kernel patch update (async result)",
        "25. UART",
        "26. FILE SYSTEM",
        "27. STORAGE (NVM)",
        "28. OS / RTOS",
        "29. DEVICE",
        "30. GPIO",
        "31. ADC",
        "32. I2C",
        "33. URC",
        "34. SYSTEM",
        "35. LOG",
        "36. MQTT: Connect + stream raw NMEA (toggle)",
        "37. LED: R/G/B indicator LEDs + blink",
        "38. LED: All ON",
        "39. LED: All OFF",
        "40. BLE: Scan for fuel probes (toggle)",
        "41. BLE: Periodic read (toggle)",
        "42. BLE: Power off",
        "43. RELAY 1: On/off (toggle)",
        "44. RELAY 2: On/off (toggle)",
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
            wm_SdkNetRegStatus creg = WM_SDK_NET_REG_UNKNOWN, cgreg = WM_SDK_NET_REG_UNKNOWN;
            wm_SdkNetAttStatus cgatt = WM_SDK_NET_DETACHED;
            UINT8           pin = 1;
            wm_SdkIpAddress    ip;
            wm_SdkNetworkGpsRadioInfo radio;
            wm_SdkNetworkTime  t;

            wm_printf("\r\n--- NETWORK ---\r\n");

            if (wm_sdk_network_get_cfun(&cfun) == WM_SDK_RESULT_SUCCESS)
                wm_printf("cfun=%lu\r\n", (unsigned long)cfun);
            if (wm_sdk_network_get_creg(&creg) == WM_SDK_RESULT_SUCCESS)
                wm_printf("creg=%d\r\n", (int)creg);
            if (wm_sdk_network_get_cgreg(&cgreg) == WM_SDK_RESULT_SUCCESS)
                wm_printf("cgreg=%d\r\n", (int)cgreg);
            if (wm_sdk_network_get_cgatt(&cgatt) == WM_SDK_RESULT_SUCCESS)
                wm_printf("cgatt=%d\r\n", (int)cgatt);
            wm_printf("net_status=%s\r\n",
                      (wm_sdk_network_get_network_status() == WM_SDK_RESULT_SUCCESS) ? "up" : "down");
            if (wm_sdk_network_get_sim_pin_status(&pin) == WM_SDK_RESULT_SUCCESS)
                wm_printf("pin_status=%u\r\n", (unsigned)pin);
            if (wm_sdk_network_get_ip_address(1, &ip) == WM_SDK_RESULT_SUCCESS)
                wm_printf("ip v4=%s v6=%s\r\n", ip.ipv4, ip.ipv6);
            else
                wm_printf("ip: not available\r\n");
            if (wm_sdk_network_get_gps_radio_info(&radio) == WM_SDK_RESULT_SUCCESS)
                wm_printf("radio csq=%d rsrp=%d rsrq=%d mcc=%d mnc=%d lac=%d cell=%d valid=%d\r\n",
                          radio.csq, radio.rsrp, radio.rsrq, radio.mcc, radio.mnc,
                          radio.lac, radio.cell_id, (int)radio.valid);
            if (wm_sdk_network_get_time(&t) == WM_SDK_RESULT_SUCCESS)
                wm_printf("time=%04u-%02u-%02u %02u:%02u:%02u tz=%d\r\n",
                          (unsigned)t.year, (unsigned)t.month, (unsigned)t.day,
                          (unsigned)t.hour, (unsigned)t.minute, (unsigned)t.second,
                          (int)t.tz_quarter_hours);
            if (wm_sdk_network_get_ctzu(&ctzu) == WM_SDK_RESULT_SUCCESS)
                wm_printf("ctzu=%lu\r\n", (unsigned long)ctzu);
            break;
        }

        /* -------------------------------------------------------------------- SIM */
        case WM_DEMO_SIM:
        {
            wm_SdkSimStatus st = WM_SDK_SIM_ERROR;
            UINT8        pin = 1;
            char         iccid[24] = {0};

            wm_printf("\r\n--- SIM ---\r\n");
            if (wm_sdk_sim_get_status(&st) == WM_SDK_RESULT_SUCCESS)
                wm_printf("status=%d\r\n", (int)st);
            if (wm_sdk_sim_get_pin_status(&pin) == WM_SDK_RESULT_SUCCESS)
                wm_printf("pin_status=%u\r\n", (unsigned)pin);
            if (wm_sdk_sim_get_iccid(iccid, sizeof(iccid)) == WM_SDK_RESULT_SUCCESS)
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
                s_sms_q = wm_sdk_msgq_create("smsq", sizeof(wm_SdkSmsMessage), 15u, 0);
            if (s_sms_q == NULL)
            {
                wm_printf("SMS queue create failed (needs %lu bytes: 15 x %u-byte msg)\r\n",
                          (unsigned long)(sizeof(wm_SdkSmsMessage) * 15u),
                          (unsigned)sizeof(wm_SdkSmsMessage));
                break;
            }

            /* Attach the queue as the incoming-SMS route (poll remembers it)
             * BEFORE bringing SMS up, so no arrival is missed once +CMTI is on. */
            wm_sdk_sms_msgq_poll(s_sms_q, &pending);

            /* Bring up the SMS subsystem + register the incoming hook. */
            wm_sdk_sms_init();

            /* Apply text mode, GSM charset, new-message indication. */
            wm_sdk_sms_set_format(WM_SDK_SMS_FORMAT_TEXT);
            wm_sdk_sms_set_charset(WM_SDK_SMS_CHARSET_GSM);
            //wm_sdk_sms_set_new_msg_ind(2, 1, 0, 0, 0); // Demo use

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
            if (wm_sdk_sms_get_storage_status(store, sizeof(store), &used, &total) == WM_SDK_RESULT_SUCCESS)
                wm_printf("storage %s: %lu/%lu used\r\n", store,
                          (unsigned long)used, (unsigned long)total);
            else
                wm_printf("storage SM: status unavailable\r\n");
            break;
        }

        /* --------------------------------------------------------- SMS: Send */
        case WM_DEMO_SMS_SEND:
        {
            const char *sms_dest_number = "7814304806";
            const char *sms_body        = "WEGW Common Gateway SMS demo";
            wm_SdkResult   sr;

            wm_printf("\r\n--- SMS: Send ---\r\n");

            /* Blocks until the network accepts the message or the attempt fails. */
            sr = wm_sdk_sms_send(sms_dest_number, sms_body);
            wm_printf("send \"%s\" to %s -> %s\r\n", sms_body, sms_dest_number,
                      (sr == WM_SDK_RESULT_SUCCESS) ? "ok" : "fail");
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
             * delivered to the queue as an wm_SdkSmsMessage. Drain it with the
             * "Poll + drain queue" menu option. */
            wm_printf("read index %lu -> %s\r\n", (unsigned long)sms_read_index,
                      (wm_sdk_sms_read(WM_SDK_SMS_STORAGE_SM, sms_read_index, s_sms_q) == WM_SDK_RESULT_SUCCESS)
                          ? "ok" : "none/fail");
            wm_printf("(result delivered async; run \"Poll + drain queue\" to see it)\r\n");
            break;
        }

        /* ------------------------------------------------------- SMS: Delete */
        case WM_DEMO_SMS_DELETE:
        {
            const UINT32 sms_del_index = 1u;   /* first stored message slot */
            wm_SdkResult    sr;

            wm_printf("\r\n--- SMS: Delete ---\r\n");

            if (s_sms_q == NULL)
            {
                wm_printf("SMS queue not available (run Configure first)\r\n");
                break;
            }

            /* Delete one stored message by index. The outcome is also posted to
             * the queue as an wm_SdkSmsMessage (WM_SDK_SMS_EVT_DELETE_RESULT) - drain
             * it with the "Poll + drain queue" menu option. */
            sr = wm_sdk_sms_delete(sms_del_index, s_sms_q);
            wm_printf("delete index %lu -> %s\r\n", (unsigned long)sms_del_index,
                      (sr == WM_SDK_RESULT_SUCCESS) ? "ok" : "none/fail");
            break;
        }

        /* --------------------------------------------------- SMS: Delete all */
        case WM_DEMO_SMS_DELETE_ALL:
        {
            UINT32    used = 0, total = 0;
            char      store[12] = {0};
            wm_SdkResult sr;

            wm_printf("\r\n--- SMS: Delete all ---\r\n");

            if (s_sms_q == NULL)
            {
                wm_printf("SMS queue not available (run Configure first)\r\n");
                break;
            }

            /* Wipe the whole store; the result is posted to the queue with
             * index -1 (WM_SDK_SMS_EVT_DELETE_RESULT). */
            sr = wm_sdk_sms_delete_all(s_sms_q);
            wm_printf("delete all -> %s\r\n",
                      (sr == WM_SDK_RESULT_SUCCESS) ? "ok" : "fail");

            /* Confirm the store is empty afterwards. */
            if (wm_sdk_sms_get_storage_status(store, sizeof(store), &used, &total) == WM_SDK_RESULT_SUCCESS)
                wm_printf("storage %s: %lu/%lu used\r\n", store,
                          (unsigned long)used, (unsigned long)total);
            break;
        }

        /* ---------------------------------------------- SMS: Poll + drain queue */
        case WM_DEMO_SMS_DRAIN:
        {
            wm_SdkSmsMessage m;
            UINT32        pending = 0;
            int           drained = 0;

            wm_printf("\r\n--- SMS: Poll + drain queue ---\r\n");

            if (s_sms_q == NULL)
            {
                wm_printf("SMS queue not available (run Configure first)\r\n");
                break;
            }

            /* How many events are pending right now. */
            if (wm_sdk_sms_msgq_poll(s_sms_q, &pending) == WM_SDK_RESULT_SUCCESS)
                wm_printf("pending=%lu\r\n", (unsigned long)pending);

            /* Drain and print every queued event (read results + async incoming).
             * Each message's text is heap-owned - release it with
             * wm_sdk_sms_msg_free() after use. */
            while (wm_sdk_msgq_recv(s_sms_q, &m, 0) == WM_SDK_RESULT_SUCCESS)
            {
                const char *kind = (m.type == WM_SDK_SMS_EVT_INCOMING)    ? "INCOMING" :
                                   (m.type == WM_SDK_SMS_EVT_READ_RESULT) ? "READ"     : "DELETE";
                wm_printf("[SMS %s] idx=%ld status=%d\r\n",
                          kind, (long)m.index, (int)m.status);
                if (m.text != NULL)
                    wm_printf("  text: %s\r\n", m.text);
                wm_sdk_sms_msg_free(&m);
                drained++;
            }
            if (drained == 0)
                wm_printf("no SMS events pending (incoming arrive async; re-run to drain)\r\n");
            break;
        }

        /* ----------------------------------------------------------- GPS: Configure */
        case WM_DEMO_GPS_CONFIG:
        {
            const UINT32 gps_start_hot = 0u;   /* 0=HOT, 1=WARM, 2=COLD          */
            UINT8        power         = 0;

            wm_printf("\r\n--- GPS: Configure ---\r\n");

            if (wm_sdk_gps_get_power_status(&power) != WM_SDK_RESULT_SUCCESS)
            {
                wm_printf("GPS not initialised (WM_GPS_SUPPORT off or board not validated?)\r\n");
                break;
            }

            /* One restart either way: powering on already resets the receiver. */
            if (power == 0u)
                wm_printf("power on -> rc=%ld\r\n", (long)wm_sdk_gps_set_power_status(1));
            else
                wm_printf("hot start -> rc=%ld\r\n",
                          (long)wm_sdk_gps_start_mode(gps_start_hot));

            /* Nothing else to send. The ROM defaults are already GGA+RMC at 1 Hz
             * on GPS|BDS|GLO|GAL|QZSS, so set_mode/enable_nmea_output/set_nmea_rate
             * would only re-assert them - and set_mode ($CFGSYS) resets the
             * receiver, throwing away the ephemeris it just started collecting
             * and turning every start into a cold one. Use wm_sdk_gps_set_mode()
             * once in production test if B1C/SBAS are wanted; it self-saves. */

            /* Read back what the receiver is actually running, which is not
             * necessarily the ROM default - set_mode persists inside it. */
            {
                UINT32 sys = 0;

                if (wm_sdk_gps_get_mode(&sys) == WM_SDK_RESULT_SUCCESS)
                    wm_gps_print_constellations(sys);
                else
                    wm_printf("constellations: no reply (receiver powered off?)\r\n");
            }

            wm_printf("(run 'GPS: Stream fixes' to print every epoch)\r\n");
            break;
        }

        /* ------------------------------------------------------------ GPS: Read fix */
        case WM_DEMO_GPS_FIX:
        {
            wm_SdkGpsNavData nav = {0};
            wm_SdkResult     sr;
            UINT8         power = 0;

            wm_printf("\r\n--- GPS: Read fix ---\r\n");

            /* A fix survives a power off, so show the power state next to it. */
            if (wm_sdk_gps_get_power_status(&power) == WM_SDK_RESULT_SUCCESS)
                wm_printf("power=%u\r\n", (unsigned)power);

            sr = wm_sdk_gps_get_navdata(&nav);
            if (sr == WM_SDK_RESULT_SUCCESS)
                wm_gps_print_navdata("fix", &nav);
            else if (sr == WM_SDK_RESULT_BUSY)
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
            wm_SdkResult sr;

            wm_printf("\r\n--- GPS: Stream fixes ---\r\n");

            /* Register the callback to start, clear it to stop. */
            sr = wm_sdk_gps_set_fix_callback(s_gps_stream_on ? NULL : wm_gps_demo_fix_cb);
            if (sr != WM_SDK_RESULT_SUCCESS)
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
            wm_SdkResult sr;

            wm_printf("\r\n--- GPS: Stream raw NMEA ---\r\n");

            /* Independent of the fix stream: both can run at the same time. */
            sr = wm_sdk_gps_set_nmea_callback(s_gps_nmea_on ? NULL : wm_gps_demo_nmea_cb);
            if (sr != WM_SDK_RESULT_SUCCESS)
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
                      (long)wm_sdk_gps_set_fix_callback(NULL));
            wm_sdk_gps_set_nmea_callback(NULL);
            s_gps_stream_on = FALSE;
            s_gps_nmea_on   = FALSE;
            wm_printf("power off -> rc=%ld\r\n", (long)wm_sdk_gps_set_power_status(0));

            /* 'GPS: Read fix' still reports the last fix while powered down. */
            wm_printf("(re-run 'GPS: Configure' to power the receiver back up)\r\n");
            break;

        /* ------------------------------------------ GPS: Cold start + AGNSS */
        case WM_DEMO_GPS_TTFF:
        {
            UINT8 power = 0;

            wm_printf("\r\n--- GPS: Cold start + AGNSS (TTFF test) ---\r\n");

            if (wm_sdk_gps_get_power_status(&power) != WM_SDK_RESULT_SUCCESS)
            {
                wm_printf("GPS not initialised\r\n");
                break;
            }

            if (power == 0u)
                wm_printf("power on -> rc=%ld\r\n",
                          (long)wm_sdk_gps_set_power_status(1));

            /* Cold drops ephemeris, almanac, position and time, so the next fix
             * is a real TTFF. This board has no GNSS reset line, so without it
             * the receiver never stops and there is nothing to measure. */
            wm_printf("cold start -> rc=%ld\r\n",
                      (long)wm_sdk_gps_start_mode(2u));
            wm_printf("AGNSS -> rc=%ld (forced; daily cap still applies)\r\n",
                      (long)wm_sdk_gps_open_agps_service());
            wm_printf("watch for 'wm_gps: TTFF <n> ms (cold, aided=yes|no)'\r\n");
            break;
        }

        /* -------------------------------------------- GPS: Set constellations */
        case WM_DEMO_GPS_MODE:
        {
            const UINT32 sys_gps_glo = WM_SDK_GPS_SYS_GPS | WM_SDK_GPS_SYS_GLO;
            const UINT32 sys_default = WM_SDK_GPS_SYS_GPS | WM_SDK_GPS_SYS_BDS |
                                       WM_SDK_GPS_SYS_GLO | WM_SDK_GPS_SYS_GAL |
                                       WM_SDK_GPS_SYS_QZSS;
            UINT32       want = s_gps_mode_narrow ? sys_default : sys_gps_glo;
            UINT32       sys  = 0;
            wm_SdkResult sr;

            wm_printf("\r\n--- GPS: Set constellations ---\r\n");

            /* $CFGSYS resets the receiver and saves to its own flash, so this
             * outlives a reboot and makes the next start a cold one. */
            sr = wm_sdk_gps_set_mode(want);
            wm_printf("set 0x%lX -> rc=%ld\r\n", (unsigned long)want, (long)sr);
            if (sr != WM_SDK_RESULT_SUCCESS)
            {
                wm_printf("(receiver powered off, or mode refused)\r\n");
                break;
            }
            s_gps_mode_narrow = s_gps_mode_narrow ? FALSE : TRUE;

            /* It is restarting; give it time before asking what it kept. */
            wm_sdk_task_sleep(1000);

            if (wm_sdk_gps_get_mode(&sys) == WM_SDK_RESULT_SUCCESS)
                wm_gps_print_constellations(sys);
            else
                wm_printf("read back: no reply (still restarting?)\r\n");

            wm_printf("re-run to %s\r\n", s_gps_mode_narrow
                      ? "restore the default set" : "narrow to GPS + GLONASS");
            break;
        }

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
                      (wm_sdk_file_mkdir(dir) == WM_SDK_RESULT_SUCCESS) ? "ok" : "fail/exists");

            /* open (write) -> write -> close */
            f = wm_sdk_file_open(path, "wb+");
            if (f == NULL)
            {
                wm_printf("open(w) failed\r\n");
                break;
            }
            if (wm_sdk_file_write(f, text, (UINT32)strlen(text), &n) == WM_SDK_RESULT_SUCCESS)
                wm_printf("wrote %lu bytes\r\n", (unsigned long)n);
            else
                wm_printf("write failed\r\n");
            wm_sdk_file_close(f);

            /* exists */
            wm_printf("exists=%s\r\n",
                      (wm_sdk_file_exists(path) == WM_SDK_RESULT_SUCCESS) ? "yes" : "no");

            /* open (read) -> size -> seek(0) -> read -> close */
            f = wm_sdk_file_open(path, "rb");
            if (f != NULL)
            {
                if (wm_sdk_file_get_size(f, &size) == WM_SDK_RESULT_SUCCESS)
                    wm_printf("size=%lu\r\n", (unsigned long)size);

                wm_sdk_file_seek(f, 0, 0);   /* SEEK_SET */

                n = 0;
                if (wm_sdk_file_read(f, buf, sizeof(buf) - 1, &n) == WM_SDK_RESULT_SUCCESS)
                {
                    buf[n] = '\0';
                    wm_printf("read %lu bytes: %s\r\n", (unsigned long)n, buf);
                }
                else
                {
                    wm_printf("read failed\r\n");
                }
                wm_sdk_file_close(f);
            }
            else
            {
                wm_printf("open(r) failed\r\n");
            }

            /* rename -> delete */
            wm_printf("rename -> %s\r\n",
                      (wm_sdk_file_rename(path, path2) == WM_SDK_RESULT_SUCCESS) ? "ok" : "fail");
            wm_printf("delete -> %s\r\n",
                      (wm_sdk_file_delete(path2) == WM_SDK_RESULT_SUCCESS) ? "ok" : "fail");

            /* disk figures. D:/ answers NOT_SUPPORTED unless the build carries
             * external flash - switch it with wm_extfs.bat. */
            {
                INT64        total = 0, freeb = 0, used = 0;
                wm_SdkResult rc;

                rc = wm_sdk_file_get_disk_info("C:/", &total, &freeb, &used);
                if (rc == WM_SDK_RESULT_SUCCESS)
                    wm_printf("C:/ total=%ld free=%ld used=%ld bytes\r\n",
                              (long)total, (long)freeb, (long)used);
                else
                    wm_printf("C:/ disk info -> rc=%ld\r\n", (long)rc);

                rc = wm_sdk_file_get_disk_info("D:/", &total, &freeb, &used);
                if (rc == WM_SDK_RESULT_SUCCESS)
                    wm_printf("D:/ total=%ld free=%ld used=%ld bytes\r\n",
                              (long)total, (long)freeb, (long)used);
                else
                    wm_printf("D:/ disk info -> rc=%ld\r\n", (long)rc);
            }

            /* list the root */
            {
                wm_SdkFileDirEntry list[12];
                UINT32 cnt = 0, i;

                if (wm_sdk_file_list_dir("C:/", list, (UINT32)(sizeof(list) / sizeof(list[0])),
                                         &cnt) == WM_SDK_RESULT_SUCCESS)
                {
                    wm_printf("dir C:/ -> %lu entries\r\n", (unsigned long)cnt);
                    for (i = 0; i < cnt; i++)
                        wm_printf("  [%s] %lu %s\r\n", list[i].name,
                                  (unsigned long)list[i].size,
                                  list[i].is_dir ? "dir" : "file");
                }
                else
                {
                    wm_printf("list dir failed\r\n");
                }
            }
            break;
        }

        /* --------------------------------------------------------- STORAGE (NVM) */
        case WM_DEMO_STORAGE:
        {
            /* Credential write/read round-trip on the client-cert slot. The
             * original value is saved first and restored at the end so the
             * device's provisioned credential is left unchanged. */
            const wm_SdkStorageCredential cred = WM_SDK_STORAGE_CRED_CLIENT_CERT;
            const char *test = "WEGW-STORAGE-DEMO-CREDENTIAL";
            static char backup[WM_SDK_STORAGE_CRED_MAX_SIZE];
            static char readback[WM_SDK_STORAGE_CRED_MAX_SIZE];
            BOOL have_backup;

            wm_printf("\r\n--- STORAGE (credentials) ---\r\n");

            /* save the current value so it can be restored afterwards */
            memset(backup, 0, sizeof(backup));
            have_backup = (wm_sdk_storage_cred_read(cred, backup, sizeof(backup)) == WM_SDK_RESULT_SUCCESS);

            /* write a test value */
            wm_printf("write -> %s\r\n",
                      (wm_sdk_storage_cred_write(cred, test) == WM_SDK_RESULT_SUCCESS) ? "ok" : "fail");

            /* read it back and compare */
            memset(readback, 0, sizeof(readback));
            if (wm_sdk_storage_cred_read(cred, readback, sizeof(readback)) == WM_SDK_RESULT_SUCCESS)
                wm_printf("read  -> \"%s\" (%s)\r\n", readback,
                          (strcmp(readback, test) == 0) ? "match" : "mismatch");
            else
                wm_printf("read  -> fail\r\n");

            /* restore the original value */
            if (have_backup)
                wm_printf("restore -> %s\r\n",
                          (wm_sdk_storage_cred_write(cred, backup) == WM_SDK_RESULT_SUCCESS) ? "ok" : "fail");
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
            wm_printf("ticks=%lu\r\n", (unsigned long)wm_sdk_get_ticks());

            /* Heap memory: alloc / free ---------------------------------- */
            mem = wm_sdk_memory_alloc(64);
            wm_printf("alloc(64)=%s\r\n", (mem != NULL) ? "ok" : "fail");
            if (mem != NULL)
                wm_sdk_memory_free(mem);

            /* Mutex: create / lock / unlock / delete --------------------- */
            if (wm_sdk_mutex_create(&mtx, 0) == WM_SDK_RESULT_SUCCESS)
            {
                wm_sdk_mutex_lock(mtx, 1000);
                wm_sdk_mutex_unlock(mtx);
                wm_sdk_mutex_delete(mtx);
                wm_printf("mutex create/lock/unlock/delete ok\r\n");
            }

            /* Message queue: create / send / recv / delete --------------- */
            mq = wm_sdk_msgq_create("osdemo", sizeof(UINT32), 4, 0);
            if (mq != NULL)
            {
                v = 0xABCDu;
                wm_sdk_msgq_send(mq, &v, 100);
                v = 0;
                wm_printf("msgq recv=%s\r\n",
                          (wm_sdk_msgq_recv(mq, &v, 100) == WM_SDK_RESULT_SUCCESS) ? "ok" : "timeout");
                wm_sdk_msgq_delete(mq);
            }

            /* Task: create / stack info / delete ------------------------- */
            s_os_demo_mq = wm_sdk_msgq_create("osdemoq", sizeof(UINT32), 2, 0);
            task = wm_sdk_task_create(wm_os_demo_task, NULL, "OSDEMO", NULL,
                                   WM_OS_DEMO_TASK_STACK, TP_TIMED_ACTIVITY);
            if (task != NULL && s_os_demo_mq != NULL &&
                wm_sdk_msgq_recv(s_os_demo_mq, &msg, 1000) == WM_SDK_RESULT_SUCCESS)
            {
                wm_printf("worker task started\r\n");
                if (wm_sdk_task_get_stack_info(task, &st_size, &st_used, &st_peak) == WM_SDK_RESULT_SUCCESS)
                    wm_printf("worker stack size=%lu used=%lu peak=%lu\r\n",
                              (unsigned long)st_size, (unsigned long)st_used, (unsigned long)st_peak);
                wm_sdk_task_sleep(20);
                wm_printf("worker delete=%s\r\n",
                          (wm_sdk_task_delete(task) == WM_SDK_RESULT_SUCCESS) ? "ok" : "fail");
            }
            if (s_os_demo_mq != NULL)
            {
                wm_sdk_msgq_delete(s_os_demo_mq);
                s_os_demo_mq = NULL;
            }

            /* System statistics ------------------------------------------ */
            wm_sdk_system_get_stats(&ram_total, &ram_free, &flash_total, &flash_free, &cpu);
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
            if (wm_sdk_device_get_imei(imei, sizeof(imei)) == WM_SDK_RESULT_SUCCESS)
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
                if (wm_sdk_gpio_set_direction(pins[i], 1) == WM_SDK_RESULT_SUCCESS)
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
                    wm_sdk_gpio_set_level(pins[i], 1);
                    wm_printf("%s LED ON  (%lu/%lu)\r\n",
                              names[i],
                              (unsigned long)(cycle + 1u), (unsigned long)blink_count);
                    wm_sdk_task_sleep(blink_ms);

                    wm_sdk_gpio_set_level(pins[i], 0);
                    wm_printf("%s LED OFF (%lu/%lu)\r\n",
                              names[i],
                              (unsigned long)(cycle + 1u), (unsigned long)blink_count);
                    wm_sdk_task_sleep(blink_ms);
                }
            }

            for (i = 0; i < 2u; i++)
            {
                if (wm_sdk_gpio_get_level(pins[i], &level) == WM_SDK_RESULT_SUCCESS)
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

            if (wm_sdk_adc_read_vbat_voltage(&vbat) == WM_SDK_RESULT_SUCCESS)
                wm_printf("vbat=%u mV\r\n", (unsigned)vbat);
            else
                wm_printf("vbat read failed\r\n");

            if (wm_sdk_adc_read_voltage(0, &ch0) == WM_SDK_RESULT_SUCCESS)
                wm_printf("adc0=%u mV\r\n", (unsigned)ch0);
            else
                wm_printf("adc0 read failed\r\n");

            if (wm_sdk_adc_read_voltage(1, &ch1) == WM_SDK_RESULT_SUCCESS)
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
                s_urc_q = wm_sdk_msgq_create("urcq", sizeof(UINT32), 8, 0);
                if (s_urc_q == NULL)
                {
                    wm_printf("URC queue create failed\r\n");
                    break;
                }
                wm_sdk_urc_register(s_urc_q, 0xFFFFFFFFu);   /* all event types */
                wm_printf("URC queue registered (all events)\r\n");
            }

            /* Hand the queue to the monitor task: it does the blocking receive,
             * so this option returns straight away and the menu stays usable. */
            if (s_urc_mon_task == NULL)
            {
                s_urc_mon_task = wm_sdk_task_create(wm_urc_monitor_task, NULL, "URCMON", NULL,
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
            reason = wm_sdk_get_reset_reason();
            wm_printf("reset_reason=%lu (%s)\r\n",
                      (unsigned long)reason, wm_sdk_get_reset_reason_string(reason));
            wm_printf("(reset/reboot/power_off not invoked in demo)\r\n");
            break;
        }

        /* -------------------------------------------------------------------- LOG */
        case WM_DEMO_LOG:
            wm_printf("\r\n--- LOG ---\r\n");
            wm_sdk_log_info("info line %d", 1);
            wm_sdk_log_warning("warning line");
            wm_sdk_log_error("error line");
            wm_sdk_debug_print("raw debug print\r\n");
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

        /* -------------------------------------------------------------------- LED */
        case WM_DEMO_LED:
        {
            /* The three indicator LEDs are independent, so a mask with more
             * than one bit simply lights that many LEDs together. */
            const UINT32 masks[4]  = { WM_SDK_LED_CH_RED,
                                       WM_SDK_LED_CH_GREEN,
                                       WM_SDK_LED_CH_BLUE,
                                       WM_SDK_LED_CH_ALL };
            const char  *names[4]  = { "RED", "GREEN", "BLUE", "ALL THREE" };
            UINT32 i;

            wm_printf("\r\n--- LED ---\r\n");
            wm_printf("(same LEDs as option 28 GPIO, driven via the SDK here)\r\n");

            /* Each LED on its own, then all three together. */
            for (i = 0; i < 4u; i++)
            {
                if (wm_sdk_led_set_state(masks[i], WM_SDK_LED_BLINK_NONE, 0u)
                        == WM_SDK_RESULT_SUCCESS)
                    wm_printf("%s on\r\n", names[i]);
                else
                    wm_printf("%s set FAILED\r\n", names[i]);
                wm_sdk_task_sleep(1000);
            }

            /* Blinking runs on a timer inside the platform layer, so the call
             * returns at once - wait out the cycles rather than driving them. */
            if (wm_sdk_led_set_state(WM_SDK_LED_CH_GREEN, WM_SDK_LED_BLINK_SLOW, 3u)
                    == WM_SDK_RESULT_SUCCESS)
                wm_printf("GREEN blink slow x3\r\n");
            else
                wm_printf("GREEN blink FAILED\r\n");
            wm_sdk_task_sleep(3500);

            if (wm_sdk_led_set_state(WM_SDK_LED_CH_RED, WM_SDK_LED_BLINK_FAST, 3u)
                    == WM_SDK_RESULT_SUCCESS)
                wm_printf("RED blink fast x3\r\n");
            else
                wm_printf("RED blink FAILED\r\n");
            wm_sdk_task_sleep(2000);

            /* Rejected requests (expect -3 WM_SDK_RESULT_INVALID_PARAM). */
            wm_printf("bad channel bit -> %ld\r\n",
                      (long)wm_sdk_led_set_state(0x08u, WM_SDK_LED_BLINK_NONE, 0u));
            wm_printf("count %lu -> %ld\r\n",
                      (unsigned long)(WM_SDK_LED_BLINK_COUNT_MAX + 1u),
                      (long)wm_sdk_led_set_state(WM_SDK_LED_CH_RED, WM_SDK_LED_BLINK_SLOW,
                                              (UINT8)(WM_SDK_LED_BLINK_COUNT_MAX + 1u)));

            /* Leave the LEDs dark so option 28 can drive the pins directly. */
            wm_sdk_led_set_state(WM_SDK_LED_CH_NONE, WM_SDK_LED_BLINK_NONE, 0u);
            wm_printf("all LEDs off\r\n");
            break;
        }

        /* ----------------------------------------------------------- LED: All ON */
        case WM_DEMO_LED_ALL_ON:
            wm_printf("\r\n--- LED: All ON ---\r\n");
            /* Steady light: the LEDs stay on until another set_state call. */
            wm_printf("all LEDs on -> %s\r\n",
                      (wm_sdk_led_set_state(WM_SDK_LED_CH_ALL, WM_SDK_LED_BLINK_NONE, 0u)
                           == WM_SDK_RESULT_SUCCESS) ? "ok" : "FAILED");
            break;

        /* ---------------------------------------------------------- LED: All OFF */
        case WM_DEMO_LED_ALL_OFF:
            wm_printf("\r\n--- LED: All OFF ---\r\n");
            /* An empty channel mask also cancels any blink still running. */
            wm_printf("all LEDs off -> %s\r\n",
                      (wm_sdk_led_set_state(WM_SDK_LED_CH_NONE, WM_SDK_LED_BLINK_NONE, 0u)
                           == WM_SDK_RESULT_SUCCESS) ? "ok" : "FAILED");
            break;

        /* -------------------------------------------------------------------- BLE */
        /* Implemented in wm_ui_ble.c - the BLE demos own a background task and
         * the peripheral wire formats, so they live in their own file. */
        case WM_DEMO_BLE_SCAN:
            wm_ui_ble_scan_demo();
            break;

        case WM_DEMO_BLE_MONITOR:
            wm_ui_ble_monitor_demo();
            break;

        case WM_DEMO_BLE_POWER_OFF:
            wm_ui_ble_power_off_demo();
            break;

        /* -------------------------------------------------------------- RELAY 1 */
        case WM_DEMO_RELAY_1:
            wm_printf("\r\n--- RELAY 1 ---\r\n");
            /* Active-high output driven straight from the platform library. It
             * holds its state until the next call, so re-run this option to
             * release the contacts. There is no read-back - listen for them. */
            s_relay_1_on = s_relay_1_on ? FALSE : TRUE;
            wm_RELAY_CTRL(WM_RELAY_1, s_relay_1_on);
            wm_printf("RELAY 1 %s (re-run this option to switch it %s)\r\n",
                      s_relay_1_on ? "ON"  : "OFF",
                      s_relay_1_on ? "off" : "on");
            break;

        /* -------------------------------------------------------------- RELAY 2 */
        case WM_DEMO_RELAY_2:
            wm_printf("\r\n--- RELAY 2 ---\r\n");
            /* Independent of relay 1: both can be closed at the same time. */
            s_relay_2_on = s_relay_2_on ? FALSE : TRUE;
            wm_RELAY_CTRL(WM_RELAY_2, s_relay_2_on);
            wm_printf("RELAY 2 %s (re-run this option to switch it %s)\r\n",
                      s_relay_2_on ? "ON"  : "OFF",
                      s_relay_2_on ? "off" : "on");
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
	wm_sdk_gps_set_power_status(0);  /* GPS is off at startup          */
    wm_sdk_ble_set_power_status(0);  /* BLE is off at startup - scanning holds
                                   * off sleep, so the demo powers it on   */
    wm_ui_app_init();       /* create WM_UI_msgq + UIPROC dispatcher */
    wm_ui_dfota_init();     /* MINI FOTA status callback - register at boot */
    RTI_LOG("WEGW Common Gateway app: WM_Entry_Task_Top_Most done");
}

void wm_ev_acc_state(BOOL ignition_on)
{
    wm_printf(ignition_on ? "\r\n ACC : IGNITION ON \r\n" : "\r\n ACC : IGNITION OFF \r\n");
}