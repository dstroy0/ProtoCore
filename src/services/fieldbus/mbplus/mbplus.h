// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mbplus.h
 * @brief Modbus Plus HDLC token-bus frame codec (PROTOCORE_ENABLE_MBPLUS).
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_MBPLUS

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

// Modbus Plus HDLC wire constants: integer values compared/emitted, in a namespacing struct.
#define MBPLUS_FLAG 0x7E       ///< HDLC frame delimiter.
#define MBPLUS_MAX_STATION 64  ///< stations 1..64 on a Modbus Plus segment.
#define MBPLUS_CTRL_DATA 0x00  ///< data frame control.
#define MBPLUS_CTRL_TOKEN 0x01 ///< token pass control.

/** @brief A parsed Modbus Plus frame (payload points into the input). */
typedef struct
{
    uint8_t address;
    uint8_t control;
    const uint8_t *payload;
    size_t payload_len;
} MbPlusFrame;

/** @brief What crc takes: bytes, len. */
typedef struct
{
    const uint8_t *bytes;
    size_t len;
} MbplusCrcArgs;

/** @brief What build takes: address, control, payload, payload_len, ... */
typedef struct
{
    uint8_t address;        ///< the destination station (1..64)
    uint8_t control;        ///< MBPLUS_CTRL_DATA / MBPLUS_CTRL_TOKEN
    const uint8_t *payload; ///< the routing path + Modbus PDU (may be null if payload_len == 0)
    size_t payload_len;     ///< its length
    uint8_t *out;
    size_t cap;
} MbplusBuildArgs;

/** @brief What parse takes: frame, len, out. */
typedef struct
{
    const uint8_t *frame;
    size_t len;
    MbPlusFrame *out;
} MbplusParseArgs;

/** @brief What next_token takes: current, max_station. */
typedef struct
{
    uint8_t current;     ///< this station's address (1..max_station)
    uint8_t max_station; ///< the highest active station on the segment
} MbplusNextTokenArgs;

/**
 * @brief Modbus Plus HDLC token-bus frame codec (PROTOCORE_ENABLE_MBPLUS).
 *
 * A caller sets the members a call takes, invokes it through ::Mbplus with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Mbplus.crc_args.bytes = ...;
 *   Mbplus.crc_args.len = ...;
 *   Mbplus.crc(work);
 *   // Mbplus.value is what the call reports
 *
 * @var MbplusNs::crc_args  what crc takes: bytes, len
 * @var MbplusNs::build_args  what build takes: address, control, payload, payload_len,
 * @var MbplusNs::parse_args  what parse takes: frame, len, out
 * @var MbplusNs::next_token_args  what next_token takes: current, max_station
 * @var MbplusNs::ok  a call's true/false outcome
 * @var MbplusNs::value  the next station address, wrapping from max_station back to 1
 * @var MbplusNs::n  the frame length (1 + 1 + 1 + payload_len + 2 + 1), or 0 on ...
 * @var MbplusNs::crc  CRC-16/X-25 (the Modbus Plus HDLC FCS) over len bytes
 * @var MbplusNs::build  build a Modbus Plus HDLC frame: 7E addr ctrl [payload] CRClo CRChi ...
 * @var MbplusNs::parse  validate the flags + CRC and parse a Modbus Plus frame. true if ...
 * @var MbplusNs::next_token  compute the next token holder in the logical ring
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    MbplusCrcArgs crc_args;
    MbplusBuildArgs build_args;
    MbplusParseArgs parse_args;
    MbplusNextTokenArgs next_token_args;
    proto_bool ok;
    uint16_t value;
    size_t n;
} MbplusVars;

/** @brief The operands and the outcome. */
extern MbplusVars MbplusV;

/** @brief The entries. */
typedef struct
{
    void (*const crc)(uint8_t *restrict work);
    void (*const build)(uint8_t *restrict work);
    void (*const parse)(uint8_t *restrict work);
    void (*const next_token)(uint8_t *restrict work);
} MbplusNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in MbplusV or a region of the borrow at a fixed offset.
void protocore_mbplus_crc(uint8_t *restrict work);
void protocore_mbplus_build(uint8_t *restrict work);
void protocore_mbplus_parse(uint8_t *restrict work);
void protocore_mbplus_next_token(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Mbplus.crc(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const MbplusNs Mbplus __attribute__((unused)) = {
    .crc = protocore_mbplus_crc,
    .build = protocore_mbplus_build,
    .parse = protocore_mbplus_parse,
    .next_token = protocore_mbplus_next_token,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MBPLUS

#endif // PROTOCORE_MBPLUS_H
