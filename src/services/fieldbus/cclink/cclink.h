// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cclink.h
 * @brief CC-Link (CLPA) cyclic fieldbus frame codec (PROTOCORE_ENABLE_CCLINK).
 *
 * CC-Link is Mitsubishi's (CLPA) factory fieldbus. The classic CC-Link master polls remote stations
 * over RS-485 exchanging a cyclic process image split into bit devices (RX/RY - remote input/output
 * bits) and word devices (RWr/RWw - remote registers). This codec builds/validates the cyclic frame a
 * master/station exchanges:
 *
 *     [station][command][RX/RY bit data...][RWr/RWw word data...][sum-checksum]
 *
 * A station's process image is a fixed BSS block; this frames it. The checksum is the low byte of the
 * arithmetic sum of the framed bytes. The RS-485 timing and the CC-Link IE Field (Gigabit) PHY are the
 * hardware-gated part; this is the frame + process-image accessors. Pure, zero heap, host-testable.
 */

#ifndef PROTOCORE_CCLINK_H
#define PROTOCORE_CCLINK_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_CCLINK

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

// CC-Link command bytes: wire values compared/emitted, so integer constants in a namespacing struct.
#define CCLINK_CMD_REFRESH 0x01 ///< cyclic refresh (master <-> station process image).
#define CCLINK_CMD_POLL 0x02    ///< poll a station.
#define CCLINK_CMD_TEST 0x0F    ///< line test.

/** @brief A parsed CC-Link frame (payload points into the input; caller knows the bit/word split). */
typedef struct
{
    uint8_t station;
    uint8_t command;
    const uint8_t *payload; ///< the bit+word data region.
    size_t payload_len;
} CcLinkFrame;

/** @brief What sum takes: bytes, len. */
typedef struct
{
    const uint8_t *bytes;
    size_t len;
} CclinkSumArgs;

/** @brief What build takes: station, command, bits, bit_len, words, ... */
typedef struct
{
    uint8_t station;      ///< station number 0..63
    uint8_t command;      ///< CCLINK_CMD_*
    const uint8_t *bits;  ///< the RX/RY bit-device bytes (may be null if bit_len == 0)
    size_t bit_len;       ///< number of bit-device bytes
    const uint8_t *words; ///< the RWr/RWw word-device bytes (little-endian words; may be null if word_len == 0)
    size_t word_len;      ///< number of word-device bytes
    uint8_t *out;
    size_t cap;
} CclinkBuildArgs;

/** @brief What parse takes: frame, len, out. */
typedef struct
{
    const uint8_t *frame;
    size_t len;
    CcLinkFrame *out;
} CclinkParseArgs;

/** @brief What get_bit takes: bits, bit_len, index. */
typedef struct
{
    const uint8_t *bits;
    size_t bit_len;
    size_t index;
} CclinkGetBitArgs;

/** @brief What set_bit takes: bits, bit_len, index, value. */
typedef struct
{
    uint8_t *bits;
    size_t bit_len;
    size_t index;
    proto_bool value;
} CclinkSetBitArgs;

/** @brief What get_word takes: words, word_len, index. */
typedef struct
{
    const uint8_t *words;
    size_t word_len;
    size_t index;
} CclinkGetWordArgs;

/**
 * @brief CC-Link (CLPA) cyclic fieldbus frame codec (PROTOCORE_ENABLE_CCLINK).
 *
 * A caller sets the members a call takes, invokes it through ::Cclink with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Cclink.sum_args.bytes = ...;
 *   Cclink.sum_args.len = ...;
 *   Cclink.sum(work);
 *   // Cclink.value is what the call reports
 *
 * @var CclinkNs::sum_args  what sum takes: bytes, len
 * @var CclinkNs::build_args  what build takes: station, command, bits, bit_len, words,
 * @var CclinkNs::parse_args  what parse takes: frame, len, out
 * @var CclinkNs::get_bit_args  what get_bit takes: bits, bit_len, index
 * @var CclinkNs::set_bit_args  what set_bit takes: bits, bit_len, index, value
 * @var CclinkNs::get_word_args  what get_word takes: words, word_len, index
 * @var CclinkNs::ok  a call's true/false outcome
 * @var CclinkNs::value  the value a call reports
 * @var CclinkNs::n  the frame length (2 + bit_len + word_len + 1), or 0 on overflow / ...
 * @var CclinkNs::sum  arithmetic-sum checksum: low byte of the sum of len bytes
 * @var CclinkNs::build  build a CC-Link cyclic frame: ...
 * @var CclinkNs::parse  validate the checksum and parse a CC-Link frame. true if the ...
 * @var CclinkNs::get_bit  read bit index (0-based) from a bit-device byte array
 * @var CclinkNs::set_bit  set/clear bit index in a bit-device byte array (no-op if out of ...
 * @var CclinkNs::get_word  read word index (0-based, little-endian) from a word-device byte ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    CclinkSumArgs sum_args;
    CclinkBuildArgs build_args;
    CclinkParseArgs parse_args;
    CclinkGetBitArgs get_bit_args;
    CclinkSetBitArgs set_bit_args;
    CclinkGetWordArgs get_word_args;

    proto_bool ok;
    uint8_t value; ///< the checksum a sum reports
    uint16_t u16;  ///< the word a get_word reports: its own member, since value is an octet
    size_t n;

    void (*const sum)(uint8_t *restrict work);
    void (*const build)(uint8_t *restrict work);
    void (*const parse)(uint8_t *restrict work);
    void (*const get_bit)(uint8_t *restrict work);
    void (*const set_bit)(uint8_t *restrict work);
    void (*const get_word)(uint8_t *restrict work);
} CclinkNs;

/** @brief The one symbol this module exports. */
extern CclinkNs Cclink;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CCLINK

#endif // PROTOCORE_CCLINK_H
