// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file snp.h
 * @brief GE Fanuc SNP (Series Ninety Protocol) serial frame codec (PROTOCORE_ENABLE_SNP).
 *
 * SNP is the GE Fanuc Series 90 (90-30 / 90-70) master-slave serial protocol over RS-485. A message is
 * a BCC-checked frame delimited by control characters:
 *
 *     [SOH-or-other-control][data...][checksum]
 *
 * SNP frames the payload with an ASCII/binary control byte, a length, the data, and the Block Check
 * Code of GE Fanuc GFK-0582D p. 7-62 (seed zero; per byte, XOR then rotate the accumulator left one
 * bit). This builds/validates that frame so a device can
 * read/write registers on a Series 90 PLC; the RS-485 UART transport (and the SNP-X session setup) is
 * the remaining device step. Pure, zero heap, no stdlib, host-testable.
 */

#ifndef PROTOCORE_SNP_H
#define PROTOCORE_SNP_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SNP

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief SNP control bytes (subset). */
// SNP control bytes: wire values compared/emitted, so integer constants in a namespacing struct.
#define SNP_ENQ 0x05 ///< enquiry / attach.
#define SNP_ACK 0x06 ///< acknowledge.
#define SNP_NAK 0x15 ///< negative acknowledge.
#define SNP_SOH 0x01 ///< start of header (a request/response frame).
#define SNP_EOT 0x04 ///< end of transmission.

/** @brief A parsed SNP frame (data points into the input). */
typedef struct
{
    uint8_t control;
    const uint8_t *data;
    size_t data_len;
} SnpFrame;

/** @brief What bcc takes: bytes, len. */
typedef struct
{
    const uint8_t *bytes;
    size_t len;
} SnpBccArgs;

/** @brief What build takes: control, data, data_len, out, cap. */
typedef struct
{
    uint8_t control;
    const uint8_t *data;
    size_t data_len;
    uint8_t *out;
    size_t cap;
} SnpBuildArgs;

/** @brief What parse takes: frame, len, out. */
typedef struct
{
    const uint8_t *frame;
    size_t len;
    SnpFrame *out;
} SnpParseArgs;

/**
 * @brief GE Fanuc SNP (Series Ninety Protocol) serial frame codec (PROTOCORE_ENABLE_SNP).
 *
 * A caller sets the members a call takes, invokes it through ::Snp with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Snp.bcc_args.bytes = ...;
 *   Snp.bcc_args.len = ...;
 *   Snp.bcc(work);
 *   // Snp.value is what the call reports
 *
 * @var SnpNs::bcc_args  what bcc takes: bytes, len
 * @var SnpNs::build_args  what build takes: control, data, data_len, out, cap
 * @var SnpNs::parse_args  what parse takes: frame, len, out
 * @var SnpNs::ok  a call's true/false outcome
 * @var SnpNs::value  the value a call reports
 * @var SnpNs::n  the frame length (2 + data_len + 1), or 0 on overflow / bad args ...
 * @var SnpNs::bcc  block Check Code over len bytes (GFK-0582D p. 7-62). Seeded at ...
 * @var SnpNs::build  build an SNP frame: [control][length][data...][BCC]. length is the ...
 * @var SnpNs::parse  validate the BCC and parse an SNP frame. true if the BCC matches ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    SnpBccArgs bcc_args;
    SnpBuildArgs build_args;
    SnpParseArgs parse_args;
    proto_bool ok;
    uint8_t value;
    size_t n;
} SnpVars;

/** @brief The operands and the outcome. */
extern SnpVars SnpV;

/** @brief The entries. */
typedef struct
{
    void (*const bcc)(uint8_t *restrict work);
    void (*const build)(uint8_t *restrict work);
    void (*const parse)(uint8_t *restrict work);
} SnpNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in SnpV or a region of the borrow at a fixed offset.
void protocore_snp_bcc(uint8_t *restrict work);
void protocore_snp_build(uint8_t *restrict work);
void protocore_snp_parse(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Snp.bcc(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const SnpNs Snp __attribute__((unused)) = {
    .bcc = protocore_snp_bcc,
    .build = protocore_snp_build,
    .parse = protocore_snp_parse,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SNP

#endif // PROTOCORE_SNP_H
