/**
 * @file stm_binary_protocol.h
 * @brief Binary UART framing for SIMCOM <-> STM32 (GW-STM-BIN-001).
 *
 * Frame envelope (request and response identical):
 *   AA 55 | VER | TYPE | SRC_TYPE | SRC_LEN | SRC_ADDR(SRC_LEN) |
 *   CMD | LEN_H LEN_L | PAYLOAD(LEN) | CRC_H CRC_L
 *
 * - CRC: CRC-16/IBM-SDLC over VER..last-payload-byte (SOF excluded), big-endian.
 * - CMD bit7=0 request, bit7=1 response (request | 0x80).
 * - LEN big-endian. Frames built here always use SRC_LEN=0 (internal commands
 *   carry no source address); the parser still handles SRC_LEN>0 generically.
 *
 * Stateless / table-free (no static RAM).
 */

#ifndef STM_BINARY_PROTOCOL_H
#define STM_BINARY_PROTOCOL_H

#include "common/types.h"

/* Frame markers / fixed header values */
#define STM_SOF1            0xAAu
#define STM_SOF2            0x55u
#define STM_VER             0x01u
#define STM_TYPE_STM        0x01u   /* TYPE: target = STM32 */

/* SRC_TYPE (source channel; STM echoes it back unchanged) */
#define STM_SRC_TYPE_TCP    0x01u
#define STM_SRC_TYPE_SMS    0x02u
#define STM_SRC_TYPE_BLE    0x03u
#define STM_SRC_TYPE_LOCAL  0x04u

/* Response bit: response CMD = request CMD | STM_CMD_RESP_BIT */
#define STM_CMD_RESP_BIT    0x80u

/* Internal command opcodes (this phase) */
#define STM_CMD_SYSTEM_RESTART 0x02u   /* system-restart  MCU stops feeding IWDG */
#define STM_CMD_GET_DEV_INFO   0x04u
#define STM_CMD_SYSTEM_UPDATE  0x06u   /* "system_alive" alive ping */
#define STM_CMD_GW_HEALTH      0x30u
#define STM_CMD_SET_GSM_STATUS 0x31u   /* gsm-connected / gsm-disconnected */
#define STM_CMD_STM_OTA        0x40u   /* stm-ota  firmware chunk / crc */
#define STM_CMD_PERI_OTA       0x41u   /* peri-ota peripheral OTA chunk */

/* STM_OTA CHUNK_TYPE values */
#define STM_OTA_CHUNK_DATA     0x00u
#define STM_OTA_CHUNK_CRC      0x01u

/* Sizes */
#define STM_FRAME_MIN_SIZE     11u    /* SRC_LEN=0, no payload */
#define STM_MAX_PAYLOAD        1040u  /* doc 9.5 FSM guard */
#define STM_SRC_LEN_MAX        31u

/* stm_frame_complete() return codes */
#define STM_FRAME_NEED_MORE     0
#define STM_FRAME_BAD          (-1)

/**
 * @brief CRC-16/IBM-SDLC (poly 0x8408 reflected, init 0xFFFF, final XOR 0xFFFF).
 *        Bit-wise (no lookup table). Check: crc16("123456789") == 0x906E.
 */
UINT16 stm_crc16(const UINT8 *data, UINT32 len);

/**
 * @brief Build a binary frame with SRC_LEN=0 (no source address).
 * @param out      Output buffer.
 * @param out_cap  Capacity of @p out.
 * @param src_type SRC_TYPE byte (echoed by STM).
 * @param cmd      Command opcode (request: bit7=0).
 * @param payload  Payload bytes (may be NULL when payload_len==0).
 * @param payload_len Payload length (<= STM_MAX_PAYLOAD).
 * @return Total frame length on success, or -1 on bad args / overflow.
 */
int stm_build_frame(UINT8 *out, int out_cap, UINT8 src_type, UINT8 cmd,
                    const UINT8 *payload, UINT16 payload_len);

/**
 * @brief Build a binary frame with an explicit source address (SRC_LEN may be >0).
 *        Used for user commands routed from SMS (SRC_ADDR = phone number) so STM
 *        echoes it back for reply routing. @p src_len <= STM_SRC_LEN_MAX.
 * @return Total frame length on success, or -1 on bad args / overflow.
 */
int stm_build_frame_src(UINT8 *out, int out_cap, UINT8 src_type,
                        const UINT8 *src_addr, UINT8 src_len,
                        UINT8 cmd, const UINT8 *payload, UINT16 payload_len);

/**
 * @brief Check whether @p buf holds a complete, CRC-valid frame.
 * @return Full frame length (>0) when complete & CRC OK;
 *         STM_FRAME_NEED_MORE (0) when more bytes are needed;
 *         STM_FRAME_BAD (-1) on a malformed frame (bad SOF/SRC_LEN/LEN/CRC).
 */
int stm_frame_complete(const UINT8 *buf, UINT32 len);

/* Accessors — valid only on a frame that stm_frame_complete() accepted. */
UINT8        stm_frame_cmd(const UINT8 *buf);
const UINT8 *stm_frame_payload(const UINT8 *buf);
UINT16       stm_frame_payload_len(const UINT8 *buf);

/**
 * @brief Self-test against the document's CRC/frame vectors.
 * @return TRUE if CRC and a known frame match the spec (0x906E, 0x7FAB).
 */
BOOL stm_protocol_selftest(void);

#endif /* STM_BINARY_PROTOCOL_H */
