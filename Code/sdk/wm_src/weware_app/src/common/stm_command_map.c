/**
 * @file stm_command_map.c
 * @brief User-command ASCII -> STM binary translation (hybrid). Table-driven;
 *        SIMCOM owns the envelope, translates known commands, passthrough for new.
 */

#include "common/stm_command_map.h"
#include "common/stm_binary_protocol.h"
#include "common/utils.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/*---------------------------------------------------------------
 * Small parse helpers for argument encoders
 *--------------------------------------------------------------*/

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* "AA:BB:CC:DD:EE:FF" -> 6 bytes. Returns 1 on success, 0 on bad format. */
static int mac_to_bytes(const char *s, UINT8 mac[6])
{
    if (!s) return 0;
    for (int i = 0; i < 6; i++) {
        int hi = hex_nibble(s[0]);
        int lo = (hi >= 0) ? hex_nibble(s[1]) : -1;
        if (hi < 0 || lo < 0) return 0;
        mac[i] = (UINT8)((hi << 4) | lo);
        s += 2;
        if (i < 5) {
            if (*s != ':') return 0;
            s++;
        }
    }
    return 1;
}

/* Parse a 1-byte device id from the first arg token. lo..hi inclusive. -1 on error. */
static int parse_dev_id(const char *args, int lo, int hi)
{
    if (!args || !*args) return -1;
    int v = atoi(args);
    if (v < lo || v > hi) return -1;
    return v;
}

/*---------------------------------------------------------------
 * Argument encoders (ASCII args -> binary payload).
 * Each is validated against the document's byte-level CRC vector.
 *--------------------------------------------------------------*/

/* del-ble-peri-info,<id> / get-peri-info,<id> : DEV_ID(1), slots 1..5. */
static int enc_dev_id_1_5(const char *args, UINT8 *p, int cap)
{
    if (cap < 1) return -1;
    int id = parse_dev_id(args, 1, 5);
    if (id < 0) return -1;
    p[0] = (UINT8)id;
    return 1;
}

/* get-ble-peri-info : no argument -> 0x00 = all devices. An explicit id must be
   1..5; 0, >5 or negative -> 0xFF so the STM replies "invalid device id". */
static int enc_dev_id_get_peri(const char *args, UINT8 *p, int cap)
{
    if (cap < 1) return -1;
    if (!args || !*args) { p[0] = 0x00u; return 1; }   /* no arg -> all */
    int id = atoi(args);
    p[0] = (id >= 1 && id <= 5) ? (UINT8)id : 0xFFu;    /* 1..5 slot; else invalid */
    return 1;
}

/* set-gsm-status,<0|1> : GSM_STATUS(1). */
static int enc_gsm_status(const char *args, UINT8 *p, int cap)
{
    if (cap < 1 || !args || !*args) return -1;
    p[0] = (atoi(args) != 0) ? 0x01u : 0x00u;
    return 1;
}

/* "true"/"false" (or "1"/"0") -> 1/0; -1 on bad token. */
static int parse_bool(const char *s)
{
    if (!s) return -1;
    if (strncasecmp(s, "true", 4) == 0)  return 1;
    if (strncasecmp(s, "false", 5) == 0) return 0;
    if (s[0] == '1') return 1;
    if (s[0] == '0') return 0;
    return -1;
}

/* set-ble-peri-info,<id>,<mac>,<name>,<interval_sec>,<log>,<gps>,<type> :
 *   DEV_ID(1)+MAC(6)+NAME_LEN(1)+NAME+INTERVAL(4 BE, seconds)+OFFLINE(1)+WITH_GPS(1)+DEV_TYPE(1).
 *   <type> true=AG(0x33, connectable), false=FUEL(0x34, advertise-only). */
static int enc_set_ble_peri(const char *args, UINT8 *p, int cap)
{
    if (!args) return -1;

    /* Locate the 7 comma-separated fields. */
    const char *f[7];
    int nf = 1;
    f[0] = args;
    for (const char *q = args; *q && nf < 7; q++) {
        if (*q == ',') f[nf++] = q + 1;
    }
    if (nf < 7) return -1;

    int id = parse_dev_id(f[0], 1, 5);
    if (id < 0) return -1;

    UINT8 mac[6];
    if (!mac_to_bytes(f[1], mac)) return -1;

    const char *name     = f[2];
    const char *name_end = strchr(name, ',');
    if (!name_end) return -1;
    int name_len = (int)(name_end - name);
    if (name_len <= 0 || name_len > 15) return -1;   /* doc: NAME max 15 */

    UINT32 interval = (UINT32)strtoul(f[3], NULL, 10);  /* seconds, sent as-is */

    int log_b  = parse_bool(f[4]);
    int gps_b  = parse_bool(f[5]);
    int type_b = parse_bool(f[6]);                      /* true=AG, false=FUEL */
    if (log_b < 0 || gps_b < 0 || type_b < 0) return -1;

    int need = 1 + 6 + 1 + name_len + 4 + 1 + 1 + 1;
    if (cap < need) return -1;

    int off = 0;
    p[off++] = (UINT8)id;
    for (int i = 0; i < 6; i++) p[off++] = mac[i];
    p[off++] = (UINT8)name_len;
    memcpy(&p[off], name, (size_t)name_len);
    off += name_len;
    p[off++] = (UINT8)((interval >> 24) & 0xFFu);
    p[off++] = (UINT8)((interval >> 16) & 0xFFu);
    p[off++] = (UINT8)((interval >> 8)  & 0xFFu);
    p[off++] = (UINT8)(interval & 0xFFu);
    p[off++] = log_b  ? 0x01u : 0x00u;
    p[off++] = gps_b  ? 0x01u : 0x00u;
    p[off++] = type_b ? 0x33u : 0x34u;
    return off;
}

/* peri-cmd,<id>,<mac>,<hex_char_len>,<hexdata> : DEV_ID(1)+MAC(6)+DATA_LEN(2)+DATA.
 *   <mac> may be empty (resolve by id -> MAC sent as zeros).
 *   <hex_char_len> is the hex-char count (informational; the actual decoded length
 *   is recomputed). <hexdata> may contain spaces.
 * Example: "5,,46,78780311...0A" -> id 5, no MAC, 23 data bytes. */
static int enc_peri_cmd(const char *args, UINT8 *p, int cap)
{
    if (!args) return -1;

    int id = parse_dev_id(args, 0, 5);   /* 0 = resolve by MAC */
    if (id < 0) return -1;

    const char *c1 = strchr(args, ',');          /* after id */
    if (!c1) return -1;
    const char *mac_s = c1 + 1;                  /* MAC field (may be empty) */
    const char *c2 = strchr(mac_s, ',');         /* after MAC */
    if (!c2) return -1;

    UINT8 mac[6] = {0};
    if (c2 > mac_s) {                            /* non-empty MAC -> parse it */
        if (!mac_to_bytes(mac_s, mac)) return -1;
    }

    const char *lenf = c2 + 1;                   /* hex_char_len field (ignored) */
    const char *c3 = strchr(lenf, ',');          /* after length */
    if (!c3) return -1;
    const char *hex = c3 + 1;                    /* hex data */

    if (cap < 1 + 6 + 2) return -1;
    int off = 0;
    p[off++] = (UINT8)id;
    for (int i = 0; i < 6; i++) p[off++] = mac[i];
    int len_field = off;          /* DATA_LEN goes here (2 bytes) */
    off += 2;

    /* hex-decode the data, skipping spaces, into p[off..] */
    UINT16 dlen = 0;
    int hi = -1;
    for (const char *q = hex; *q && *q != '\r' && *q != '\n'; q++) {
        if (*q == ' ' || *q == '\t') continue;
        int nib = hex_nibble(*q);
        if (nib < 0) return -1;
        if (hi < 0) {
            hi = nib;
        } else {
            if (off >= cap) return -1;
            p[off++] = (UINT8)((hi << 4) | nib);
            dlen++;
            hi = -1;
        }
    }
    if (hi >= 0) return -1;       /* odd number of hex chars */
    p[len_field]     = (UINT8)((dlen >> 8) & 0xFFu);
    p[len_field + 1] = (UINT8)(dlen & 0xFFu);
    return off;
}

/*---------------------------------------------------------------
 * Command table
 *
 * Each entry maps an ASCII command name to its STM opcode. `encode` builds the
 * binary payload from the ASCII argument list (the text after the first comma);
 * NULL means the command carries no payload (envelope only).
 *
 * Payload-bearing commands (set-ble-peri-info, get-ble-peri-info, del-ble-peri-info,
 * peri-cmd, get-peri-info, set-gsm-status) are added in the per-command phase, each
 * validated against the doc's byte-level example.
 *--------------------------------------------------------------*/

/* args = text after the command name's comma (NULL if none). Returns payload
 * length written to `payload` (>=0), or -1 on parse/overflow error. */
typedef int (*stm_arg_encoder_fn)(const char *args, UINT8 *payload, int cap);

typedef struct {
    const char        *name;     /* command name (token before first comma) */
    UINT8              opcode;    /* STM request opcode (bit7=0) */
    stm_arg_encoder_fn encode;   /* NULL => payload-less command */
} StmCmdEntry;

static const StmCmdEntry k_cmd_table[] = {
    /* ---- payload-less commands (envelope only) ---- */
    { "set-stm-reboot",        0x01u, NULL },
    { "system-restart",        0x02u, NULL },
    { "set-stm-factory-reset", 0x03u, NULL },
    { "get-device-info",       0x04u, NULL },
    { "get-reboot-info",       0x05u, NULL },
    { "system_alive",          0x06u, NULL },
    { "set-ble-scan-on",       0x20u, NULL },
    { "set-ble-scan-off",      0x21u, NULL },
    { "get-ag-data",           0x26u, NULL },
    { "get-fuel-data",         0x27u, NULL },
    { "get-ble-data",          0x32u, NULL },
    { "get-peri-link",         0x29u, NULL },
    /* ---- payload-bearing commands (validated against the doc's CRC vectors) ---- */
    { "set-ble-peri-info",     0x22u, enc_set_ble_peri },
    { "get-ble-peri-info",     0x23u, enc_dev_id_get_peri },
    { "del-ble-peri-info",     0x24u, enc_dev_id_1_5 },
    { "peri-cmd",              0x25u, enc_peri_cmd   },
    { "set-ble-peri-cmd",      0x25u, enc_peri_cmd   },  /* doc names it both ways */
    { "get-peri-info",         0x28u, enc_dev_id_1_5 },
    { "set-gsm-status",        0x31u, enc_gsm_status  },
    /* INTERVAL for set-ble-peri-info is sent in SECONDS (per the user spec + the
     * 0x22 field description). The doc's §11.9 example uses ms — confirm on HW. */
};

#define STM_CMD_TABLE_COUNT  (sizeof(k_cmd_table) / sizeof(k_cmd_table[0]))

/* Copy the command name (up to first ',' or end) into buf; returns name length. */
static size_t cmd_name_token(const char *ascii_cmd, char *buf, size_t cap)
{
    size_t n = 0;
    while (ascii_cmd[n] != '\0' && ascii_cmd[n] != ',' && n < cap - 1u) {
        buf[n] = ascii_cmd[n];
        n++;
    }
    buf[n] = '\0';
    return n;
}

int stm_cmd_build_from_ascii(const char *ascii_cmd, UINT8 src_type,
                             const UINT8 *src_addr, UINT8 src_len,
                             UINT8 *out, int out_cap)
{
    if (!ascii_cmd || !out)
        return STM_CMD_BUILD_ERR;

    char name[40];
    (void)cmd_name_token(ascii_cmd, name, sizeof(name));

    const StmCmdEntry *e = NULL;
    for (size_t i = 0; i < STM_CMD_TABLE_COUNT; i++) {
        if (strcmp(name, k_cmd_table[i].name) == 0) {
            e = &k_cmd_table[i];
            break;
        }
    }
    if (!e)
        return STM_CMD_UNKNOWN;    /* caller may try passthrough or reject */

    UINT8  payload[STM_MAX_PAYLOAD];
    UINT16 plen = 0;
    if (e->encode) {
        const char *comma = strchr(ascii_cmd, ',');
        const char *args  = comma ? comma + 1 : NULL;
        int pn = e->encode(args, payload, (int)sizeof(payload));
        if (pn < 0)
            return STM_CMD_BUILD_ERR;
        plen = (UINT16)pn;
    }

    int flen = stm_build_frame_src(out, out_cap, src_type, src_addr, src_len,
                                   e->opcode, (plen > 0) ? payload : NULL, plen);
    return (flen > 0) ? flen : STM_CMD_BUILD_ERR;
}

int stm_cmd_try_hex_passthrough(const char *body, UINT8 *out, int out_cap)
{
    /* Skeleton stub: full raw-hex passthrough (decode hex body -> frame to forward)
     * lands with the per-command phase. Returns 0 = not a passthrough for now. */
    (void)body;
    (void)out;
    (void)out_cap;
    return 0;
}
