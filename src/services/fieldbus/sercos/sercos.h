// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sercos.h
 * @brief SERCOS III motion-bus telegram + IDN codec (PROTOCORE_ENABLE_SERCOS).
 *
 * SERCOS III is the real-time drive/motion bus over Ethernet (raw L2, ethertype 0x88CD, on the shipped
 * services/fieldbus/rawl2). The master cyclically sends **MDT** (Master Data Telegrams) carrying setpoints to the
 * drives, and the drives answer with **AT** (Acknowledge / drive Telegrams) carrying actual values. Both
 * carry a short SERCOS header then the cyclic device data; a separate **service channel** transfers
 * parameters addressed by an **IDN** (IDentification Number):
 *
 *   Telegram header: [type MDT/AT : 1][phase/counter : 1][cycle count : 2]
 *   IDN (16 bit):    S/P bit(1) | parameter-set(3) | data-block(12)  -> "S-0-0100" style addressing
 *
 * This provides the MDT/AT telegram framing + the IDN encode/decode (the addressing every drive
 * parameter uses). The isochronous timing + the ring/line topology are the hardware-gated part. Pure,
 * zero heap, no stdlib, host-testable.
 */

#ifndef PROTOCORE_SERCOS_H
#define PROTOCORE_SERCOS_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SERCOS

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

// SERCOS telegram types + header length: wire values, so integer constants in a struct.
#define SERCOS_TEL_MDT 0x00 ///< Master Data Telegram (master -> drives).
#define SERCOS_TEL_AT 0x01  ///< Acknowledge Telegram (drive -> master).
#define SERCOS_HDR_LEN 4

/** @brief A parsed SERCOS telegram (data points into the input). */
typedef struct
{
    uint8_t type;
    uint8_t phase;
    uint16_t cycle;
    const uint8_t *data;
    size_t data_len;
} SercosTelegram;
/** @brief What idn takes: is_product, param_set, data_block. */
typedef struct
{
    proto_bool is_product; ///< true = a P-parameter (product-specific), false = an S-parameter (standard)
    uint8_t param_set;     ///< the parameter set 0..7
    uint16_t data_block;   ///< the data block number 0..4095
} SercosIdnArgs;
/** @brief What idn_parse takes: idn, is_product, param_set, data_block. */
typedef struct
{
    uint16_t idn;
    proto_bool *is_product;
    uint8_t *param_set;
    uint16_t *data_block;
} SercosIdnParseArgs;
/** @brief What build takes: type, phase, cycle, data, data_len, out, ... */
typedef struct
{
    uint8_t type;        ///< SERCOS_TEL_MDT or SERCOS_TEL_AT
    uint8_t phase;       ///< the communication phase / counter byte
    uint16_t cycle;      ///< the cycle count
    const uint8_t *data; ///< the cyclic device data (may be null if data_len == 0)
    size_t data_len;
    uint8_t *out;
    size_t cap;
} SercosBuildArgs;
/** @brief What parse takes: frame, len, out. */
typedef struct
{
    const uint8_t *frame;
    size_t len;
    SercosTelegram *out;
} SercosParseArgs;
/**
 * @brief SERCOS III motion-bus telegram + IDN codec (PROTOCORE_ENABLE_SERCOS).
 *
 * A caller sets the members a call takes, invokes it through ::Sercos with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Sercos.idn_args.is_product = ...;
 *   Sercos.idn_args.param_set = ...;
 *   Sercos.idn_args.data_block = ...;
 *   Sercos.idn(work);
 *   // Sercos.value is what the call reports
 *
 * @var SercosNs::idn_args  what idn takes: is_product, param_set, data_block
 * @var SercosNs::idn_parse_args  what idn_parse takes: idn, is_product, param_set, data_block
 * @var SercosNs::build_args  what build takes: type, phase, cycle, data, data_len, out,
 * @var SercosNs::parse_args  what parse takes: frame, len, out
 * @var SercosNs::ok  a call's true/false outcome
 * @var SercosNs::value  the 16-bit IDN: bit15 = S/P, bits14..12 = set, bits11..0 = block
 * @var SercosNs::n  the telegram length (4 + data_len), or 0 on overflow
 * @var SercosNs::idn  encode a SERCOS IDN (16-bit) from its parts
 * @var SercosNs::idn_parse  decode a SERCOS IDN into its parts (any out-pointer may be null)
 * @var SercosNs::build  build a SERCOS telegram: [type][phase][cycle:2 LE][data...]
 * @var SercosNs::parse  parse a SERCOS telegram. true if len >= 4 and the type is MDT/AT
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    SercosIdnArgs idn_args;
    SercosIdnParseArgs idn_parse_args;
    SercosBuildArgs build_args;
    SercosParseArgs parse_args;
    proto_bool ok;
    uint16_t value;
    size_t n;
} SercosVars;

/** @brief The operands and the outcome. */
extern SercosVars SercosV;

/** @brief The entries. */
typedef struct
{
    void (*const idn)(uint8_t *restrict work);
    void (*const idn_parse)(uint8_t *restrict work);
    void (*const build)(uint8_t *restrict work);
    void (*const parse)(uint8_t *restrict work);
} SercosNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in SercosV or a region of the borrow at a fixed offset.
void protocore_sercos_idn(uint8_t *restrict work);
void protocore_sercos_idn_parse(uint8_t *restrict work);
void protocore_sercos_build(uint8_t *restrict work);
void protocore_sercos_parse(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Sercos.idn(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const SercosNs Sercos __attribute__((unused)) = {
    .idn = protocore_sercos_idn,
    .idn_parse = protocore_sercos_idn_parse,
    .build = protocore_sercos_build,
    .parse = protocore_sercos_parse,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SERCOS

#endif // PROTOCORE_SERCOS_H
