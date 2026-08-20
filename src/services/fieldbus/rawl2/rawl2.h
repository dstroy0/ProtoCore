// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rawl2.h
 * @brief Raw Layer-2 Ethernet frame codec (PROTOCORE_ENABLE_RAWL2).
 *
 * The host-testable core of raw-L2 frame TX/RX: build and parse Ethernet II frames (and 802.1Q
 * VLAN-tagged frames) so the app can inject/receive arbitrary L2 frames - the basis for the raw-L2
 * industrial protocols (PROFINET DCP, IEC 61850 GOOSE, POWERLINK, SERCOS) and for custom management /
 * proprietary MAC framing. On device the bytes go out through the vendor L2 transmit path
 * (wired or Wi-Fi); the MAC normally appends the FCS, so the builder emits the frame
 * without it and `Rawl2.fcs` is provided for the cases that need it.
 *
 *   Ethernet II:  [dst MAC 6][src MAC 6][ethertype 2][payload]
 *   802.1Q:       [dst 6][src 6][0x8100][TCI 2][ethertype 2][payload]
 *
 * Pure, zero heap, no stdlib, host-testable.
 */

#ifndef PROTOCORE_RAWL2_H
#define PROTOCORE_RAWL2_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_RAWL2

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

// Ethernet II framing sizes + ethertypes.
#define ETH_ALEN 6            ///< MAC address length.
#define ETH_HDR_LEN 14        ///< dst + src + ethertype.
#define ETH_VLAN_HDR_LEN 18   ///< with the 4-octet 802.1Q tag.
#define ETH_TPID_8021Q 0x8100 ///< 802.1Q tag protocol id.
#define ETHERTYPE_IPV4 0x0800
#define ETHERTYPE_ARP 0x0806
#define ETHERTYPE_PROFINET 0x8892 ///< PROFINET RT / DCP.
#define ETHERTYPE_GOOSE 0x88B8    ///< IEC 61850 GOOSE.
#define ETHERTYPE_POWERLINK 0x88AB

/** @brief A parsed Ethernet frame (pointers into the input). */
typedef struct
{
    const uint8_t *dst;
    const uint8_t *src;
    proto_bool vlan;
    uint8_t pcp;
    uint16_t vid;
    uint16_t ethertype;
    const uint8_t *payload;
    size_t payload_len;
} EthFrame;

/** @brief What build takes: dst, src, ethertype, payload, ... */
typedef struct
{
    const uint8_t *dst;
    const uint8_t *src;
    uint16_t ethertype;
    const uint8_t *payload;
    size_t payload_len;
    uint8_t *out;
    size_t cap;
} Rawl2BuildArgs;

/** @brief What build_vlan takes: dst, src, pcp, dei, vid, ethertype, ... */
typedef struct
{
    const uint8_t *dst;
    const uint8_t *src;
    uint8_t pcp;    ///< priority code point (0..7)
    proto_bool dei; ///< drop-eligible indicator
    uint16_t vid;   ///< VLAN id (0..4095)
    uint16_t ethertype;
    const uint8_t *payload;
    size_t payload_len;
    uint8_t *out;
    size_t cap;
} Rawl2BuildVlanArgs;

/** @brief What parse takes: frame, len, out. */
typedef struct
{
    const uint8_t *frame;
    size_t len;
    EthFrame *out;
} Rawl2ParseArgs;

/** @brief What fcs takes: bytes, len. */
typedef struct
{
    const uint8_t *bytes;
    size_t len;
} Rawl2FcsArgs;

/**
 * @brief Raw Layer-2 Ethernet frame codec (PROTOCORE_ENABLE_RAWL2).
 *
 * A caller sets the members a call takes, invokes it through ::Rawl2 with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Rawl2.build_args.dst = ...;
 *   Rawl2.build_args.src = ...;
 *   Rawl2.build_args.ethertype = ...;
 *   Rawl2.build_args.payload = ...;
 *   Rawl2.build_args.payload_len = ...;
 *   Rawl2.build_args.out = ...;
 *   Rawl2.build_args.cap = ...;
 *   Rawl2.build(work);
 *   // Rawl2.n is what the call reports
 *
 * @var Rawl2Ns::build_args  what build takes: dst, src, ethertype, payload,
 * @var Rawl2Ns::build_vlan_args  what build_vlan takes: dst, src, pcp, dei, vid, ethertype,
 * @var Rawl2Ns::parse_args  what parse takes: frame, len, out
 * @var Rawl2Ns::fcs_args  what fcs takes: bytes, len
 * @var Rawl2Ns::ok  a call's true/false outcome
 * @var Rawl2Ns::n  the frame length (14 + payload_len), or 0 if it won't fit or a ...
 * @var Rawl2Ns::u32  what a call reports
 * @var Rawl2Ns::build  build an Ethernet II frame (no FCS)
 * @var Rawl2Ns::build_vlan  build an 802.1Q VLAN-tagged Ethernet frame (no FCS)
 * @var Rawl2Ns::parse  parse an Ethernet II / 802.1Q frame (FCS not expected). true if ...
 * @var Rawl2Ns::fcs  IEEE 802.3 frame check sequence (CRC-32, reflected, init ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    Rawl2BuildArgs build_args;
    Rawl2BuildVlanArgs build_vlan_args;
    Rawl2ParseArgs parse_args;
    Rawl2FcsArgs fcs_args;
    proto_bool ok;
    size_t n;
    uint32_t u32;
} Rawl2Vars;

/** @brief The operands and the outcome. */
extern Rawl2Vars Rawl2V;

/** @brief The entries. */
typedef struct
{
    void (*const build)(uint8_t *restrict work);
    void (*const build_vlan)(uint8_t *restrict work);
    void (*const parse)(uint8_t *restrict work);
    void (*const fcs)(uint8_t *restrict work);
} Rawl2Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Rawl2V or a region of the borrow at a fixed offset.
void protocore_rawl2_build(uint8_t *restrict work);
void protocore_rawl2_build_vlan(uint8_t *restrict work);
void protocore_rawl2_parse(uint8_t *restrict work);
void protocore_rawl2_fcs(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Rawl2.build(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Rawl2Ns Rawl2 __attribute__((unused)) = {
    .build = protocore_rawl2_build,
    .build_vlan = protocore_rawl2_build_vlan,
    .parse = protocore_rawl2_parse,
    .fcs = protocore_rawl2_fcs,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_RAWL2

#endif // PROTOCORE_RAWL2_H
