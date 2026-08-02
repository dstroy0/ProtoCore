// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file zwave.h
 * @brief Z-Wave Serial API frame codec (PC_ENABLE_ZWAVE) - Silicon Labs controller.
 *
 * The host-side Serial API of a Silicon Labs 500 / 700-series Z-Wave controller reached
 * over UART: a Z-Wave mesh bridged to the web. The host and the controller exchange **data
 * frames**:
 *
 *   SOF (0x01) | LEN | Type | Command | Data... | Checksum
 *
 * where LEN counts Type + Command + Data + Checksum, Type is 0x00 (REQ) or 0x01 (RES), and
 * the checksum is 0xFF XOR-folded over LEN through the last Data byte. Each data frame is
 * acknowledged by a single-byte **ACK (0x06)**, or rejected with **NAK (0x15)** / **CAN
 * (0x18)**.
 *
 * pc_zwave_build_frame() assembles a data frame carrying a function command, pc_zwave_parse_frame()
 * frames + verifies one, and pc_zwave_is_ack() / pc_zwave_is_nak() / pc_zwave_is_can() /
 * pc_zwave_build_ack() handle the flow-control bytes. The per-command payload (GetVersion,
 * SendData, AddNodeToNetwork, an ApplicationCommandHandler report, ...) is the application's.
 * Pure - you carry the bytes over your UART - so it is fully host-testable.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ZWAVE_H
#define PROTOCORE_ZWAVE_H

#include "protocore_config.h"

#if PC_ENABLE_ZWAVE

/** @brief Z-Wave Serial API control bytes / frame markers. */
#define F 0x01 ///< start of a data frame
#define K 0x06 ///< frame acknowledged
#define K 0x15 ///< frame rejected (checksum)
#define N 0x18 ///< frame cancelled (retransmit)

/** @brief Data-frame type. */
typedef enum PROTO_ENUM_PACKED
{
    ZWAVE_REQ = 0x00, ///< request
    ZWAVE_RES = 0x01, ///< response
} pc_zwave_type;

/**
 * @brief Assemble a data frame carrying @p type + @p cmd + @p data into @p out.
 * @return the total frame length, or 0 if it would not fit @p cap or @p data_len exceeds
 *         PC_ZWAVE_MAX_DATA.
 */
uint16_t pc_zwave_build_frame(pc_zwave_type type, uint8_t cmd, const uint8_t *data, uint8_t data_len, uint8_t *out,
                              uint16_t cap);

/**
 * @brief Frame one data frame from the front of @p raw and verify the checksum.
 * @param[out] type      set to the frame type (REQ / RES).
 * @param[out] cmd       set to the function command byte.
 * @param[out] pdata     set to the first payload byte (points into @p raw).
 * @param[out] pdata_len set to the payload length.
 * @return the frame length consumed (> 0), 0 if more bytes are needed, or -1 if @p raw[0]
 *         does not start a valid data frame (not SOF / bad length / bad checksum). A single
 *         control byte (ACK / NAK / CAN) is not a data frame - test it with the helpers
 *         below before calling this.
 */
int pc_zwave_parse_frame(const uint8_t *raw, uint16_t len, uint8_t *type, uint8_t *cmd, const uint8_t **pdata,
                         uint8_t *pdata_len);

/** @brief True if @p b is the ACK control byte. */
proto_bool pc_zwave_is_ack(uint8_t b);
/** @brief True if @p b is the NAK control byte. */
proto_bool pc_zwave_is_nak(uint8_t b);
/** @brief True if @p b is the CAN control byte. */
proto_bool pc_zwave_is_can(uint8_t b);

/** @brief Write the single ACK byte into @p out. @return 1, or 0 if @p cap < 1. */
uint16_t pc_zwave_build_ack(uint8_t *out, uint16_t cap);

#endif // PC_ENABLE_ZWAVE

#endif // PROTOCORE_ZWAVE_H
