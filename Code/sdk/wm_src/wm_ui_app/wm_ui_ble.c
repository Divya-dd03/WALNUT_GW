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
#include "wm_ui_smp.h"

/*******************************************************************************
** The peripherals this demo talks to. EDIT THESE FOR YOUR UNITS.
**
** A fuel probe may use a rotating private address: if one stops reporting while
** powered, re-scan with the filter cleared and update its address here.
******************************************************************************/
#define WM_BLE_AG_ADDR      "D8:85:AC:5D:7B:6A"
#define WM_BLE_AG_ADDR_TYPE WM_SDK_BLE_ADDR_PUBLIC   /* +SCANRAW type of the AG */

/* Autoguard 3.0: random address, and the bench sequence that works with it -
 * a short unfiltered scan at 80/80, stopped before connecting. */
#define WM_BLE_AG3_ADDR           "FB:4A:3C:6B:74:C3"
#define WM_BLE_AG3_ADDR_TYPE      WM_SDK_BLE_ADDR_RANDOM
#define WM_BLE_AG3_SCAN_INTERVAL  80u   /* x1.25 ms */
#define WM_BLE_AG3_SCAN_WINDOW    80u

/* AG 3.0 DFU: its own staging file, so a radio image cannot be sent by mistake. */
#define WM_BLE_AG3_FW_PATH        "D:/ag3_fw.bin"
#define WM_BLE_AG3_FW_VOLUME      "D:/"

/* Walnut fuel sensor: an nRF52 behind MCUboot, so the AG 3.0 method fits it. */
/* This is the sensor's own address, not necessarily a probe listed below. */
#define WM_BLE_FUEL_DFU_ADDR      "C5:ED:B4:32:FA:4F"
#define WM_BLE_FUEL_DFU_ADDR_TYPE WM_SDK_BLE_ADDR_RANDOM
#define WM_BLE_FUEL_SCAN_INTERVAL 80u   /* x1.25 ms */
#define WM_BLE_FUEL_SCAN_WINDOW   80u

/* Fuel sensor DFU: its own staging file, kept apart from the AG 3.0 image. */
#define WM_BLE_FUEL_FW_PATH       "D:/fuel_fw.bin"
#define WM_BLE_FUEL_FW_VOLUME     "D:/"

/* One SMP round trip. Erasing the secondary slot makes the first one slow. */
#define WM_BLE_SMP_TIMEOUT        20000u
#define WM_BLE_SMP_FIRST_MS       60000u

/* MCUboot slot the upload lands in, and the one it runs from after the swap. */
#define WM_BLE_SLOT_SECONDARY     1u
#define WM_BLE_SLOT_PRIMARY       0u

/* Reboot plus the MCUboot slot swap, before the unit advertises again. */
#define WM_BLE_BOOT_WAIT_MS       20000u
#define WM_BLE_RECONNECT_TRIES    5

/* BLE radio firmware: URL and SHA-256 are typed in at run time by the operator. */
#define WM_BLE_FW_PATH      "D:/ble_fw.bin"  /* external flash (WM_EXT_FLASH_SUPPORT) */
#define WM_BLE_FW_VOLUME    "D:/"
#define WM_BLE_FW_TIMEOUT   30u    /* seconds, per ranged request                     */
#define WM_BLE_FW_CHUNK     4096u  /* one ranged GET per chunk, < RESP_BUF_SIZE       */

/* No compiled-in anchor can be assumed for a typed URL, so the digest guards it. */
static const char *s_fw_ca = NULL;   /* a PEM held by reference, or NULL for none */

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
#define WM_BLE_SCAN_SETTLE_MS      2000u    /* scan before the first connect  */
#define WM_BLE_MON_TASK_STACK      (1024 * 8)   /* 8K: the DFU job runs here   */
#define WM_BLE_GATT_MAX            12u      /* services listed per discovery  */

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

/* One MCUmgr DFU target: all a unit differs by, since the method is shared. */
typedef struct
{
    const char *name;            /* as it reads in a sentence                  */
    const char *image;           /* image file the vendor build produces       */
    const char *addr;
    UINT8       addr_type;
    const char *path;            /* where the image is staged on this device   */
    const char *volume;
    UINT16      scan_interval;   /* x1.25 ms, for the scan before a connect    */
    UINT16      scan_window;
} wm_dfu_target_t;

static const wm_dfu_target_t s_dfu_ag3 =
{
    "AG 3.0", "firmware.signed.bin",
    WM_BLE_AG3_ADDR, WM_BLE_AG3_ADDR_TYPE,
    WM_BLE_AG3_FW_PATH, WM_BLE_AG3_FW_VOLUME,
    WM_BLE_AG3_SCAN_INTERVAL, WM_BLE_AG3_SCAN_WINDOW
};

static const wm_dfu_target_t s_dfu_fuel =
{
    "fuel sensor", "app_update.bin",
    WM_BLE_FUEL_DFU_ADDR, WM_BLE_FUEL_DFU_ADDR_TYPE,
    WM_BLE_FUEL_FW_PATH, WM_BLE_FUEL_FW_VOLUME,
    WM_BLE_FUEL_SCAN_INTERVAL, WM_BLE_FUEL_SCAN_WINDOW
};

/*******************************************************************************
** Demo state
******************************************************************************/
static BOOL  s_ready    = FALSE;   /* module powered + callbacks wired        */
static BOOL  s_scanning = FALSE;
static BOOL  s_mon_run  = FALSE;
static void *s_mon_task = NULL;

/* Each Autoguard's channel id, -1 while its link is down. */
static volatile int s_ag_cid  = -1;
static volatile int s_ag3_cid = -1;

/* The address a job asked the scan to look out for, and what it saw of it. */
static const char   *volatile s_watch_addr = NULL;
static volatile BOOL  s_watch_seen = FALSE;
static volatile INT16 s_watch_rssi = 0;
static volatile UINT8 s_watch_type = 0u;

/* Raw content of the last health reply, printed beside the decode. */
static UINT8 s_health_raw[WM_AG_FRAME_MAX];
static UINT8 s_health_raw_len = 0u;

/* One-shot jobs that block, run on the BLEMON task instead of the menu. */
typedef enum
{
    WM_BLE_JOB_NONE = 0,
    WM_BLE_JOB_GATT,
    WM_BLE_JOB_FW_UPDATE,
    WM_BLE_JOB_AG3_HEALTH,
    WM_BLE_JOB_DFU
} wm_ble_job_e;

static volatile wm_ble_job_e s_job = WM_BLE_JOB_NONE;

/* Which unit a queued DFU job updates, set before the job is posted. */
static const wm_dfu_target_t *volatile s_dfu = NULL;

/* While a DFU runs the link carries SMP, not Autoguard frames. */
static volatile BOOL s_smp_mode = FALSE;
static volatile int  s_smp_cid  = -1;

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
** CRC-16/MODBUS: init 0xFFFF, reflected polynomial 0xA001, no final XOR.
** Measured: the AG 3.0 checks its replies with this, over the same bytes as
** CRC-ITU but sent LITTLE-endian (78 78 02 16 ... 9E 43 0D 0A -> 0x439E),
** while it still accepts CRC-ITU requests.
******************************************************************************/
static UINT16 prv_crc_modbus(const UINT8 *buf, UINT16 len)
{
    UINT16 crc = 0xFFFFu;
    UINT16 i;
    UINT8  bit;

    for (i = 0u; i < len; i++)
    {
        crc ^= (UINT16)buf[i];
        for (bit = 0u; bit < 8u; bit++)
            crc = (crc & 1u) ? (UINT16)((crc >> 1) ^ 0xA001u) : (UINT16)(crc >> 1);
    }
    return crc;
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

            /* CRC-ITU big-endian (AG 1.x), or CRC-16/MODBUS little-endian
             * (AG 3.0 replies) - same bytes covered by both. */
            if (stop_ok &&
                (prv_crc_itu(&s_rx[2], (UINT16)(total - 6u)) == want ||
                 prv_crc_modbus(&s_rx[2], (UINT16)(total - 6u)) ==
                     (UINT16)((want >> 8) | (want << 8))))
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

    /* Recorded only while a job scans unfiltered for one known address. */
    if (s_watch_addr != NULL && strcmp(res->addr, (const char *)s_watch_addr) == 0)
    {
        s_watch_rssi = res->rssi;
        s_watch_type = res->addr_type;
        s_watch_seen = TRUE;
    }

    /* The filter narrows this to our probes; find which slot. */
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

/* A module reset clears the target UUID, stops scanning and drops every link;
 * force the next cycle to redo all three. */
static void prv_evt_cb(wm_SdkBleEvent evt, int cid)
{
    if (evt == WM_SDK_BLE_EVT_READY)
    {
        s_ready    = FALSE;
        s_scanning = FALSE;
        s_ag_cid   = -1;
        s_ag3_cid  = -1;
    }
    else if (evt == WM_SDK_BLE_EVT_DISCONNECTED)
    {
        if (cid == s_ag_cid)
            s_ag_cid = -1;
        if (cid == s_ag3_cid)
            s_ag3_cid = -1;
    }
}

static void prv_data_cb(int cid, const UINT8 *data, UINT16 len)
{
    UINT8 content[WM_AG_FRAME_MAX];
    UINT8 type;
    UINT8 clen;

    /* A DFU link speaks SMP, so it is handed over before the Autoguard decode. */
    if (s_smp_mode && cid == s_smp_cid)
    {
        wm_ui_smp_rx(data, len);
        return;
    }

    /* Only the Autoguard links carry frames this demo understands. */
    if ((cid != s_ag_cid && cid != s_ag3_cid) || data == NULL || len == 0u)
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
            memcpy(s_health_raw, content, clen);
            s_health_raw_len = clen;
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
    wm_sdk_ble_set_event_callback(prv_evt_cb);
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

    /* RAW: the probe's measurement is in a non-standard advertising data type. */
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

/* One request/reply on the link held in *cid_slot, connecting first if it is
 * down. Shared by both Autoguard generations. */
static void prv_health_exchange(const char *addr, UINT8 addr_type,
                                volatile int *cid_slot, BOOL keep_open)
{
    char      imei[20] = {0};
    UINT8     frame[32];
    UINT16    flen;
    UINT32    token;
    wm_SdkResult sr;
    BOOL      ok = FALSE;
    int       cid;

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

    /* Reuse the link while it is up: a second connect would take another cid. */
    if (*cid_slot < 0)
    {
        sr = wm_sdk_ble_connect(addr, addr_type, &cid);
        if (sr != WM_SDK_RESULT_SUCCESS)
        {
            wm_printf("[AG %s] connect failed rc=%ld\r\n", addr, (long)sr);
            return;
        }
        *cid_slot = cid;
        wm_printf("[AG %s] connected on cid %d\r\n", addr, cid);
    }
    cid = *cid_slot;

    /* Drop any stale reply, so the wait below can only be satisfied by this
     * request's answer. */
    while (wm_sdk_msgq_recv(s_health_q, &token, 0u) == WM_SDK_RESULT_SUCCESS)
        ;
    s_health_valid = FALSE;
    s_rx_len       = 0u;

    sr = wm_sdk_ble_send(cid, frame, flen);
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

        /* Raw content too: the field map above was measured on AG 1.x. */
        {
            UINT8 i;

            wm_printf("      raw=");
            for (i = 0u; i < s_health_raw_len; i++)
                wm_printf("%02X", (unsigned)s_health_raw[i]);
            wm_printf("\r\n");
        }
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
    {
        wm_sdk_ble_disconnect(cid);
        *cid_slot = -1;
    }
}

/*******************************************************************************
** One-shot jobs. Both block for seconds, so they run on the BLEMON task.
******************************************************************************/
static void prv_gatt_job(void)
{
    wm_SdkBleGattService svc[WM_BLE_GATT_MAX];
    wm_SdkResult         sr;
    UINT8                n = 0u;
    UINT8                i;
    BOOL                 opened = FALSE;
    int                  cid;

    wm_printf("\r\n[BLE GATT] --- Autoguard %s ---\r\n", WM_BLE_AG_ADDR);

    if (s_ag_cid < 0)
    {
        sr = wm_sdk_ble_connect(WM_BLE_AG_ADDR, WM_BLE_AG_ADDR_TYPE, &cid);
        if (sr != WM_SDK_RESULT_SUCCESS)
        {
            wm_printf("connect failed rc=%ld\r\n", (long)sr);
            return;
        }
        s_ag_cid = cid;
        opened   = TRUE;
    }
    cid = s_ag_cid;

    sr = wm_sdk_ble_gatt_discover(cid, NULL, svc, (UINT8)WM_BLE_GATT_MAX, &n);
    if (sr != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("discovery on cid %d failed rc=%ld\r\n", cid, (long)sr);
    }
    else
    {
        wm_printf("cid %d: %u primary service(s)\r\n", cid, (unsigned)n);
        for (i = 0u; i < n && i < (UINT8)WM_BLE_GATT_MAX; i++)
            wm_printf("  %04X-%04X  %s\r\n", (unsigned)svc[i].start_handle,
                      (unsigned)svc[i].end_handle, svc[i].uuid);
        if (n > (UINT8)WM_BLE_GATT_MAX)
            wm_printf("  ... %u more not listed\r\n", (unsigned)(n - (UINT8)WM_BLE_GATT_MAX));
    }

    /* Leave a link the monitor opened; close only the one opened here. */
    if (opened)
    {
        wm_sdk_ble_disconnect(cid);
        s_ag_cid = -1;
    }
}

/* Flashes what the menu handler already downloaded and verified. */
static void prv_fw_job(void)
{
    char         ver[48];
    wm_SdkResult sr;

    wm_printf("\r\n[BLE FW] --- update from %s ---\r\n", WM_BLE_FW_PATH);

    if (wm_sdk_file_exists(WM_BLE_FW_PATH) != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("no image at %s - download it with this option first\r\n",
                  WM_BLE_FW_PATH);
        return;
    }

    /* The update needs every link down and holds the radio for about a
     * minute, so the monitor is stopped rather than paused. */
    s_mon_run = FALSE;
    wm_sdk_ble_disconnect_all();
    s_ag_cid  = -1;
    s_ag3_cid = -1;

    memset(ver, 0, sizeof(ver));
    if (wm_sdk_ble_get_version(ver, sizeof(ver)) == WM_SDK_RESULT_SUCCESS)
        wm_printf("running version: %s\r\n", ver);

    wm_printf("updating - about a minute, do not power off\r\n");
    sr = wm_sdk_ble_fw_update(WM_BLE_FW_PATH);
    wm_printf("update -> rc=%ld%s\r\n", (long)sr,
              (sr == WM_SDK_RESULT_SUCCESS) ? "" : " (old firmware still runs)");

    /* The radio restarted (or was left mid-update): set everything up again. */
    s_ready    = FALSE;
    s_scanning = FALSE;

    memset(ver, 0, sizeof(ver));
    if (wm_sdk_ble_get_version(ver, sizeof(ver)) == WM_SDK_RESULT_SUCCESS)
        wm_printf("now running: %s\r\n", ver);
}

/* AG 3.0 health read, in the order that works on the bench: target UUID
 * (prv_ble_ready), unfiltered scan at 80/80, scan stopped, connect with a
 * random address, request, reply, disconnect. */
static void prv_ag3_job(void)
{
    const wm_SdkBleScanParam ag3 = { WM_BLE_AG3_SCAN_INTERVAL, WM_BLE_AG3_SCAN_WINDOW };
    wm_SdkBleScanParam       saved;
    wm_SdkResult             sr;

    wm_printf("\r\n[AG3] --- health %s ---\r\n", WM_BLE_AG3_ADDR);

    if (wm_sdk_ble_scan_get_param(&saved) != WM_SDK_RESULT_SUCCESS)
    {
        saved.interval = 96u;
        saved.window   = 48u;
    }

    /* Any running scan (fuel probes) is replaced; the monitor redoes it. */
    wm_sdk_ble_scan_stop();
    s_scanning   = FALSE;
    s_watch_seen = FALSE;
    s_watch_addr = WM_BLE_AG3_ADDR;

    if ((sr = wm_sdk_ble_scan_set_param(&ag3)) != WM_SDK_RESULT_SUCCESS ||
        (sr = wm_sdk_ble_scan_clear_filter()) != WM_SDK_RESULT_SUCCESS ||
        (sr = wm_sdk_ble_scan_start(WM_SDK_BLE_SCAN_RAW)) != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("scan setup failed rc=%ld\r\n", (long)sr);
        (void)wm_sdk_ble_scan_set_param(&saved);
        s_watch_addr = NULL;
        return;
    }

    wm_sdk_task_sleep(WM_BLE_SCAN_SETTLE_MS);
    wm_sdk_ble_scan_stop();
    (void)wm_sdk_ble_scan_set_param(&saved);   /* not scanning: just stored */
    s_watch_addr = NULL;

    if (s_watch_seen)
        wm_printf("seen in scan: rssi=%d dBm type=%s\r\n", (int)s_watch_rssi,
                  (s_watch_type == WM_SDK_BLE_ADDR_RANDOM) ? "random" : "public");
    else
        wm_printf("not seen in %lu ms of scanning - trying to connect anyway\r\n",
                  (unsigned long)WM_BLE_SCAN_SETTLE_MS);

    prv_health_exchange(WM_BLE_AG3_ADDR, WM_BLE_AG3_ADDR_TYPE, &s_ag3_cid, FALSE);
}

/* Size of the staged image, which the upload has to announce up front. */
static BOOL prv_dfu_fw_size(const char *path, UINT32 *size)
{
    void *f = wm_sdk_file_open(path, "rb");

    if (f == NULL)
        return FALSE;

    if (wm_sdk_file_get_size(f, size) != WM_SDK_RESULT_SUCCESS || *size == 0u)
    {
        wm_sdk_file_close(f);
        return FALSE;
    }

    wm_sdk_file_close(f);
    return TRUE;
}

/* Scan briefly so AT+CONN has a fresh record, then open an SMP link. */
static wm_SdkResult prv_dfu_smp_connect(const wm_dfu_target_t *t, int *cid)
{
    const wm_SdkBleScanParam scan = { t->scan_interval, t->scan_window };
    wm_SdkBleScanParam       saved;
    wm_SdkResult             sr;
    UINT8                    type = t->addr_type;
    int                      got = -1;

    *cid = -1;

    if (wm_sdk_ble_scan_get_param(&saved) != WM_SDK_RESULT_SUCCESS)
    {
        saved.interval = 96u;
        saved.window   = 48u;
    }

    wm_sdk_ble_scan_stop();
    s_scanning   = FALSE;
    s_watch_seen = FALSE;
    s_watch_addr = t->addr;

    if (wm_sdk_ble_scan_set_param(&scan) == WM_SDK_RESULT_SUCCESS &&
        wm_sdk_ble_scan_clear_filter() == WM_SDK_RESULT_SUCCESS &&
        wm_sdk_ble_scan_start(WM_SDK_BLE_SCAN_RAW) == WM_SDK_RESULT_SUCCESS)
    {
        wm_sdk_task_sleep(WM_BLE_SCAN_SETTLE_MS);
        wm_sdk_ble_scan_stop();
    }
    (void)wm_sdk_ble_scan_set_param(&saved);
    s_watch_addr = NULL;

    if (!s_watch_seen)
    {
        wm_printf("not seen in %lu ms of scanning - trying to connect anyway\r\n",
                  (unsigned long)WM_BLE_SCAN_SETTLE_MS);
    }
    else if (s_watch_type != t->addr_type)
    {
        /* The configured type is a guess; a connect with the wrong one fails. */
        type = s_watch_type;
        wm_printf("seen as %s, not %s - connecting as seen\r\n",
                  (s_watch_type == WM_SDK_BLE_ADDR_RANDOM) ? "random" : "public",
                  (t->addr_type == WM_SDK_BLE_ADDR_RANDOM) ? "random" : "public");
    }

    sr = wm_sdk_ble_connect(t->addr, type, &got);
    if (sr != WM_SDK_RESULT_SUCCESS)
        return sr;

    s_smp_cid = got;

    /* Never report failure with a link still up: the caller would lose it. */
    sr = wm_ui_smp_open(got);
    if (sr != WM_SDK_RESULT_SUCCESS)
    {
        wm_sdk_ble_disconnect(got);
        s_smp_cid = -1;
        return sr;
    }

    *cid = got;
    return WM_SDK_RESULT_SUCCESS;
}

/* Sends what the menu handler already downloaded and verified, over SMP. */
/* Every unit updated this way runs the same sequence, so only *t changes. */
static void prv_dfu_job(void)
{
    const wm_dfu_target_t *t = s_dfu;
    UINT8        hash[WM_SMP_HASH_LEN];
    UINT8        live[WM_SMP_HASH_LEN];
    UINT32       size = 0u;
    UINT32       t0;
    int          cid = -1;
    int          try;
    wm_SdkResult sr;

    if (t == NULL)
        return;

    wm_printf("\r\n[DFU] --- %s: %s -> %s ---\r\n", t->name, t->path, t->addr);

    if (!prv_dfu_fw_size(t->path, &size))
    {
        wm_printf("no image at %s - download it with this option first\r\n",
                  t->path);
        return;
    }
    wm_printf("image %lu bytes\r\n", (unsigned long)size);

    /* The transfer owns the radio for minutes, so the monitor is stopped. */
    s_mon_run = FALSE;
    wm_sdk_ble_scan_stop();
    s_scanning = FALSE;
    wm_sdk_ble_disconnect_all();
    s_ag_cid  = -1;
    s_ag3_cid = -1;

    /* The target is global and latched at connect, so it is set before it. */
    sr = wm_sdk_ble_set_target(WM_SMP_SERVICE_UUID, WM_SMP_CHAR_UUID);
    if (sr != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("cannot target the SMP characteristic rc=%ld\r\n", (long)sr);
        return;
    }

    s_smp_cid  = -1;
    s_smp_mode = TRUE;

    /* A failure here usually means the radio would not accept the SMP target. */
    sr = prv_dfu_smp_connect(t, &cid);
    if (sr != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("connect to the SMP service failed rc=%ld\r\n", (long)sr);
        goto done;
    }
    wm_printf("connected on cid %d\r\n", cid);

    /* A short command first: it proves the framing before a long transfer. */
    sr = wm_ui_smp_echo(WM_BLE_SMP_TIMEOUT);
    if (sr != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("SMP echo failed rc=%ld - the peer is not answering SMP\r\n", (long)sr);
        goto done;
    }
    wm_printf("SMP echo ok - uploading, do not power either unit off\r\n");

    t0 = wm_sdk_get_ticks();
    sr = wm_ui_smp_upload(t->path, size, WM_BLE_SMP_FIRST_MS);
    if (sr != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("upload failed rc=%ld - the %s still runs its old image\r\n",
                  (long)sr, t->name);
        goto done;
    }
    wm_printf("upload done in %lu ms\r\n", (unsigned long)(wm_sdk_get_ticks() - t0));

    /* The device reports the hash of what it stored, so nothing is guessed here. */
    sr = wm_ui_smp_image_hash(WM_BLE_SLOT_SECONDARY, hash, WM_BLE_SMP_TIMEOUT);
    if (sr != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("cannot read back the staged image rc=%ld\r\n", (long)sr);
        goto done;
    }

    /* Test, not confirm: if the new image cannot boot, MCUboot puts the old */
    /* one back by itself. The confirm below is what makes it permanent. */
    sr = wm_ui_smp_image_test(hash, WM_BLE_SMP_TIMEOUT);
    if (sr != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("marking the image for test failed rc=%ld\r\n", (long)sr);
        goto done;
    }
    wm_printf("image marked for test - resetting the %s\r\n", t->name);

    (void)wm_ui_smp_reset(WM_BLE_SMP_TIMEOUT);

    /* The link is gone the moment it reboots, and MCUboot swaps before it */
    /* advertises again, so everything is torn down before waiting. */
    wm_ui_smp_close();
    if (cid >= 0)
        wm_sdk_ble_disconnect(cid);
    cid       = -1;
    s_smp_cid = -1;

    wm_printf("waiting %lu s for the swap and reboot\r\n",
              (unsigned long)(WM_BLE_BOOT_WAIT_MS / 1000u));
    wm_sdk_task_sleep(WM_BLE_BOOT_WAIT_MS);

    for (try = 1; try <= WM_BLE_RECONNECT_TRIES; try++)
    {
        wm_printf("reconnecting (%d/%d)\r\n", try, WM_BLE_RECONNECT_TRIES);
        if (prv_dfu_smp_connect(t, &cid) == WM_SDK_RESULT_SUCCESS)
            break;
        cid = -1;
    }

    if (cid < 0)
    {
        wm_printf("could not reconnect - the new image is running on TEST only "
                  "and will revert at the next reboot; re-run this option to "
                  "confirm it\r\n");
        goto done;
    }

    /* A NULL hash confirms whatever is running now, which is the new image. */
    sr = wm_ui_smp_image_confirm(NULL, WM_BLE_SMP_TIMEOUT);
    if (sr != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("confirm failed rc=%ld - it will revert at the next reboot\r\n",
                  (long)sr);
        goto done;
    }

    /* Proof rather than assumption: the primary slot must hold what was sent. */
    if (wm_ui_smp_image_hash(WM_BLE_SLOT_PRIMARY, live,
                             WM_BLE_SMP_TIMEOUT) == WM_SDK_RESULT_SUCCESS &&
        memcmp(live, hash, sizeof(hash)) == 0)
    {
        wm_printf("confirmed - the %s is running the new image\r\n", t->name);
    }
    else
    {
        wm_printf("confirmed, but the running image does not match what was "
                  "sent - read the %s back before trusting it\r\n", t->name);
    }

done:
    wm_ui_smp_close();
    s_smp_mode = FALSE;
    s_smp_cid  = -1;

    if (cid >= 0)
        wm_sdk_ble_disconnect(cid);

    /* Hand the radio back to the Autoguard demos. */
    (void)wm_sdk_ble_set_target(WM_AG_SERVICE, WM_AG_CHAR);
}

/*******************************************************************************
** Monitor task - keeps the blocking work off the menu dispatcher.
******************************************************************************/
static void prv_monitor_task(void *arg)
{
    (void)arg;

    while (1)
    {
        if (s_job != WM_BLE_JOB_NONE)
        {
            if (prv_ble_ready())
            {
                if (s_job == WM_BLE_JOB_GATT)
                    prv_gatt_job();
                else if (s_job == WM_BLE_JOB_AG3_HEALTH)
                    prv_ag3_job();
                else if (s_job == WM_BLE_JOB_DFU)
                    prv_dfu_job();
                else
                    prv_fw_job();
            }
            s_job = WM_BLE_JOB_NONE;
            continue;
        }

        if (!s_mon_run)
        {
            wm_sdk_task_sleep(200);
            continue;
        }

        /* Also recovers after a module reset (see prv_evt_cb). */
        if (!prv_ble_ready())
        {
            wm_sdk_task_sleep(WM_BLE_PERIOD_MS);
            continue;
        }
        if (!s_scanning)
        {
            if (!prv_scan_start())
            {
                wm_sdk_task_sleep(WM_BLE_PERIOD_MS);
                continue;
            }
            /* AT+CONN expects the peer to have been seen by the scan first. */
            wm_sdk_task_sleep(WM_BLE_SCAN_SETTLE_MS);
        }

        wm_printf("\r\n[BLEMON] --- cycle ---\r\n");
        prv_fuel_print();          /* scanning never stopped, so this is live */
        /* Keep the link up between cycles. */
        prv_health_exchange(WM_BLE_AG_ADDR, WM_BLE_AG_ADDR_TYPE, &s_ag_cid, TRUE);

        wm_sdk_task_sleep(WM_BLE_PERIOD_MS);
    }
}

static BOOL prv_mon_task_ensure(void)
{
    if (s_mon_task != NULL)
        return TRUE;

    s_mon_task = wm_sdk_task_create(prv_monitor_task, NULL, "BLEMON", NULL,
                                 WM_BLE_MON_TASK_STACK, TP_TIMED_ACTIVITY);
    if (s_mon_task == NULL)
    {
        wm_printf("monitor task create failed\r\n");
        return FALSE;
    }
    return TRUE;
}

static void prv_job_post(wm_ble_job_e job)
{
    if (!prv_ble_ready())
        return;
    if (s_job != WM_BLE_JOB_NONE)
    {
        wm_printf("another BLE job is still running\r\n");
        return;
    }
    if (!prv_mon_task_ensure())
        return;

    s_job = job;
    wm_printf("started - results follow\r\n");
}

/*******************************************************************************
** Firmware staging - prompts and HTTPS, so it runs on the menu dispatcher
******************************************************************************/
/* One console line with its newline stripped; the caller frees msg.arg3. */
static SIM_MSG_T prv_fw_prompt(const char *what)
{
    SIM_MSG_T msg;

    wm_printf("%s", what);

    msg = GetParamFromUart();

    if (msg.arg3 != NULL)
        (void)strtok((char *)msg.arg3, "\r\n");

    return msg;
}

/* Why a download request failed: the server's verdict, then the transport code. */
static void prv_fw_explain(const char *what)
{
    wm_printf("%s failed: http=%ld err=%ld\r\n", what,
              (long)wm_sdk_https_get_status_code(WM_SDK_HTTPS_DOWNLOAD_INDEX),
              (long)wm_sdk_https_get_last_error(WM_SDK_HTTPS_DOWNLOAD_INDEX));
    wm_printf("(err -1 connect/TLS, -2 closed by peer, -4 protocol, "
              "-5 DNS, -6 bad URL, -7 out of memory)\r\n");
}

/* Fetch 'url' into 'path' by ranges; TRUE only if the whole image landed. */
static BOOL prv_fw_download(const char *url, const char *path, const char *volume)
{
    char        *chunk;
    void        *f;
    INT64        freeb = 0;
    UINT32       size = 0u, off = 0u, n = 0u, written = 0u;
    wm_SdkResult rc;
    BOOL         complete;

    wm_printf("-> %s\r\n", path);

    if (!gf_pdp_ready)
        wm_printf("note: no PDP context is up, so this will fail\r\n");

    if (wm_sdk_https_init(WM_SDK_HTTPS_MODE_SYNC, NULL) != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("https init failed (a request may still be in flight)\r\n");
        return FALSE;
    }

    wm_printf("tls: server verification %s\r\n", (s_fw_ca != NULL) ? "on" : "OFF");

    if (wm_sdk_https_configure_ssl(WM_SDK_HTTPS_DOWNLOAD_INDEX, s_fw_ca, NULL, NULL)
            != WM_SDK_RESULT_SUCCESS ||
        wm_sdk_https_set_params(WM_SDK_HTTPS_DOWNLOAD_INDEX, url, WM_BLE_FW_TIMEOUT)
            != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("download session setup failed (is the URL too long?)\r\n");
        wm_sdk_https_terminate(WM_SDK_HTTPS_DOWNLOAD_INDEX);
        return FALSE;
    }

    /* Size first, so nothing is written before the volume is known to hold it. */
    if (wm_sdk_https_download_get_file_size(&size) != 0 || size == 0u)
    {
        prv_fw_explain("size query");
        wm_printf("the URL may be wrong, or the server may not honour ranged "
                  "requests, which this download depends on\r\n");
        wm_sdk_https_terminate(WM_SDK_HTTPS_DOWNLOAD_INDEX);
        return FALSE;
    }

    /* The digest covers whatever sits at the path, so a stale image cannot stay. */
    (void)wm_sdk_file_delete(path);

    rc = wm_sdk_file_get_disk_info(volume, NULL, &freeb, NULL);
    if (rc != WM_SDK_RESULT_SUCCESS)
    {
        wm_printf("%s unavailable (rc=%ld) - this build carries no external flash\r\n",
                  volume, (long)rc);
        wm_sdk_https_terminate(WM_SDK_HTTPS_DOWNLOAD_INDEX);
        return FALSE;
    }

    wm_printf("image %lu bytes, %s free %ld bytes\r\n", (unsigned long)size,
              volume, (long)freeb);

    if ((INT64)size > freeb)
    {
        wm_printf("not enough room on %s for this image\r\n", volume);
        wm_sdk_https_terminate(WM_SDK_HTTPS_DOWNLOAD_INDEX);
        return FALSE;
    }

    /* Heap, not BSS: the buffer is only wanted for the length of a transfer. */
    chunk = (char *)wm_sdk_memory_alloc(WM_BLE_FW_CHUNK);
    if (chunk == NULL)
    {
        wm_printf("chunk buffer alloc (%lu bytes) failed\r\n",
                  (unsigned long)WM_BLE_FW_CHUNK);
        wm_sdk_https_terminate(WM_SDK_HTTPS_DOWNLOAD_INDEX);
        return FALSE;
    }

    f = wm_sdk_file_open(path, "wb+");
    if (f == NULL)
    {
        wm_printf("cannot open %s for writing\r\n", path);
        wm_sdk_memory_free(chunk);
        wm_sdk_https_terminate(WM_SDK_HTTPS_DOWNLOAD_INDEX);
        return FALSE;
    }

    /* Each chunk goes straight out to flash, so the image is never held in RAM. */
    while (off < size)
    {
        UINT32 want = ((size - off) < WM_BLE_FW_CHUNK) ? (size - off) : WM_BLE_FW_CHUNK;

        if (wm_sdk_https_download_read_chunk(off, want, chunk, &n) != 0 || n == 0u)
        {
            wm_printf("chunk at offset %lu ", (unsigned long)off);
            prv_fw_explain("read");
            break;
        }

        if (wm_sdk_file_write(f, chunk, n, &written) != WM_SDK_RESULT_SUCCESS || written != n)
        {
            wm_printf("write failed at offset %lu (%lu of %lu bytes)\r\n",
                      (unsigned long)off, (unsigned long)written, (unsigned long)n);
            break;
        }

        off += n;
        wm_printf("  %lu/%lu bytes\r\n", (unsigned long)off, (unsigned long)size);
    }

    complete = (off == size) ? TRUE : FALSE;

    wm_sdk_file_sync(f);
    wm_sdk_file_close(f);
    wm_sdk_memory_free(chunk);
    wm_sdk_https_terminate(WM_SDK_HTTPS_DOWNLOAD_INDEX);

    if (!complete)
    {
        wm_printf("download incomplete: %lu of %lu bytes - not verifying\r\n",
                  (unsigned long)off, (unsigned long)size);
        return FALSE;
    }

    wm_printf("saved to %s\r\n", path);
    return TRUE;
}

/* Hash the staged image: the only check standing between it and the radio. */
static BOOL prv_fw_verify(const char *path, const char *expected)
{
    if (strlen(expected) != (size_t)WM_SDK_OTA_SHA256_HEX_LEN)
    {
        wm_printf("not checked: \"%s\" is not %u hex chars\r\n", expected,
                  (unsigned)WM_SDK_OTA_SHA256_HEX_LEN);
        return FALSE;
    }

    wm_printf("expected = %s\r\n", expected);
    wm_printf("computed = ");

    /* Prints the digest it computed and its own verdict line. */
    return wm_compare_sha256((char *)path, (char *)expected);
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

void wm_ui_ble_monitor_demo(void)
{
    wm_printf("\r\n--- BLE: Periodic read ---\r\n");

    if (!prv_ble_ready())
        return;

    /* Scanning is started by the monitor task, which waits for the Autoguard to
     * be scanned before its first connect. */
    if (!prv_mon_task_ensure())
        return;

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
    wm_sdk_ble_disconnect_all();
    wm_sdk_ble_set_scan_callback(NULL);
    wm_sdk_ble_set_data_callback(NULL);
    wm_sdk_ble_set_event_callback(NULL);

    wm_printf("power off -> rc=%ld\r\n", (long)wm_sdk_ble_set_power_status(0));

    /* Force the next option through prv_ble_ready() to power back up. */
    s_ready    = FALSE;
    s_scanning = FALSE;
    s_ag_cid   = -1;
    s_ag3_cid  = -1;
    s_rx_len   = 0u;
    memset(s_fuel, 0, sizeof(s_fuel));
}

void wm_ui_ble_info_demo(void)
{
    char               buf[48];
    char               flt[WM_SDK_BLE_FILTER_MAX][WM_SDK_BLE_ADDR_STR_LEN];
    UINT32             baud = 0u;
    INT8               dbm = 0;
    BOOL               lp = FALSE;
    UINT8              mask = 0u;
    UINT8              n = 0u;
    UINT8              i;
    wm_SdkBleConnParam cp;
    wm_SdkBleScanParam sp;
    wm_SdkResult       sr;

    wm_printf("\r\n--- BLE: Module info ---\r\n");

    if (!prv_ble_ready())
        return;

    memset(buf, 0, sizeof(buf));
    sr = wm_sdk_ble_get_version(buf, sizeof(buf));
    wm_printf("version   : %s (rc=%ld)\r\n", (sr == WM_SDK_RESULT_SUCCESS) ? buf : "-", (long)sr);

    memset(buf, 0, sizeof(buf));
    sr = wm_sdk_ble_get_address(buf, sizeof(buf));
    wm_printf("address   : %s (rc=%ld)\r\n", (sr == WM_SDK_RESULT_SUCCESS) ? buf : "-", (long)sr);

    sr = wm_sdk_ble_get_baud(&baud);
    wm_printf("baud      : %lu (rc=%ld)\r\n", (unsigned long)baud, (long)sr);

    sr = wm_sdk_ble_get_tx_power(&dbm);
    wm_printf("tx power  : %d dBm (rc=%ld)\r\n", (int)dbm, (long)sr);

    sr = wm_sdk_ble_get_low_power(&lp);
    wm_printf("low power : %s (rc=%ld)\r\n", lp ? "allowed" : "off", (long)sr);

    memset(&cp, 0, sizeof(cp));
    sr = wm_sdk_ble_get_conn_param(&cp);
    wm_printf("conn param: interval %u-%u latency %u timeout %u (x1.25 ms) (rc=%ld)\r\n",
              (unsigned)cp.interval_min, (unsigned)cp.interval_max,
              (unsigned)cp.latency, (unsigned)cp.timeout, (long)sr);

    memset(&sp, 0, sizeof(sp));
    sr = wm_sdk_ble_scan_get_param(&sp);
    wm_printf("scan param: interval %u window %u (x1.25 ms) (rc=%ld)\r\n",
              (unsigned)sp.interval, (unsigned)sp.window, (long)sr);

    sr = wm_sdk_ble_get_link_mask(&mask);
    wm_printf("links     : cid0=%s cid1=%s cid2=%s (rc=%ld)\r\n",
              (mask & 0x01u) ? "up" : "-", (mask & 0x02u) ? "up" : "-",
              (mask & 0x04u) ? "up" : "-", (long)sr);

    sr = wm_sdk_ble_scan_get_filter(flt, (UINT8)WM_SDK_BLE_FILTER_MAX, &n);
    wm_printf("filter    : %u address(es) on the module (rc=%ld)\r\n", (unsigned)n, (long)sr);
    for (i = 0u; sr == WM_SDK_RESULT_SUCCESS && i < n && i < (UINT8)WM_SDK_BLE_FILTER_MAX; i++)
        wm_printf("            %s\r\n", flt[i]);
}

void wm_ui_ble_gatt_demo(void)
{
    wm_printf("\r\n--- BLE: GATT discovery ---\r\n");
    prv_job_post(WM_BLE_JOB_GATT);
}

void wm_ui_ble_fw_update_demo(void)
{
    SIM_MSG_T msg;
    char      expected[WM_SDK_OTA_SHA256_HEX_LEN + 1] = {0};

    wm_printf("\r\n--- BLE: Firmware update (download + SHA-256) ---\r\n");

    /* Check the radio first: a download is minutes to spend on a dead module. */
    if (!prv_ble_ready())
        return;

    wm_printf("NOTE: uncheck \"send with \\r\\n\" in the SPT tool before sending "
              "the link and the SHA256\r\n\r\n");

    /* --- 1. URL ---------------------------------------------------------- */
    msg = prv_fw_prompt("Enter download link of the BLE firmware image: ");
    if (msg.arg3 == NULL)
    {
        wm_printf("\r\nno link entered\r\n");
        return;
    }
    wm_printf("\r\n%s\r\n", (char *)msg.arg3);

    /* --- 2. Download ----------------------------------------------------- */
    if (!prv_fw_download((const char *)msg.arg3, WM_BLE_FW_PATH, WM_BLE_FW_VOLUME))
    {
        wm_sdk_memory_free(msg.arg3);
        return;
    }
    wm_sdk_memory_free(msg.arg3);

    /* --- 3. Expected digest ---------------------------------------------- */
    msg = prv_fw_prompt("Enter SHA256 of the image file: ");
    if (msg.arg3 == NULL)
    {
        wm_printf("\r\nno digest entered; the image is staged but not flashed\r\n");
        return;
    }
    strncpy(expected, (const char *)msg.arg3, sizeof(expected) - 1);
    wm_sdk_memory_free(msg.arg3);
    wm_printf("\r\n");

    /* --- 4. Verify ------------------------------------------------------- */
    if (!prv_fw_verify(WM_BLE_FW_PATH, expected))
    {
        wm_printf("refusing to flash an unverified image - radio untouched\r\n");
        (void)wm_sdk_file_delete(WM_BLE_FW_PATH);
        return;
    }

    /* --- 5. Flash, on BLEMON so it cannot overlap a monitor cycle --------- */
    prv_job_post(WM_BLE_JOB_FW_UPDATE);
}

void wm_ui_ble_ag3_demo(void)
{
    wm_printf("\r\n--- BLE: AG 3.0 health packet ---\r\n");
    prv_job_post(WM_BLE_JOB_AG3_HEALTH);
}

/* Stage an image for one DFU target, then hand it to the BLEMON task. */
/* Shared by every unit updated over SMP: only the target differs. */
static void prv_dfu_menu(const wm_dfu_target_t *t)
{
    SIM_MSG_T msg;
    char      expected[WM_SDK_OTA_SHA256_HEX_LEN + 1] = {0};

    /* Check the radio first: a download is minutes to spend on a dead module. */
    if (!prv_ble_ready())
        return;

    wm_printf("NOTE: uncheck \"send with \\r\\n\" in the SPT tool before sending "
              "the link and the SHA256\r\n\r\n");

    /* --- 1. URL, or keep what a previous run already staged --------------- */
    if (wm_sdk_file_exists(t->path) == WM_SDK_RESULT_SUCCESS)
    {
        UINT32 staged = 0u;

        if (prv_dfu_fw_size(t->path, &staged))
            wm_printf("a staged image is already present (%lu bytes)\r\n",
                      (unsigned long)staged);
        msg = prv_fw_prompt("Enter a link to replace it, or 'k' to keep it: ");
    }
    else
    {
        /* The image file differs per unit, so it is named before the prompt. */
        wm_printf("%s image file: %s\r\n", t->name, t->image);
        msg = prv_fw_prompt("Enter download link of the image: ");
    }

    if (msg.arg3 == NULL)
    {
        wm_printf("\r\nno link entered\r\n");
        return;
    }
    wm_printf("\r\n%s\r\n", (char *)msg.arg3);

    /* --- 2. Download, unless the staged image is being reused ------------- */
    if (strcmp((const char *)msg.arg3, "k") == 0)
    {
        wm_printf("keeping %s\r\n", t->path);
    }
    else if (!prv_fw_download((const char *)msg.arg3, t->path, t->volume))
    {
        wm_sdk_memory_free(msg.arg3);
        return;
    }
    wm_sdk_memory_free(msg.arg3);

    /* --- 3. Expected digest ---------------------------------------------- */
    /* This is the digest of the whole file, not the MCUboot image hash. */
    msg = prv_fw_prompt("Enter SHA256 of the image file: ");
    if (msg.arg3 == NULL)
    {
        wm_printf("\r\nno digest entered; the image is staged but not sent\r\n");
        return;
    }
    strncpy(expected, (const char *)msg.arg3, sizeof(expected) - 1);
    wm_sdk_memory_free(msg.arg3);
    wm_printf("\r\n");

    /* --- 4. Verify ------------------------------------------------------- */
    if (!prv_fw_verify(t->path, expected))
    {
        wm_printf("refusing to send an unverified image - %s untouched\r\n",
                  t->name);
        (void)wm_sdk_file_delete(t->path);
        return;
    }

    /* --- 5. DFU, on BLEMON so it cannot overlap a monitor cycle ----------- */
    /* Read by the job, which keeps its own pointer for the whole transfer. */
    s_dfu = t;
    prv_job_post(WM_BLE_JOB_DFU);
}

void wm_ui_ble_ag3_dfu_demo(void)
{
    wm_printf("\r\n--- BLE: AG 3.0 DFU (download + SHA-256 + SMP) ---\r\n");
    prv_dfu_menu(&s_dfu_ag3);
}

void wm_ui_ble_fuel_dfu_demo(void)
{
    wm_printf("\r\n--- BLE: Fuel sensor DFU (download + SHA-256 + SMP) ---\r\n");
    prv_dfu_menu(&s_dfu_fuel);
}
