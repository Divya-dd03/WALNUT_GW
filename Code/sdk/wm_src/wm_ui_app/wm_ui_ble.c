/**
  ******************************************************************************
  * @file    wm_ui_ble.c
  * @author  Walnut Medical
  * @brief   Common Gateway (WEGW) reference application - BLE demos.
  *
  *          Reads three peripherals at fixed addresses:
  *
  *          - Autoguard, over a connection. It never speaks first: the gateway
  *            sends a health request carrying its IMEI and the unit replies.
  *          - Two Italon fuel probes, from their advertisements alone. Both are
  *            in range together, so readings are kept per address.
  *
  *          Scanning runs while connected, so it is simply left on.
  *
  *          Both wire formats are decoded here rather than in the SDK, which
  *          carries raw bytes only. Field layouts marked "measured" below
  *          differ from the vendor documents and match the hardware.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 Walnut Medical.
  * All rights reserved.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "wm_ui_ble.h"

/*******************************************************************************
** The peripherals this demo talks to. EDIT THESE FOR YOUR UNITS.
**
** A fuel probe may use a rotating private address: if one stops reporting while
** powered, re-scan with the filter cleared and update its address here.
******************************************************************************/
#define WM_BLE_AG_ADDR      "D8:85:AC:5D:7B:6A"

static const char *const s_fuel_addr[] =
{
    "C5:ED:B4:32:FA:4F",
    "F6:F9:D1:92:93:43",
};

#define WM_BLE_FUEL_COUNT   (sizeof(s_fuel_addr) / sizeof(s_fuel_addr[0]))

/*******************************************************************************
** Demo tuning
******************************************************************************/
#define WM_BLE_PERIOD_MS           10000u   /* monitor cycle                  */
#define WM_BLE_HEALTH_TIMEOUT_MS   5000u    /* wait for the 0x02 reply        */
#define WM_BLE_MON_TASK_STACK      (1024 * 4)

/*******************************************************************************
** Italon fuel probe advertising element (type 0x77). All little-endian.
**
**   0    1  package version - must be 0
**   1    4  timestamp, epoch seconds (only valid if the probe's clock was set)
**   5    2  fuel counter units - NOT litres; litres need a per-tank table
**   7    2  supply voltage, mV
**   9    3  accelerometer X/Y/Z, signed
**  12    1  temperature, signed, -40..+85 C
**  13    1  "notice" - present on current probes only
******************************************************************************/
#define WM_AD_ITALON               0x77u
#define WM_ITALON_AD_MIN           13u

/*******************************************************************************
** Autoguard frame: 78 78 | type | len | content | CRC-ITU (2, big-endian) |
** 0D 0A, where 'len' counts the content plus the two CRC bytes.
******************************************************************************/
#define WM_AG_START                0x78u
#define WM_AG_TYPE_GW_HEALTH       0x01u    /* gateway    -> peripheral       */
#define WM_AG_TYPE_PERIPH_HEALTH   0x02u    /* peripheral -> gateway          */
#define WM_AG_HEALTH_CONTENT_LEN   20u
#define WM_AG_FRAME_MAX            96u      /* reassembly buffer              */
#define WM_AG_IMEI_DIGITS          15u

/* Notify and write share one characteristic on this peripheral. */
#define WM_AG_SERVICE              WM_SDK_BLE_DEFAULT_SERVICE
#define WM_AG_CHAR                 WM_SDK_BLE_DEFAULT_CHAR

/*******************************************************************************
** Decoded data
******************************************************************************/
typedef struct
{
    BOOL   seen;
    INT16  rssi;
    UINT16 fuel_units;
    UINT16 voltage_mv;
    INT8   accel[3];
    INT8   temp_c;
    UINT32 timestamp;
} wm_fuel_t;

typedef struct
{
    UINT8  mac[6];
    UINT8  status_raw;
    UINT8  ign_out;              /* 0=GND 1=VCC 2=Float - see prv_decode_health */
    UINT8  ign_in;
    BOOL   maintenance;
    BOOL   charge;
    BOOL   relay;
    BOOL   siren;
    INT8   rssi_dbm;             /* SIGNED                                     */
    UINT16 batt_mv;              /* BIG-endian on the wire                     */
    UINT8  disconnect_count;
    UINT8  connect_count;
    UINT8  reset_count;
    UINT8  hw_version;           /* decimal digits are the version: 140=1.4.0  */
    UINT8  fw_version;
    UINT8  extra_len;            /* undocumented tail past byte 20             */
} wm_ag_health_t;

/*******************************************************************************
** Demo state
******************************************************************************/
static BOOL  s_ready    = FALSE;   /* module powered + callbacks wired        */
static BOOL  s_scanning = FALSE;
static BOOL  s_mon_run  = FALSE;
static void *s_mon_task = NULL;

/* One slot per fixed address, so both tanks stay separate. */
static wm_fuel_t s_fuel[WM_BLE_FUEL_COUNT];

/* Reply handover: the data callback cannot block, so it decodes into s_health
 * and posts a token that prv_health_exchange() waits on. */
static void          *s_health_q = NULL;
static wm_ag_health_t s_health;
static BOOL           s_health_valid = FALSE;

/* Reassembly buffer: a reply is longer than one default-MTU notification, so it
 * can arrive split. */
static UINT8  s_rx[WM_AG_FRAME_MAX];
static UINT16 s_rx_len = 0u;

/*******************************************************************************
** CRC-ITU (CRC-16/X-25): init 0xFFFF, reflected polynomial 0x8408, final
** one's complement. Bitwise, to avoid a 512-byte table. Test vector:
** crc("123456789") == 0x906E.
******************************************************************************/
static UINT16 prv_crc_itu(const UINT8 *buf, UINT16 len)
{
    UINT16 fcs = 0xFFFFu;
    UINT16 i;
    UINT8  bit;

    for (i = 0u; i < len; i++)
    {
        fcs ^= (UINT16)buf[i];
        for (bit = 0u; bit < 8u; bit++)
            fcs = (fcs & 1u) ? (UINT16)((fcs >> 1) ^ 0x8408u) : (UINT16)(fcs >> 1);
    }
    return (UINT16)(~fcs);
}

/*******************************************************************************
** Build one gateway health request. Returns its length, or 0.
**
** The IMEI becomes 8 BCD bytes, zero-padded on the left to 16 nibbles:
** 863492050167456 -> 08 63 49 20 50 16 74 56. A wrong-length IMEI is rejected
** rather than padded, since a malformed identity would be answered as valid.
******************************************************************************/
static UINT16 prv_build_health(const char *imei, UINT8 *out, UINT16 out_size)
{
    UINT8  content[10];
    UINT16 crc;
    UINT16 n = 0u;
    UINT32 i;

    if (out_size < 18u || imei == NULL)
        return 0u;

    for (i = 0u; i < WM_AG_IMEI_DIGITS; i++)
    {
        if (imei[i] < '0' || imei[i] > '9')
            return 0u;
    }
    if (imei[WM_AG_IMEI_DIGITS] != '\0')
        return 0u;

    content[0] = (UINT8)(imei[0] - '0');   /* leading pad nibble is zero */
    for (i = 1u; i < 8u; i++)
        content[i] = (UINT8)(((imei[(2u * i) - 1u] - '0') << 4) | (imei[2u * i] - '0'));

    content[8] = 0x00u;   /* status: bit2 tamper, bit1 charge, bit0 ACC */
    content[9] = 0x00u;   /* speed, km/h                                */

    out[n++] = WM_AG_START;
    out[n++] = WM_AG_START;
    out[n++] = WM_AG_TYPE_GW_HEALTH;
    out[n++] = (UINT8)(sizeof(content) + 2u);   /* content + the 2 CRC bytes */
    memcpy(&out[n], content, sizeof(content));
    n += (UINT16)sizeof(content);

    /* Measured: the checksum covers type + length + content, i.e. from offset 2
     * up to the CRC field. */
    crc = prv_crc_itu(&out[2], (UINT16)(n - 2u));
    out[n++] = (UINT8)(crc >> 8);     /* big-endian on the wire */
    out[n++] = (UINT8)(crc & 0xFFu);
    out[n++] = 0x0Du;
    out[n++] = 0x0Au;

    return n;
}

/*******************************************************************************
** Deframer: byte stream -> one frame, copied out and consumed.
**
** Tolerant on receive, strict on transmit. Peripherals have been seen to use
** three length conventions (content+2, +1 and +3) and either terminator order,
** so all are accepted; only content+2 with 0D 0A is ever sent. A frame whose
** CRC fails is dropped, never reported.
******************************************************************************/
static BOOL prv_deframe_next(UINT8 *type, UINT8 *content, UINT8 *content_len,
                             UINT8 content_max)
{
    static const UINT8 extras[3] = { 6u, 7u, 5u };   /* len+2, len+1, len+3 */

    while (s_rx_len >= 8u)   /* 2 start + type + len + 2 crc + 2 stop */
    {
        UINT16 start = 0u;
        UINT16 k;
        UINT8  lenf;
        BOOL   found = FALSE;
        BOOL   incomplete = FALSE;

        while ((start + 1u) < s_rx_len)
        {
            if (s_rx[start] == WM_AG_START && s_rx[start + 1u] == WM_AG_START)
            {
                found = TRUE;
                break;
            }
            start++;
        }
        if (!found)
        {
            /* Keep the trailing byte: it may be the first half of a marker
             * whose second half has not arrived yet. */
            s_rx[0] = s_rx[s_rx_len - 1u];
            s_rx_len = 1u;
            return FALSE;
        }
        if (start > 0u)
        {
            memmove(s_rx, &s_rx[start], (size_t)(s_rx_len - start));
            s_rx_len = (UINT16)(s_rx_len - start);
        }
        if (s_rx_len < 8u)
            return FALSE;

        lenf = s_rx[3];

        for (k = 0u; k < 3u; k++)
        {
            UINT16 total = (UINT16)lenf + extras[k];
            UINT16 want;
            BOOL   stop_ok;

            if (total < 8u || total > WM_AG_FRAME_MAX)
                continue;
            if (s_rx_len < total)
            {
                incomplete = TRUE;
                continue;
            }

            stop_ok = ((s_rx[total - 2u] == 0x0Du && s_rx[total - 1u] == 0x0Au) ||
                       (s_rx[total - 2u] == 0x0Au && s_rx[total - 1u] == 0x0Du))
                          ? TRUE : FALSE;

            want = (UINT16)(((UINT16)s_rx[total - 4u] << 8) | s_rx[total - 3u]);

            if (stop_ok && prv_crc_itu(&s_rx[2], (UINT16)(total - 6u)) == want)
            {
                UINT8 clen = (UINT8)(total - 8u);

                if (clen > content_max)
                    clen = content_max;

                *type        = s_rx[2];
                *content_len = clen;
                if (clen > 0u)
                    memcpy(content, &s_rx[4], clen);

                /* Consumed here, so a caller can never leave a decoded frame in
                 * the buffer and spin on it. */
                memmove(s_rx, &s_rx[total], (size_t)(s_rx_len - total));
                s_rx_len = (UINT16)(s_rx_len - total);
                return TRUE;
            }
        }

        if (incomplete)
            return FALSE;        /* wait for the rest of the frame */

        /* No candidate had both a valid checksum and a plausible terminator, so
         * this start marker was noise. Step past it and resync. */
        memmove(s_rx, &s_rx[2], (size_t)(s_rx_len - 2u));
        s_rx_len = (UINT16)(s_rx_len - 2u);
    }

    return FALSE;
}

/*******************************************************************************
** Peripheral health reply
******************************************************************************/
static void prv_decode_health(const UINT8 *c, UINT8 len, wm_ag_health_t *h)
{
    memset(h, 0, sizeof(*h));

    memcpy(h->mac, &c[0], sizeof(h->mac));

    /* Ignition states need three values, so they are 2-bit fields. Unconfirmed,
     * which is why status_raw is always reported alongside. */
    h->status_raw  = c[6];
    h->ign_out     = (UINT8)((c[6] >> 6) & 0x03u);
    h->ign_in      = (UINT8)((c[6] >> 4) & 0x03u);
    h->maintenance = (c[6] & 0x08u) ? TRUE : FALSE;
    h->charge      = (c[6] & 0x04u) ? TRUE : FALSE;
    h->relay       = (c[6] & 0x02u) ? TRUE : FALSE;
    h->siren       = (c[6] & 0x01u) ? TRUE : FALSE;

    h->rssi_dbm = (INT8)c[8];   /* measured: signed */

    /* Measured: big-endian, unlike every other multi-byte field here. */
    h->batt_mv = (UINT16)(((UINT16)c[9] << 8) | c[10]);

    h->disconnect_count = c[11];
    h->connect_count    = c[12];
    h->reset_count      = c[13];

    /* Each version occupies two bytes, only the first of which is used. */
    h->hw_version = c[15];
    h->fw_version = c[17];

    /* Anything beyond this is device-specific and left undecoded. */
    if (len > WM_AG_HEALTH_CONTENT_LEN)
        h->extra_len = (UINT8)(len - WM_AG_HEALTH_CONTENT_LEN);
}

/*******************************************************************************
** SDK callbacks - these must stay short and make no blocking call.
******************************************************************************/
static void prv_scan_cb(const wm_SdkBleScanResult *res)
{
    const UINT8 *s = NULL;
    UINT8        len = 0u;
    UINT32       i;

    /* The filter already narrows this to our probes; find which slot. */
    for (i = 0u; i < WM_BLE_FUEL_COUNT; i++)
    {
        if (strcmp(res->addr, s_fuel_addr[i]) == 0)
            break;
    }
    if (i >= WM_BLE_FUEL_COUNT)
        return;

    if (WM_SDK_RESULT_SUCCESS != wm_sdk_ble_adv_find(res->data, res->len,
                                               WM_AD_ITALON, &s, &len))
        return;
    /* Type 0x77 is not an assigned type, so check the version byte too. */
    if (len < WM_ITALON_AD_MIN || s[0] != 0x00u)
        return;

    s_fuel[i].rssi       = res->rssi;
    s_fuel[i].timestamp  = (UINT32)s[1] | ((UINT32)s[2] << 8) |
                           ((UINT32)s[3] << 16) | ((UINT32)s[4] << 24);
    s_fuel[i].fuel_units = (UINT16)((UINT16)s[5] | ((UINT16)s[6] << 8));
    s_fuel[i].voltage_mv = (UINT16)((UINT16)s[7] | ((UINT16)s[8] << 8));
    s_fuel[i].accel[0]   = (INT8)s[9];
    s_fuel[i].accel[1]   = (INT8)s[10];
    s_fuel[i].accel[2]   = (INT8)s[11];
    s_fuel[i].temp_c     = (INT8)s[12];
    s_fuel[i].seen       = TRUE;
}

static void prv_data_cb(const UINT8 *data, UINT16 len)
{
    UINT8 content[WM_AG_FRAME_MAX];
    UINT8 type;
    UINT8 clen;

    if (data == NULL || len == 0u)
        return;

    /* Slide the window on overflow so a part-received frame survives. */
    if (len > (UINT16)WM_AG_FRAME_MAX)
    {
        data += (len - (UINT16)WM_AG_FRAME_MAX);
        len   = (UINT16)WM_AG_FRAME_MAX;
    }
    if ((UINT16)(s_rx_len + len) > (UINT16)WM_AG_FRAME_MAX)
    {
        UINT16 drop = (UINT16)((s_rx_len + len) - (UINT16)WM_AG_FRAME_MAX);

        memmove(s_rx, &s_rx[drop], (size_t)(s_rx_len - drop));
        s_rx_len = (UINT16)(s_rx_len - drop);
    }

    memcpy(&s_rx[s_rx_len], data, len);
    s_rx_len = (UINT16)(s_rx_len + len);

    while (prv_deframe_next(&type, content, &clen, (UINT8)sizeof(content)))
    {
        if (type == WM_AG_TYPE_PERIPH_HEALTH && clen >= WM_AG_HEALTH_CONTENT_LEN)
        {
            UINT32 token = 1u;

            prv_decode_health(content, clen, &s_health);
            s_health_valid = TRUE;
            if (s_health_q != NULL)
                (void)wm_sdk_msgq_send(s_health_q, &token, 0u);   /* non-blocking */
        }
        else
        {
            RTI_LOG("BLE: ignoring AG frame type 0x%02X len %u",
                    (unsigned)type, (unsigned)clen);
        }
    }
}

/*******************************************************************************
** Bring-up, shared by every option
******************************************************************************/
static BOOL prv_ble_ready(void)
{
    char  buf[WM_SDK_BLE_ADDR_STR_LEN];
    UINT8 power = 0u;

    if (s_ready)
        return TRUE;

    if (wm_sdk_ble_get_power_status(&power) != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("BLE not initialised (WM_BLE_SUPPORT off, no module fitted, "
                  "or board not validated?)\r\n");
        return FALSE;
    }

    if (power == 0u)
    {
        wm_SdkResult sr = wm_sdk_ble_set_power_status(1);

        wm_printf("power on -> rc=%ld\r\n", (long)sr);
        if (sr != WM_SDK_RESULT_SUCCESS)
            return FALSE;
        wm_sdk_task_sleep(500);   /* let the module finish booting */
    }

    memset(buf, 0, sizeof(buf));
    if (wm_sdk_ble_get_address(buf, sizeof(buf)) == WM_SDK_RESULT_SUCCESS)
        wm_printf("module address=%s\r\n", buf);

    if (s_health_q == NULL)
        s_health_q = wm_sdk_msgq_create("blehq", sizeof(UINT32), 4, 0);

    wm_sdk_ble_set_scan_callback(prv_scan_cb);
    wm_sdk_ble_set_data_callback(prv_data_cb);
    wm_sdk_ble_set_target(WM_AG_SERVICE, WM_AG_CHAR);   /* before connecting */

    s_ready = TRUE;
    return TRUE;
}

/*******************************************************************************
** Scanning. Every address is known up front, so the scan runs filtered and is
** left on: no window has to be opened around the Autoguard exchange.
******************************************************************************/
static BOOL prv_scan_start(void)
{
    const char *list[WM_BLE_FUEL_COUNT];
    UINT32      i;

    for (i = 0u; i < WM_BLE_FUEL_COUNT; i++)
        list[i] = s_fuel_addr[i];

    if (wm_sdk_ble_scan_set_filter(list, (UINT8)WM_BLE_FUEL_COUNT) != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("scan filter rejected - check the addresses above\r\n");
        return FALSE;
    }

    /* RAW mode is required: the probe's measurement is in a non-standard
     * advertising data type, which DECODED mode does not report. */
    if (wm_sdk_ble_scan_start(WM_SDK_BLE_SCAN_RAW) != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("scan start failed\r\n");
        return FALSE;
    }

    memset(s_fuel, 0, sizeof(s_fuel));
    s_scanning = TRUE;
    return TRUE;
}

static void prv_fuel_print(void)
{
    UINT32 i;

    for (i = 0u; i < WM_BLE_FUEL_COUNT; i++)
    {
        if (!s_fuel[i].seen)
        {
            wm_printf("[FUEL %s] not heard yet\r\n", s_fuel_addr[i]);
            continue;
        }
        wm_printf("[FUEL %s] rssi=%d fuel=%u units volt=%u mV temp=%d C accel=%d,%d,%d\r\n",
                  s_fuel_addr[i], (int)s_fuel[i].rssi,
                  (unsigned)s_fuel[i].fuel_units, (unsigned)s_fuel[i].voltage_mv,
                  (int)s_fuel[i].temp_c,
                  (int)s_fuel[i].accel[0], (int)s_fuel[i].accel[1],
                  (int)s_fuel[i].accel[2]);
    }
}

/*******************************************************************************
** Autoguard health exchange. Blocks, so it runs on an application task.
******************************************************************************/
static const char *prv_ign_name(UINT8 s)
{
    return (s == 0u) ? "GND" : (s == 1u) ? "VCC" : (s == 2u) ? "Float" : "?";
}

static void prv_health_exchange(BOOL keep_open)
{
    char      imei[20] = {0};
    UINT8     frame[32];
    UINT16    flen;
    UINT32    token;
    wm_SdkResult sr;
    BOOL      linked = FALSE;
    BOOL      ok = FALSE;

    if (wm_sdk_device_get_imei(imei, sizeof(imei)) != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("IMEI read failed - cannot build a health frame\r\n");
        return;
    }

    flen = prv_build_health(imei, frame, sizeof(frame));
    if (flen == 0u)
    {
        wm_printf("health frame build failed (IMEI \"%s\" not 15 digits?)\r\n", imei);
        return;
    }

    /* Only connect when the link is down: connecting again returns BUSY. */
    if (wm_sdk_ble_is_connected(&linked) != WM_SDK_RESULT_SUCCESS)
        linked = FALSE;

    if (!linked)
    {
        sr = wm_sdk_ble_connect(WM_BLE_AG_ADDR);
        if (sr != WM_SDK_RESULT_SUCCESS)
        {
            wm_printf("[AG %s] connect failed rc=%ld\r\n", WM_BLE_AG_ADDR, (long)sr);
            return;
        }
    }

    /* Drop any stale reply, so the wait below can only be satisfied by this
     * request's answer. */
    while (wm_sdk_msgq_recv(s_health_q, &token, 0u) == WM_SDK_RESULT_SUCCESS)
        ;
    s_health_valid = FALSE;
    s_rx_len       = 0u;

    sr = wm_sdk_ble_send(frame, flen);
    if (sr != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("[AG] send failed rc=%ld\r\n", (long)sr);
    }
    else if (wm_sdk_msgq_recv(s_health_q, &token, WM_BLE_HEALTH_TIMEOUT_MS)
                 == WM_SDK_RESULT_SUCCESS && s_health_valid)
    {
        const wm_ag_health_t *h = &s_health;

        ok = TRUE;
        wm_printf("[AG %02X:%02X:%02X:%02X:%02X:%02X] status=0x%02X ign_in=%s ign_out=%s "
                  "relay=%u siren=%u charge=%u maint=%u\r\n",
                  (unsigned)h->mac[0], (unsigned)h->mac[1], (unsigned)h->mac[2],
                  (unsigned)h->mac[3], (unsigned)h->mac[4], (unsigned)h->mac[5],
                  (unsigned)h->status_raw,
                  prv_ign_name(h->ign_in), prv_ign_name(h->ign_out),
                  (unsigned)(h->relay ? 1u : 0u), (unsigned)(h->siren ? 1u : 0u),
                  (unsigned)(h->charge ? 1u : 0u),
                  (unsigned)(h->maintenance ? 1u : 0u));
        /* The version byte's decimal digits are the version: 140 is 1.4.0. */
        wm_printf("      batt=%u mV rssi=%d dBm conn/disc=%u/%u resets=%u "
                  "hw=%u.%u.%u fw=%u.%u.%u%s\r\n",
                  (unsigned)h->batt_mv, (int)h->rssi_dbm,
                  (unsigned)h->connect_count, (unsigned)h->disconnect_count,
                  (unsigned)h->reset_count,
                  (unsigned)(h->hw_version / 100u),
                  (unsigned)((h->hw_version / 10u) % 10u),
                  (unsigned)(h->hw_version % 10u),
                  (unsigned)(h->fw_version / 100u),
                  (unsigned)((h->fw_version / 10u) % 10u),
                  (unsigned)(h->fw_version % 10u),
                  (h->extra_len > 0u) ? " (+undocumented tail)" : "");
    }
    else
    {
        /* The peripheral never speaks first. Persistent silence on a unit that
         * connects usually means it needs a power cycle. */
        wm_printf("[AG] no health reply within %lu ms\r\n",
                  (unsigned long)WM_BLE_HEALTH_TIMEOUT_MS);
    }

    /* Hold the link only when it is working and the caller wants it. */
    if (!keep_open || !ok)
        wm_sdk_ble_disconnect();
}

/*******************************************************************************
** Monitor task - keeps the blocking work off the menu dispatcher.
******************************************************************************/
static void prv_monitor_task(void *arg)
{
    (void)arg;

    while (1)
    {
        if (!s_mon_run)
        {
            wm_sdk_task_sleep(200);
            continue;
        }

        wm_printf("\r\n[BLEMON] --- cycle ---\r\n");
        prv_fuel_print();          /* scanning never stopped, so this is live */
        prv_health_exchange(TRUE); /* keep the link up between cycles */

        wm_sdk_task_sleep(WM_BLE_PERIOD_MS);
    }
}

/*******************************************************************************
** Menu handlers
******************************************************************************/
void wm_ui_ble_scan_demo(void)
{
    wm_printf("\r\n--- BLE: Scan ---\r\n");

    if (!prv_ble_ready())
        return;

    if (s_scanning)
    {
        wm_sdk_ble_scan_stop();
        s_scanning = FALSE;
        wm_printf("scanning OFF\r\n");
        return;
    }

    if (prv_scan_start())
        wm_printf("scanning ON, filtered to %lu fuel probe(s) - re-run to stop\r\n",
                  (unsigned long)WM_BLE_FUEL_COUNT);
}

void wm_ui_ble_read_demo(void)
{
    wm_printf("\r\n--- BLE: Read peripherals ---\r\n");

    if (!prv_ble_ready())
        return;

    if (!s_scanning && !prv_scan_start())
        return;

    prv_fuel_print();
    prv_health_exchange(FALSE);   /* one-shot: close the link when done */
}

void wm_ui_ble_monitor_demo(void)
{
    wm_printf("\r\n--- BLE: Periodic read ---\r\n");

    if (!prv_ble_ready())
        return;

    if (!s_scanning && !prv_scan_start())
        return;

    if (s_mon_task == NULL)
    {
        s_mon_task = wm_sdk_task_create(prv_monitor_task, NULL, "BLEMON", NULL,
                                     WM_BLE_MON_TASK_STACK, TP_TIMED_ACTIVITY);
        if (s_mon_task == NULL)
        {
            wm_printf("monitor task create failed\r\n");
            return;
        }
    }

    /* Toggled by a flag, not by deleting the task: a cycle may be mid-connect,
     * and deleting it there would leave a link nothing closes. */
    s_mon_run = s_mon_run ? FALSE : TRUE;
    wm_printf("monitor %s (period %lu ms) - re-run to %s\r\n",
              s_mon_run ? "ON" : "OFF", (unsigned long)WM_BLE_PERIOD_MS,
              s_mon_run ? "stop" : "start");
}

void wm_ui_ble_power_off_demo(void)
{
    wm_printf("\r\n--- BLE: Power off ---\r\n");

    s_mon_run = FALSE;

    if (wm_sdk_ble_scan_stop() == WM_SDK_RESULT_NOT_INITIALIZED)
    {
        wm_printf("BLE not initialised\r\n");
        return;
    }

    wm_sdk_ble_scan_clear_filter();
    wm_sdk_ble_disconnect();
    wm_sdk_ble_set_scan_callback(NULL);
    wm_sdk_ble_set_data_callback(NULL);

    wm_printf("power off -> rc=%ld\r\n", (long)wm_sdk_ble_set_power_status(0));

    /* Force the next option through prv_ble_ready() to power back up. */
    s_ready    = FALSE;
    s_scanning = FALSE;
    s_rx_len   = 0u;
    memset(s_fuel, 0, sizeof(s_fuel));
}
