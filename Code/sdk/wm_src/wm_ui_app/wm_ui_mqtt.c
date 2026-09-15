/**
  ******************************************************************************
  * @file    wm_ui_mqtt.c
  * @author  Walnut Medical
  * @brief   Common Gateway (WEGW) reference application - MQTT demo.
  *
  *          One demo: connect to AWS IoT Core over mutual TLS, stream the GNSS
  *          receiver's raw NMEA sentences up, and take GPS configuration
  *          commands back down. Both topics are IMEI-specific, so any number of
  *          units can share the one endpoint:
  *
  *              publish    wegw/<imei>/gps/nmea    raw NMEA, batched
  *              subscribe  wegw/<imei>/gps/cmd     GPS configuration commands
  *
  *          The uplink and the GNSS receiver run at very different speeds, which
  *          is what shapes the code below. Sentences arrive on the GNSS parser
  *          task ("GPSTASK"), which must never block - it is draining a UART -
  *          while a publish blocks for as long as the cellular link needs.
  *          Publishing per sentence would stall the parser and put 6-10 tiny
  *          messages per second on the air. So the GNSS callback only appends
  *          sentences to a batch buffer under a short-timeout mutex, and a
  *          dedicated "MQTTPUB" task publishes the accumulated batch once per
  *          interval. Sentences arriving while the buffer is full, or while the
  *          link is down, are counted and dropped: for live position telemetry,
  *          fresh data is worth more than complete data.
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
#include <stdio.h>
#include <stdlib.h>
#include "wm_ui_mqtt.h"
#include "wm_demo_certs.h"

/*******************************************************************************
** Broker configuration - retarget the demo by editing these.
**
** AWS IoT Core endpoint for the account the compiled-in certificates belong to
** (see wm_demo_certs.c). AWS IoT requires mutual TLS on 8883 and accepts only
** QoS 0 and 1, so the stream publishes at QoS 0 and the command topic is
** subscribed at QoS 1.
******************************************************************************/
#define WM_MQTT_HOST            "avj249ldc7joq-ats.iot.ap-south-1.amazonaws.com"
#define WM_MQTT_PORT            "8883"
#define WM_MQTT_KEEPALIVE_SEC   (60u)

/* Topic tree. The AWS IoT policy attached to the certificate must allow
 * publishing to the nmea topic and subscribing to the cmd topic. */
#define WM_MQTT_TOPIC_ROOT      "wegw"
#define WM_MQTT_TOPIC_PUB_LEAF  "gps/nmea"
#define WM_MQTT_TOPIC_SUB_LEAF  "gps/cmd"

/* Batching. The batch must fit one publish (WM_SDK_MQTT_PAYLOAD_MAX); 2 KB holds a
 * full 1 Hz epoch. A multi-constellation fix sends one GSV set per constellation
 * and a GSA per satellite in view, so an epoch runs well past the 6-10 sentences
 * a GPS-only receiver produced - overflow just increments the drop counter. */
#define WM_MQTT_NMEA_BATCH_MAX  (2048u)
#define WM_MQTT_NMEA_PUB_MS     (1000u)  /* default interval; 'interval=' cmd  */
#define WM_MQTT_NMEA_LOCK_MS    (10u)    /* GNSS-task lock wait; drop if busy  */
#define WM_MQTT_PUB_TASK_STACK  (1024 * 4)

/*******************************************************************************
** Demo state
******************************************************************************/
/* Built once from the IMEI, then held for the life of the client: wm_sdk_mqtt
 * keeps the topic string it is given rather than copying it. */
static char s_client_id[WM_SDK_MQTT_CLIENT_ID_MAX];
static char s_topic_nmea[WM_SDK_MQTT_TOPIC_MAX];
static char s_topic_cmd[WM_SDK_MQTT_TOPIC_MAX];
static BOOL s_ids_ready;

/* Batch filled on the GNSS parser task, drained by "MQTTPUB". */
static char   s_nmea_batch[WM_MQTT_NMEA_BATCH_MAX];
static char   s_nmea_pub[WM_MQTT_NMEA_BATCH_MAX];
static UINT32 s_nmea_len;
static void  *s_nmea_mtx;
static void  *s_nmea_task;
static BOOL   s_nmea_on;

/* Publish interval, adjustable at runtime by the 'interval=' command. */
static volatile UINT32 s_pub_ms = WM_MQTT_NMEA_PUB_MS;

/* Diagnostics only, so a lost update under contention costs a counter. */
static volatile UINT32 s_nmea_dropped;   /* buffer full, lock busy, or no link */

/*******************************************************************************
** Identity - client id and topics derived from the IMEI
******************************************************************************/
static void wm_mqtt_build_ids(void)
{
    char imei[20] = {0};   /* IMEI is 15 digits + NUL */

    if (s_ids_ready)
        return;

    /* Fall back to a fixed id so the demo still runs on a unit whose IMEI
     * cannot be read; on a real fleet that would collide, hence the warning. */
    if (wm_sdk_device_get_imei(imei, sizeof(imei)) != WM_SDK_RESULT_SUCCESS || imei[0] == '\0')
    {
        wm_printf("IMEI unavailable - using a fixed client id (do not ship this)\r\n");
        strcpy(imei, "000000000000000");
    }

    snprintf(s_client_id,  sizeof(s_client_id),  "wegw-%s", imei);
    snprintf(s_topic_nmea, sizeof(s_topic_nmea), "%s/%s/%s",
             WM_MQTT_TOPIC_ROOT, imei, WM_MQTT_TOPIC_PUB_LEAF);
    snprintf(s_topic_cmd,  sizeof(s_topic_cmd),  "%s/%s/%s",
             WM_MQTT_TOPIC_ROOT, imei, WM_MQTT_TOPIC_SUB_LEAF);
    s_ids_ready = TRUE;
}

/*******************************************************************************
** Raw NMEA -> MQTT (uplink)
******************************************************************************/
/* Runs on the GNSS parser task, once per sentence. Appends to the batch and
 * returns immediately: no allocation, no network, and only a bounded wait for
 * the lock (a busy lock drops the sentence rather than stalling the parser).
 *
 * The driver delivers sentences without their line terminator, so one is added
 * here to keep the published batch line-delimited. */
static void wm_mqtt_nmea_cb(const char *sentence, UINT16 len)
{
    UINT32 need = (UINT32)len + 2u;   /* sentence + CRLF */

    if (s_nmea_mtx == NULL || len == 0u)
        return;

    if (wm_sdk_mutex_lock(s_nmea_mtx, WM_MQTT_NMEA_LOCK_MS) != WM_SDK_RESULT_SUCCESS)
    {
        s_nmea_dropped++;
        return;
    }

    if (s_nmea_len + need <= sizeof(s_nmea_batch))
    {
        memcpy(&s_nmea_batch[s_nmea_len], sentence, len);
        s_nmea_len += len;
        s_nmea_batch[s_nmea_len++] = '\r';
        s_nmea_batch[s_nmea_len++] = '\n';
    }
    else
    {
        /* Publisher has not drained the batch in time - keep the sentences
         * already queued and drop this one. */
        s_nmea_dropped++;
    }

    wm_sdk_mutex_unlock(s_nmea_mtx);
}

/* Swaps the batch out under the lock, then publishes outside it, so the GNSS
 * callback is never blocked for the duration of a network write. */
static void wm_mqtt_nmea_pub_task(void *arg)
{
    (void)arg;

    while (1)
    {
        UINT32 len = 0;

        wm_sdk_task_sleep(s_pub_ms);

        if (!s_nmea_on)
            continue;

        if (wm_sdk_mutex_lock(s_nmea_mtx, 100) != WM_SDK_RESULT_SUCCESS)
            continue;
        len = s_nmea_len;
        if (len > 0u)
            memcpy(s_nmea_pub, s_nmea_batch, len);
        s_nmea_len = 0;
        wm_sdk_mutex_unlock(s_nmea_mtx);

        if (len == 0u)
            continue;

        /* Drop the batch rather than hold it: the next epoch is already on its
         * way and a stale position is worth less than a fresh one. */
        if (!wm_sdk_mqtt_is_connected())
        {
            s_nmea_dropped++;
            continue;
        }

        if (wm_sdk_mqtt_publish(s_topic_nmea, s_nmea_pub, len,
                             WM_SDK_MQTT_QOS0, FALSE) == WM_SDK_RESULT_SUCCESS)
            wm_printf("[MQTT] %lu bytes of NMEA -> %s\r\n",
                      (unsigned long)len, s_topic_nmea);
        else
            wm_printf("[MQTT] publish failed rc=%ld\r\n",
                      (long)wm_sdk_mqtt_last_error());
    }
}

void wm_ui_mqtt_nmea_stream_stop(void)
{
    if (!s_nmea_on)
        return;

    s_nmea_on = FALSE;
    if (wm_sdk_mutex_lock(s_nmea_mtx, 100) == WM_SDK_RESULT_SUCCESS)
    {
        s_nmea_len = 0;
        wm_sdk_mutex_unlock(s_nmea_mtx);
    }
}

BOOL wm_ui_mqtt_nmea_stream_active(void)
{
    return s_nmea_on;
}

/*******************************************************************************
** GPS configuration over MQTT (downlink)
**
** Commands are plain "key=value" text, one per message, so they can be sent
** straight from the AWS IoT console's test client. Each maps onto one wm_sdk_gps_*
** configuration call:
**
**     power=0|1        receiver power off / on
**     start=0|1|2      restart: 0 HOT, 1 WARM, 2 COLD
**     rate=1|2|4|5     NMEA output rate in Hz
**     mode=<mask>      constellations, OR of WM_SDK_GPS_SYS_* (1 GPS, 16 BDS,
**                      128 BDS B1C, 256 GLONASS, 4096 Galileo, 65536 QZSS,
**                      131072 SBAS); e.g. 4353 = GPS+GLO+GAL
**     output=0|1       NMEA output destination: 0 serial, 1 URC
**     stream=0|1       stop / start publishing to the nmea topic
**     interval=<ms>    publish interval, 250..60000 ms
******************************************************************************/
/* Match "key=" and parse the decimal value that follows. */
static BOOL wm_mqtt_arg_uint(const char *cmd, const char *key, UINT32 *out)
{
    UINT32 klen = (UINT32)strlen(key);

    if (strncmp(cmd, key, klen) != 0 || cmd[klen] != '=')
        return FALSE;
    if (cmd[klen + 1u] == '\0')
        return FALSE;

    *out = (UINT32)strtoul(&cmd[klen + 1u], NULL, 10);
    return TRUE;
}

/* Apply one command. Runs on the MQTT client's receive task. Each call writes to
 * the GNSS UART and waits for the receiver to acknowledge, so the handler blocks
 * for as long as that takes - normally milliseconds, but up to the driver's ACK
 * timeout if the receiver is unpowered or wedged. */
static void wm_mqtt_apply_cmd(const char *cmd)
{
    UINT32 v = 0;

    if (wm_mqtt_arg_uint(cmd, "power", &v))
    {
        wm_printf("[CMD] power=%lu -> rc=%ld\r\n",
                  (unsigned long)v, (long)wm_sdk_gps_set_power_status((UINT8)v));
    }
    else if (wm_mqtt_arg_uint(cmd, "start", &v))
    {
        wm_printf("[CMD] start=%lu -> rc=%ld\r\n",
                  (unsigned long)v, (long)wm_sdk_gps_start_mode(v));
    }
    else if (wm_mqtt_arg_uint(cmd, "rate", &v))
    {
        wm_printf("[CMD] rate=%lu -> rc=%ld\r\n",
                  (unsigned long)v, (long)wm_sdk_gps_set_nmea_rate(v));
    }
    else if (wm_mqtt_arg_uint(cmd, "mode", &v))
    {
        wm_printf("[CMD] mode=0x%lx -> rc=%ld\r\n",
                  (unsigned long)v, (long)wm_sdk_gps_set_mode(v));
    }
    else if (wm_mqtt_arg_uint(cmd, "output", &v))
    {
        wm_printf("[CMD] output=%lu -> rc=%ld\r\n",
                  (unsigned long)v, (long)wm_sdk_gps_enable_nmea_output(v));
    }
    else if (wm_mqtt_arg_uint(cmd, "interval", &v))
    {
        /* Bounded so a bad value cannot spin the publisher or stall it for
         * minutes. */
        if (v < 250u || v > 60000u)
        {
            wm_printf("[CMD] interval=%lu rejected (250..60000 ms)\r\n", (unsigned long)v);
        }
        else
        {
            s_pub_ms = v;
            wm_printf("[CMD] interval=%lu ms\r\n", (unsigned long)v);
        }
    }
    else if (wm_mqtt_arg_uint(cmd, "stream", &v))
    {
        if (v == 0u)
        {
            wm_sdk_gps_set_nmea_callback(NULL);
            wm_ui_mqtt_nmea_stream_stop();
            wm_printf("[CMD] stream=0 (stopped)\r\n");
        }
        else if (!s_nmea_on)
        {
            /* The batch mutex and publisher task already exist by the time any
             * command can arrive, because the menu option created them before
             * subscribing. */
            if (wm_sdk_gps_set_nmea_callback(wm_mqtt_nmea_cb) == WM_SDK_RESULT_SUCCESS)
            {
                s_nmea_on = TRUE;
                wm_printf("[CMD] stream=1 (started)\r\n");
            }
            else
            {
                wm_printf("[CMD] stream=1 failed (GPS not initialised)\r\n");
            }
        }
        else
        {
            wm_printf("[CMD] stream=1 (already running)\r\n");
        }
    }
    else
    {
        wm_printf("[CMD] unknown \"%s\"\r\n", cmd);
        wm_printf("      power= start= rate= mode= output= stream= interval=\r\n");
    }
}

/* Runs on the MQTT client's receive task. The payload is not NUL-terminated and
 * is wiped as soon as this returns, so it is copied out first. */
static void wm_mqtt_cmd_cb(const wm_SdkMqttMessage *msg)
{
    char   cmd[64];
    UINT32 n;

    n = (msg->payload_len < sizeof(cmd) - 1u) ? msg->payload_len : sizeof(cmd) - 1u;
    memcpy(cmd, msg->payload, n);
    cmd[n] = '\0';

    /* Tolerate the trailing whitespace a console or shell publisher adds. */
    while (n > 0u && (cmd[n - 1u] == '\r' || cmd[n - 1u] == '\n' ||
                      cmd[n - 1u] == ' '  || cmd[n - 1u] == '\t'))
        cmd[--n] = '\0';

    if (n == 0u)
        return;

    wm_printf("[MQTT RX] %s: %s\r\n", msg->topic, cmd);
    wm_mqtt_apply_cmd(cmd);
}

/*******************************************************************************
** Connect
******************************************************************************/
static BOOL wm_mqtt_connect(void)
{
    wm_SdkMqttConfig cfg;
    wm_SdkIpAddress  ip;
    wm_SdkResult     sr;
    INT32         raw;

    if (wm_sdk_mqtt_is_connected())
        return TRUE;

    /* MQTT rides on the TCP/IP stack, so a PDP context has to be up first.
     * Report it here rather than letting the connect fail obscurely. */
    if (wm_sdk_network_get_network_status() != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("network is down - wait for PDP (see the NETWORK option)\r\n");
        return FALSE;
    }
    if (wm_sdk_network_get_ip_address(1, &ip) == WM_SDK_RESULT_SUCCESS)
        wm_printf("bearer up, ip=%s\r\n", ip.ipv4);

    sr = wm_sdk_mqtt_init();
    if (sr != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("init failed rc=%ld (client buffers need a few KB of heap)\r\n", (long)sr);
        return FALSE;
    }

    /* Mutual TLS with the compiled-in AWS IoT credentials. Supplying ca_cert is
     * what selects TLS; the cert/key pair is what AWS IoT authenticates against.
     * All three are static, as wm_sdk_mqtt keeps the pointers while connected. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.host           = WM_MQTT_HOST;
    cfg.port           = WM_MQTT_PORT;
    cfg.client_id      = s_client_id;
    cfg.keep_alive_sec = WM_MQTT_KEEPALIVE_SEC;
    cfg.clean_session  = TRUE;
    cfg.ca_cert        = wm_cacert;
    cfg.client_cert    = wm_clientcert;
    cfg.client_key     = wm_clientkey;

    sr = wm_sdk_mqtt_config(&cfg);
    if (sr != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("config failed rc=%ld\r\n", (long)sr);
        return FALSE;
    }
    wm_printf("broker %s:%s\r\n", WM_MQTT_HOST, WM_MQTT_PORT);
    wm_printf("client \"%s\", certs %s (mutual TLS)\r\n", s_client_id, wm_demo_certs_id);

    /* Blocks for a few seconds: TCP, TLS handshake, then MQTT CONNECT. */
    sr = wm_sdk_mqtt_connect();
    if (sr != WM_SDK_RESULT_SUCCESS)
    {
        raw = wm_sdk_mqtt_last_error();
        wm_printf("connect failed rc=%ld raw=%ld\r\n", (long)sr, (long)raw);
        /* 1..5 is the broker's CONNACK refusal rather than a local error. AWS
         * IoT usually drops the TLS session instead, so a handshake or cert
         * problem shows up as a negative code here. */
        if (raw > 0)
            wm_printf("broker refused the session (CONNACK code %ld)\r\n", (long)raw);
        else
            wm_printf("check the certificates, the endpoint and the IoT policy\r\n");
        return FALSE;
    }

    wm_printf("connected\r\n");
    return TRUE;
}

/*******************************************************************************
** Menu handler
******************************************************************************/
void wm_ui_mqtt_demo(void)
{
    wm_printf("\r\n--- MQTT: GPS stream + remote config ---\r\n");

    /* Re-running stops the stream, so one option covers both directions. The
     * client stays connected so commands can still restart it remotely. */
    if (s_nmea_on)
    {
        wm_sdk_gps_set_nmea_callback(NULL);
        wm_ui_mqtt_nmea_stream_stop();
        wm_printf("stream OFF (dropped=%lu), still subscribed to %s\r\n",
                  (unsigned long)s_nmea_dropped, s_topic_cmd);
        wm_printf("(re-run to start, or publish stream=1 to the cmd topic)\r\n");
        return;
    }

    wm_mqtt_build_ids();

    if (!wm_mqtt_connect())
        return;

    /* One mutex and one task for the life of the app: the toggle only moves the
     * flag and the GNSS callback, so repeated toggling leaks nothing. Both are
     * in place before subscribing, so a command can safely drive the stream. */
    if (s_nmea_mtx == NULL && wm_sdk_mutex_create(&s_nmea_mtx, 0) != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("batch mutex create failed\r\n");
        return;
    }

    s_nmea_len     = 0;
    s_nmea_dropped = 0;

    if (s_nmea_task == NULL)
    {
        s_nmea_task = wm_sdk_task_create(wm_mqtt_nmea_pub_task, NULL, "MQTTPUB", NULL,
                                      WM_MQTT_PUB_TASK_STACK, TP_TIMED_ACTIVITY);
        if (s_nmea_task == NULL)
        {
            wm_printf("publisher task create failed\r\n");
            return;
        }
    }

    /* Registering the callback is what starts the flow; it replaces any raw
     * NMEA callback the GPS demo had installed. */
    if (wm_sdk_gps_set_nmea_callback(wm_mqtt_nmea_cb) != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("GPS not initialised (WM_GPS_SUPPORT off or board not validated?)\r\n");
        return;
    }
    s_nmea_on = TRUE;

    /* Downlink: GPS configuration commands for this IMEI. */
    if (wm_sdk_mqtt_subscribe(s_topic_cmd, WM_SDK_MQTT_QOS1, wm_mqtt_cmd_cb) == WM_SDK_RESULT_SUCCESS)
        wm_printf("subscribed  %s\r\n", s_topic_cmd);
    else
        wm_printf("subscribe to %s failed rc=%ld\r\n",
                  s_topic_cmd, (long)wm_sdk_mqtt_last_error());

    wm_printf("publishing  %s every %lu ms\r\n",
              s_topic_nmea, (unsigned long)s_pub_ms);
    wm_printf("config cmds: power= start= rate= mode= output= stream= interval=\r\n");
    wm_printf("(takes the raw-NMEA callback from 'GPS: Stream raw NMEA')\r\n");
}
