// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file df1.h
 * @brief Allen-Bradley DF1 full-duplex frame codec (PROTOCORE_ENABLE_DF1) - zero-heap framing +
 *        DLE byte-stuffing + BCC/CRC for the Rockwell serial PLC link layer.
 *
 * A DF1 full-duplex message frame (AB pub. 1770-6.5.16):
 * @code
 *   DLE STX  <application data, DLE bytes doubled>  DLE ETX  BCC | CRC
 * @endcode
 *  - A data byte equal to DLE (0x10) is transmitted twice (DLE DLE); the doubled DLE is
 *    counted only once in the BCC/CRC.
 *  - BCC: the 2's complement of the modulo-256 sum of the application data bytes (the ETX
 *    is NOT included). One octet.
 *  - CRC: CRC-16 (poly X16+X15+X2+X0 = 0x8005, init 0x0000, reflected) over the application
 *    data bytes AND the ETX byte; two octets, transmitted low byte first.
 *
 * This is the data-link framing layer; the DST/SRC/CMD/STS/TNS application header lives
 * inside the application data. Field definitions verified against the 1770-6.5.16 manual.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DF1_H
#define PROTOCORE_DF1_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_DF1

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

#define DF1_DLE 0x10
#define DF1_STX 0x02
#define DF1_ETX 0x03

/** @brief Which error check the frame carries. */
typedef enum PROTO_ENUM_PACKED
{
    DF1_CHECK_BCC, ///< 1-octet block check character
    DF1_CHECK_CRC  ///< 2-octet CRC-16
} Df1Check;

/** @brief What bcc takes: data, len. */
typedef struct
{
    const uint8_t *data;
    size_t len;
} Df1BccArgs;

/** @brief What crc takes: data, len. */
typedef struct
{
    const uint8_t *data;
    size_t len;
} Df1CrcArgs;

/** @brief What build_frame takes: buf, cap, data, data_len, check. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const uint8_t *data;
    size_t data_len;
    Df1Check check; ///< DF1_CHECK_BCC (1 octet) or DF1_CHECK_CRC (2 octets, low byte first; CRC over the data + ETX)
} Df1BuildFrameArgs;

/** @brief What parse_frame takes: buf, len, check, out, out_cap, ... */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    Df1Check check;
    uint8_t *out;    ///< receives the de-stuffed application data
    size_t out_cap;  ///< capacity of out
    size_t *out_len; ///< receives the application-data length
} Df1ParseFrameArgs;

/**
 * @brief Allen-Bradley DF1 full-duplex frame codec (PROTOCORE_ENABLE_DF1) - zero-heap framing + DLE byte-stuffing +
 * BCC/CRC for the Rockwell serial PLC link layer.
 *
 * A caller sets the members a call takes, invokes it through ::Df1 with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Df1.bcc_args.data = ...;
 *   Df1.bcc_args.len = ...;
 *   Df1.bcc(work);
 *   // Df1.value is what the call reports
 *
 * @var Df1Ns::bcc_args  what bcc takes: data, len
 * @var Df1Ns::crc_args  what crc takes: data, len
 * @var Df1Ns::build_frame_args  what build_frame takes: buf, cap, data, data_len, check
 * @var Df1Ns::parse_frame_args  what parse_frame takes: buf, len, check, out, out_cap,
 * @var Df1Ns::ok  true on a complete, check-valid frame; false on bad framing, ...
 * @var Df1Ns::value  the value a call reports
 * @var Df1Ns::u16  what a call reports
 * @var Df1Ns::n  total octets written, or 0 on overflow / bad input
 * @var Df1Ns::bcc  BCC: 2's complement of the modulo-256 sum of [data, data+len)
 * @var Df1Ns::crc  CRC-16/ARC (poly 0x8005 / 0xA001 reflected, init 0) over [data, ...
 * @var Df1Ns::build_frame  build a full-duplex frame around data: DLE STX + stuffed data + DLE ...
 * @var Df1Ns::parse_frame  parse + validate a full-duplex frame, un-stuffing the application ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    Df1BccArgs bcc_args;
    Df1CrcArgs crc_args;
    Df1BuildFrameArgs build_frame_args;
    Df1ParseFrameArgs parse_frame_args;
    proto_bool ok;
    uint8_t value;
    uint16_t u16;
    size_t n;
} Df1Vars;

/** @brief The operands and the outcome. */
extern Df1Vars Df1V;

/** @brief The entries. */
typedef struct
{
    void (*const bcc)(uint8_t *restrict work);
    void (*const crc)(uint8_t *restrict work);
    void (*const build_frame)(uint8_t *restrict work);
    void (*const parse_frame)(uint8_t *restrict work);
} Df1Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Df1V or a region of the borrow at a fixed offset.
void protocore_df1_bcc(uint8_t *restrict work);
void protocore_df1_crc(uint8_t *restrict work);
void protocore_df1_build_frame(uint8_t *restrict work);
void protocore_df1_parse_frame(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Df1.bcc(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Df1Ns Df1 __attribute__((unused)) = {
    .bcc = protocore_df1_bcc,
    .crc = protocore_df1_crc,
    .build_frame = protocore_df1_build_frame,
    .parse_frame = protocore_df1_parse_frame,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DF1

#endif // PROTOCORE_DF1_H
