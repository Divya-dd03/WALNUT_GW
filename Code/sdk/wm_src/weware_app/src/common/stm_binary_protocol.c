/**
 * @file stm_binary_protocol.c
 * @brief Binary UART framing for SIMCOM <-> STM32 (GW-STM-BIN-001).
 *        Stateless, table-free (no static RAM).
 */

#include "common/stm_binary_protocol.h"

/*---------------------------------------------------------------
 * Frame field offsets (SRC_LEN is variable, so CMD onward is computed).
 *
 *   [0] SOF1  [1] SOF2  [2] VER  [3] TYPE  [4] SRC_TYPE  [5] SRC_LEN
 *   [6 .. 6+SRC_LEN-1] SRC_ADDR
 *   [6+SRC_LEN] CMD  [7+SRC_LEN] LEN_H  [8+SRC_LEN] LEN_L
 *   [9+SRC_LEN .. ] PAYLOAD(LEN)  then CRC_H CRC_L
 *
 * CRC covers VER .. last payload byte = bytes [2 .. (9+SRC_LEN+LEN-1)].
 *--------------------------------------------------------------*/

/* CRC-16/IBM-SDLC: reflected poly 0x8408, init 0xFFFF, final XOR 0xFFFF. */
UINT16 stm_crc16(const UINT8 *data, UINT32 len)
{
    UINT16 fcs = 0xFFFFu;
    if (!data) {
        return (UINT16)(~fcs & 0xFFFFu);
    }
    for (UINT32 i = 0; i < len; i++) {
        fcs ^= (UINT16)data[i];
        for (int b = 0; b < 8; b++) {
            if (fcs & 1u)
                fcs = (UINT16)((fcs >> 1) ^ 0x8408u);
            else
                fcs = (UINT16)(fcs >> 1);
        }
    }
    return (UINT16)(~fcs & 0xFFFFu);
}

int stm_build_frame_src(UINT8 *out, int out_cap, UINT8 src_type,
                        const UINT8 *src_addr, UINT8 src_len,
                        UINT8 cmd, const UINT8 *payload, UINT16 payload_len)
{
    if (!out)
        return -1;
    if (src_len > STM_SRC_LEN_MAX)
        return -1;
    if (src_len > 0 && !src_addr)
        return -1;
    if (payload_len > STM_MAX_PAYLOAD)
        return -1;
    if (payload_len > 0 && !payload)
        return -1;

    int total = 9 + (int)src_len + (int)payload_len + 2;  /* + CRC */
    if (out_cap < total)
        return -1;

    out[0] = STM_SOF1;
    out[1] = STM_SOF2;
    out[2] = STM_VER;
    out[3] = STM_TYPE_STM;
    out[4] = src_type;
    out[5] = src_len;
    for (UINT8 i = 0; i < src_len; i++)
        out[6 + i] = src_addr[i];

    UINT32 c = 6u + (UINT32)src_len;                 /* index of CMD */
    out[c]     = cmd;
    out[c + 1] = (UINT8)((payload_len >> 8) & 0xFFu);  /* LEN_H */
    out[c + 2] = (UINT8)(payload_len & 0xFFu);         /* LEN_L */
    for (UINT16 i = 0; i < payload_len; i++)
        out[c + 3 + i] = payload[i];

    /* CRC over VER..last payload byte = out[2 ..] = 7 + src_len + payload_len bytes */
    UINT16 crc = stm_crc16(&out[2], 7u + (UINT32)src_len + (UINT32)payload_len);
    out[c + 3 + payload_len]     = (UINT8)((crc >> 8) & 0xFFu);  /* CRC_H */
    out[c + 3 + payload_len + 1] = (UINT8)(crc & 0xFFu);         /* CRC_L */

    return total;
}

int stm_build_frame(UINT8 *out, int out_cap, UINT8 src_type, UINT8 cmd,
                    const UINT8 *payload, UINT16 payload_len)
{
    /* SRC_LEN = 0 (no source address) — used by SIMCOM-internal commands. */
    return stm_build_frame_src(out, out_cap, src_type, NULL, 0, cmd, payload, payload_len);
}

int stm_frame_complete(const UINT8 *buf, UINT32 len)
{
    if (!buf)
        return STM_FRAME_NEED_MORE;

    if (len < 2u)
        return STM_FRAME_NEED_MORE;
    if (buf[0] != STM_SOF1 || buf[1] != STM_SOF2)
        return STM_FRAME_BAD;

    /* Need through SRC_LEN to know the header size. */
    if (len < 6u)
        return STM_FRAME_NEED_MORE;

    UINT8 src_len = buf[5];
    if (src_len > STM_SRC_LEN_MAX)
        return STM_FRAME_BAD;

    /* Need CMD + LEN_H + LEN_L. */
    UINT32 len_field_end = 9u + (UINT32)src_len;  /* index just past LEN_L */
    if (len < len_field_end)
        return STM_FRAME_NEED_MORE;

    UINT16 payload_len = (UINT16)(((UINT16)buf[7u + src_len] << 8) | buf[8u + src_len]);
    if (payload_len > STM_MAX_PAYLOAD)
        return STM_FRAME_BAD;

    UINT32 total = 9u + (UINT32)src_len + (UINT32)payload_len + 2u; /* + CRC */
    if (len < total)
        return STM_FRAME_NEED_MORE;

    /* CRC over VER..last payload byte = buf[2 .. total-3] = (total-4) bytes. */
    UINT16 calc = stm_crc16(&buf[2], total - 4u);
    UINT16 rx   = (UINT16)(((UINT16)buf[total - 2u] << 8) | buf[total - 1u]);
    if (calc != rx)
        return STM_FRAME_BAD;

    return (int)total;
}

UINT8 stm_frame_cmd(const UINT8 *buf)
{
    return buf[6u + buf[5]];
}

const UINT8 *stm_frame_payload(const UINT8 *buf)
{
    return &buf[9u + buf[5]];
}

UINT16 stm_frame_payload_len(const UINT8 *buf)
{
    UINT8 src_len = buf[5];
    return (UINT16)(((UINT16)buf[7u + src_len] << 8) | buf[8u + src_len]);
}

BOOL stm_protocol_selftest(void)
{
    /* Vector 1: CRC-16/IBM-SDLC self-check. */
    static const UINT8 check[] = { '1','2','3','4','5','6','7','8','9' };
    if (stm_crc16(check, sizeof(check)) != 0x906Eu)
        return FALSE;

    /* Vector 2: GET_DEV_INFO request (TCP src, no addr) -> CRC 0x7FAB.
     * Frame: AA 55 01 01 01 00 04 00 00 7F AB */
    UINT8 frame[STM_FRAME_MIN_SIZE];
    int n = stm_build_frame(frame, (int)sizeof(frame), STM_SRC_TYPE_TCP,
                            STM_CMD_GET_DEV_INFO, NULL, 0);
    if (n != (int)STM_FRAME_MIN_SIZE)
        return FALSE;
    if (frame[9] != 0x7Fu || frame[10] != 0xABu)
        return FALSE;

    /* Round-trip: the frame we built must validate. */
    if (stm_frame_complete(frame, (UINT32)n) != n)
        return FALSE;

    return TRUE;
}
