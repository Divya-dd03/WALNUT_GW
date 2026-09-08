/**
 * @file stm_command_map.h
 * @brief User-command (SMS/TCP) ASCII <-> STM binary translation (hybrid).
 *
 * SIMCOM owns the frame envelope (SOF/VER/TYPE/SRC routing/CRC). This module maps
 * a known ASCII command (e.g. "set-ble-scan-on", "set-ble-peri-info,1,...") to its
 * binary opcode + payload and builds the frame. Commands not in the table fall
 * back to the raw-hex passthrough so new STM opcodes need no SIMCOM change.
 */

#ifndef STM_COMMAND_MAP_H
#define STM_COMMAND_MAP_H

#include "common/types.h"

/* stm_cmd_build_from_ascii() return codes (>0 = frame length). */
#define STM_CMD_UNKNOWN     0     /* command name not in table -> caller may passthrough/reject */
#define STM_CMD_BUILD_ERR  (-1)   /* known command but encode/overflow failed */

/**
 * @brief Build a binary STM frame from a user ASCII command.
 * @param ascii_cmd ASCII command, prefix already stripped (e.g. "set-ble-scan-on"
 *                  or "set-ble-peri-info,1,AA:BB:..."). First token = command name.
 * @param src_type  SRC_TYPE for routing (STM_SRC_TYPE_SMS / _TCP).
 * @param src_addr  Source address bytes (SMS phone number); NULL/0 for TCP.
 * @param src_len   Length of @p src_addr (0..STM_SRC_LEN_MAX).
 * @param out       Output frame buffer.
 * @param out_cap   Capacity of @p out.
 * @return >0 frame length; STM_CMD_UNKNOWN (0) if name not mapped;
 *         STM_CMD_BUILD_ERR (-1) on encode/overflow error.
 */
int stm_cmd_build_from_ascii(const char *ascii_cmd, UINT8 src_type,
                             const UINT8 *src_addr, UINT8 src_len,
                             UINT8 *out, int out_cap);

/**
 * @brief Raw-hex passthrough: if @p body is a hex-encoded command body, decode it
 *        into @p out as the frame to forward (caller adds nothing). For new/unmapped
 *        opcodes the sender supplies hex; SIMCOM stays a transparent bridge.
 * @return >0 decoded frame length; 0 if @p body is not a hex passthrough.
 * @note Skeleton stub — full passthrough wiring lands with the per-command phase.
 */
int stm_cmd_try_hex_passthrough(const char *body, UINT8 *out, int out_cap);

#endif /* STM_COMMAND_MAP_H */
