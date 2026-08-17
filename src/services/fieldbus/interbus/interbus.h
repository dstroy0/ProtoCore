// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file interbus.h
 * @brief INTERBUS summation-frame fieldbus codec (PROTOCORE_ENABLE_INTERBUS).
 *
 * INTERBUS (Phoenix Contact) is a ring fieldbus with a distinctive **summation frame**: instead of
 * addressing each device, one frame circulates the whole ring and every device is a shift-register slice
 * of it - the master clocks the frame around, each device reads its input slot and writes its output
 * slot as the bits pass through. A cycle frame is:
 *
 *   [loopback word : 2][device data words...][FCS : 2 (CRC-16/CCITT)]
 *
 * The loopback word (0xFFFF -> 0x0000) detects the ring is closed; each device slice is a fixed number of
 * 16-bit words (its process image). This codec assembles the summation frame from a list of per-device
 * word slices and disassembles a received frame back into those slices, plus the CRC. The physical ring
 * (the shift-register clocking) is hardware-gated; this is the summation-frame + process-image layer.
 * Pure, zero heap, no stdlib, host-testable.
 */

#ifndef PROTOCORE_INTERBUS_H
#define PROTOCORE_INTERBUS_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_INTERBUS

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief the loopback word that opens a summation frame. */
#define PROTOCORE_INTERBUS_LOOPBACK 0xFFFF

/** @brief What fcs takes: bytes, len. */
typedef struct
{
    const uint8_t *bytes;
    size_t len;
} InterbusFcsArgs;

/** @brief What build takes: words, word_count, out, cap. */
typedef struct
{
    const uint16_t *words; ///< the concatenated device data words (big-endian on the wire)
    size_t word_count;     ///< number of 16-bit words across all device slices
    uint8_t *out;          ///< output byte buffer
    size_t cap;            ///< its capacity
} InterbusBuildArgs;

/** @brief What parse takes: frame, len, out_words, max_words, ... */
typedef struct
{
    const uint8_t *frame; ///< the received frame
    size_t len;           ///< its length
    uint16_t *out_words;  ///< buffer for the decoded 16-bit words
    size_t max_words;     ///< its capacity (in words)
    size_t *out_count;    ///< set to the number of words decoded
} InterbusParseArgs;

/**
 * @brief INTERBUS summation-frame fieldbus codec (PROTOCORE_ENABLE_INTERBUS).
 *
 * A caller sets the members a call takes, invokes it through ::Interbus with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Interbus.fcs_args.bytes = ...;
 *   Interbus.fcs_args.len = ...;
 *   Interbus.fcs(work);
 *   // Interbus.value is what the call reports
 *
 * @var InterbusNs::fcs_args  what fcs takes: bytes, len
 * @var InterbusNs::build_args  what build takes: words, word_count, out, cap
 * @var InterbusNs::parse_args  what parse takes: frame, len, out_words, max_words,
 * @var InterbusNs::ok  true if the loopback word + FCS are valid and the words fit ...
 * @var InterbusNs::value  the value a call reports
 * @var InterbusNs::n  the frame length (2 + word_count*2 + 2), or 0 on overflow. Layout: ...
 * @var InterbusNs::fcs  CRC-16/CCITT-FALSE (the INTERBUS FCS) over len bytes
 * @var InterbusNs::build  assemble a summation frame from per-device 16-bit word slices
 * @var InterbusNs::parse  disassemble a summation frame back into device data words
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    InterbusFcsArgs fcs_args;
    InterbusBuildArgs build_args;
    InterbusParseArgs parse_args;

    proto_bool ok;
    uint16_t value;
    size_t n;

    void (*const fcs)(uint8_t *restrict work);
    void (*const build)(uint8_t *restrict work);
    void (*const parse)(uint8_t *restrict work);
} InterbusNs;

/** @brief The one symbol this module exports. */
extern InterbusNs Interbus;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_INTERBUS

#endif // PROTOCORE_INTERBUS_H
