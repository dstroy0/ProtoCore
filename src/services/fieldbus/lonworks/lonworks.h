// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file lonworks.h
 * @brief LonWorks / LON-IP (ISO/IEC 14908) network-variable codec (PROTOCORE_ENABLE_LONWORKS).
 *
 * LonWorks is the ISO/IEC 14908 building-automation network. Devices exchange **network variables**
 * (SNVTs - Standard Network Variable Types) as LonTalk application PDUs. LON/IP (14908-4) tunnels those
 * PDUs over UDP so a device speaks LON without a Neuron chip. This codec builds/parses the LonTalk
 * application-layer message a network-variable update carries:
 *
 *   [1 d sel13..8 : 1][sel7..0 : 1][value...]
 *
 * where bit 7 marks the APDU a network variable message, `d` is the direction (1 outgoing, 0 incoming),
 * and the remaining six bits join the second octet to form the 14-bit selector that addresses the bound
 * network variable. The value is the SNVT-encoded data. It also provides the two most-common
 * SNVT scalar encodings from the LONMARK SNVT master list: **SNVT_temp** (index 39, tenths of a degree
 * Celsius above -274) and **SNVT_switch** (index 95, a level 0..100 % in 0.5 % steps + a state), so an
 * app reads/writes those without a full SNVT table. Pure, zero heap, no stdlib, host-testable; the
 * LON/IP UDP transport is the shipped UDP layer.
 */

#ifndef PROTOCORE_LONWORKS_H
#define PROTOCORE_LONWORKS_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_LONWORKS

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

// LonTalk NV header fields: wire values, so integer constants in a struct.
#define LON_NV_HDR_LEN 2           ///< the NV APDU header is two octets.
#define LON_NV_TYPE_MASK 0xC0      ///< octet 0 bits 7..6: the message bit and the direction bit.
#define LON_NV_SEL_HI_MASK 0x3F    ///< octet 0 bits 5..0: selector bits 13..8.
#define LON_MSG_NV_UPDATE 0x80     ///< message bit set, direction incoming: `1 0 000000`.
#define LON_MSG_NV_POLL 0x81       ///< message bit set, direction incoming, selector bit 0 set.
#define LON_NV_SELECTOR_MAX 0x3FFF ///< the NV selector is 14 bits.

/** @brief A parsed LonTalk NV PDU (value points into the input). */
typedef struct
{
    uint8_t msg_code;
    uint16_t selector;
    const uint8_t *value;
    size_t value_len;
} LonNv;

/** @brief What build_nv takes: msg_code, selector, value, value_len, ... */
typedef struct
{
    uint8_t msg_code;     ///< supplies bits 7..6; its low bits are the caller's and are dropped
    uint16_t selector;    ///< the 14-bit NV selector (0..0x3FFF)
    const uint8_t *value; ///< the SNVT-encoded value (may be null if value_len == 0)
    size_t value_len;     ///< value length
    uint8_t *out;
    size_t cap;
} LonworksBuildNvArgs;

/** @brief What parse_nv takes: pdu, len, out. */
typedef struct
{
    const uint8_t *pdu;
    size_t len;
    LonNv *out;
} LonworksParseNvArgs;

/** @brief What snvt_temp_encode takes: celsius, out. */
typedef struct
{
    double celsius;
    uint8_t *out; ///< 2 bytes.
} LonworksSnvtTempEncodeArgs;

/** @brief What snvt_temp_decode takes: in. */
typedef struct
{
    const uint8_t *in; ///< 2 bytes.
} LonworksSnvtTempDecodeArgs;

/** @brief What snvt_switch_encode takes: percent, state, out. */
typedef struct
{
    double percent;
    uint8_t state;
    uint8_t *out; ///< 2 bytes.
} LonworksSnvtSwitchEncodeArgs;

/** @brief What snvt_switch_decode takes: in, percent, state. */
typedef struct
{
    const uint8_t *in; ///< 2 bytes.
    double *percent;
    uint8_t *state;
} LonworksSnvtSwitchDecodeArgs;

/**
 * @brief LonWorks / LON-IP (ISO/IEC 14908) network-variable codec (PROTOCORE_ENABLE_LONWORKS).
 *
 * A caller sets the members a call takes, invokes it through ::Lonworks with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Lonworks.build_nv_args.msg_code = ...;
 *   Lonworks.build_nv_args.selector = ...;
 *   Lonworks.build_nv_args.value = ...;
 *   Lonworks.build_nv_args.value_len = ...;
 *   Lonworks.build_nv_args.out = ...;
 *   Lonworks.build_nv_args.cap = ...;
 *   Lonworks.build_nv(work);
 *   // Lonworks.n is what the call reports
 *
 * @var LonworksNs::build_nv_args  what build_nv takes: msg_code, selector, value, value_len,
 * @var LonworksNs::parse_nv_args  what parse_nv takes: pdu, len, out
 * @var LonworksNs::snvt_temp_encode_args  what snvt_temp_encode takes: celsius, out
 * @var LonworksNs::snvt_temp_decode_args  what snvt_temp_decode takes: in
 * @var LonworksNs::snvt_switch_encode_args  what snvt_switch_encode takes: percent, state, out
 * @var LonworksNs::snvt_switch_decode_args  what snvt_switch_decode takes: in, percent, state
 * @var LonworksNs::ok  a call's true/false outcome
 * @var LonworksNs::n  the PDU length (2 + value_len), or 0 on overflow / bad args
 * @var LonworksNs::value  the value a call reports
 * @var LonworksNs::build_nv  build a LonTalk NV application PDU: ...
 * @var LonworksNs::parse_nv  parse a LonTalk NV PDU. true if len >= 3
 * @var LonworksNs::snvt_temp_encode  encode degrees C as the 2-byte big-endian SNVT_temp raw: (celsius * ...
 * @var LonworksNs::snvt_temp_decode  decode a SNVT_temp 2-byte value to degrees C: (raw - 2740) / 10
 * @var LonworksNs::snvt_switch_encode  encode a SNVT_switch (value 0..100 % in 0.5 % steps, state 0 OFF / ...
 * @var LonworksNs::snvt_switch_decode  decode a SNVT_switch 2-byte value (percent out via percent, state ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    LonworksBuildNvArgs build_nv_args;
    LonworksParseNvArgs parse_nv_args;
    LonworksSnvtTempEncodeArgs snvt_temp_encode_args;
    LonworksSnvtTempDecodeArgs snvt_temp_decode_args;
    LonworksSnvtSwitchEncodeArgs snvt_switch_encode_args;
    LonworksSnvtSwitchDecodeArgs snvt_switch_decode_args;
    proto_bool ok;
    size_t n;
    double value;
} LonworksVars;

/** @brief The operands and the outcome. */
extern LonworksVars LonworksV;

/** @brief The entries. */
typedef struct
{
    void (*const build_nv)(uint8_t *restrict work);
    void (*const parse_nv)(uint8_t *restrict work);
    void (*const snvt_temp_encode)(uint8_t *restrict work);
    void (*const snvt_temp_decode)(uint8_t *restrict work);
    void (*const snvt_switch_encode)(uint8_t *restrict work);
    void (*const snvt_switch_decode)(uint8_t *restrict work);
} LonworksNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in LonworksV or a region of the borrow at a fixed offset.
void protocore_lonworks_build_nv(uint8_t *restrict work);
void protocore_lonworks_parse_nv(uint8_t *restrict work);
void protocore_lonworks_snvt_temp_encode(uint8_t *restrict work);
void protocore_lonworks_snvt_temp_decode(uint8_t *restrict work);
void protocore_lonworks_snvt_switch_encode(uint8_t *restrict work);
void protocore_lonworks_snvt_switch_decode(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Lonworks.build_nv(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const LonworksNs Lonworks __attribute__((unused)) = {
    .build_nv = protocore_lonworks_build_nv,
    .parse_nv = protocore_lonworks_parse_nv,
    .snvt_temp_encode = protocore_lonworks_snvt_temp_encode,
    .snvt_temp_decode = protocore_lonworks_snvt_temp_decode,
    .snvt_switch_encode = protocore_lonworks_snvt_switch_encode,
    .snvt_switch_decode = protocore_lonworks_snvt_switch_decode,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_LONWORKS

#endif // PROTOCORE_LONWORKS_H
