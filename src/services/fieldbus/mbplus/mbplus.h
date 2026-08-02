// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mbplus.h
 * @brief Modbus Plus HDLC token-bus frame codec (PC_ENABLE_MBPLUS).
 *
 * Modbus Plus is Schneider's 1 Mbit/s token-passing peer bus. Its data link is HDLC-framed: a frame is
 * delimited by the HDLC flag 0x7E, carries an address / control / the LLC+Modbus routing path + data,
 * and ends with a CRC-16 (CRC-16/X-25). This codec builds/validates the HDLC frame (with 0x7E-flag
 * delimiting and the standard bit/byte transparency handled at the byte level) around a Modbus routing
 * path + PDU, plus the token-rotation helper that computes the next station in the logical ring:
 *
 *   [7E][address][control][routing path...][data...][CRC-16 lo][CRC-16 hi][7E]
 *
 * The physical 1 Mbit/s bus is hardware-gated; this is the frame + token-MAC layer, reusing the shipped
 * Modbus PDU model for the data. Pure, zero heap, no stdlib, host-testable.
 */

#ifndef PROTOCORE_MBPLUS_H
#define PROTOCORE_MBPLUS_H

#include "protocore_config.h"

#if PC_ENABLE_MBPLUS

// Modbus Plus HDLC wire constants: integer values compared/emitted, in a namespacing struct.
#define MBPLUS_FLAG 0x7E       ///< HDLC frame delimiter.
#define MBPLUS_MAX_STATION 64  ///< stations 1..64 on a Modbus Plus segment.
#define MBPLUS_CTRL_DATA 0x00  ///< data frame control.
#define MBPLUS_CTRL_TOKEN 0x01 ///< token pass control.

/** @brief CRC-16/X-25 (the Modbus Plus HDLC FCS) over @p len bytes. */
uint16_t pc_mbplus_crc(const uint8_t *bytes, size_t len);

/**
 * @brief Build a Modbus Plus HDLC frame: 7E addr ctrl [payload] CRClo CRChi 7E.
 * @param address  the destination station (1..64).
 * @param control  MBPLUS_CTRL_DATA / MBPLUS_CTRL_TOKEN.
 * @param payload  the routing path + Modbus PDU (may be null if payload_len == 0).
 * @param payload_len its length.
 * @return the frame length (1 + 1 + 1 + payload_len + 2 + 1), or 0 on overflow / bad args.
 *
 * The CRC covers address + control + payload (not the flags).
 */
size_t pc_mbplus_build(uint8_t address, uint8_t control, const uint8_t *payload, size_t payload_len, uint8_t *out,
                       size_t cap);

/** @brief A parsed Modbus Plus frame (payload points into the input). */
typedef struct
{
    uint8_t address;
    uint8_t control;
    const uint8_t *payload;
    size_t payload_len;
} MbPlusFrame;

/** @brief Validate the flags + CRC and parse a Modbus Plus frame. @return true if well-formed. */
proto_bool pc_mbplus_parse(const uint8_t *frame, size_t len, MbPlusFrame *out);

/**
 * @brief Compute the next token holder in the logical ring.
 * @param current      this station's address (1..max_station).
 * @param max_station  the highest active station on the segment.
 * @return the next station address, wrapping from max_station back to 1.
 */
uint8_t pc_mbplus_next_token(uint8_t current, uint8_t max_station);

#endif // PC_ENABLE_MBPLUS
#endif // PROTOCORE_MBPLUS_H
