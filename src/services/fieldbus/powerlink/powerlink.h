// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file powerlink.h
 * @brief Ethernet POWERLINK (EPSG) basic frame codec (PROTOCORE_ENABLE_POWERLINK).
 *
 * Ethernet POWERLINK is the EPSG real-time managed-node bus over raw L2 (ethertype 0x88AB, on the
 * shipped services/fieldbus/rawl2). The Managing Node (MN) runs an isochronous cycle: it multicasts a **SoC**
 * (Start of Cycle), unicasts a **PReq** (Poll Request) to each Controlled Node (CN), each CN answers with
 * a **PRes** (Poll Response) carrying its process data, then an **SoA** (Start of Async) opens the async
 * phase. Every EPL basic frame is:
 *
 *     [messageType : 1][destination node : 1][source node : 1][payload...]
 *
 * This builds and parses those frames (the four cyclic message types + the node addressing), so the MN
 * schedules the cycle and a CN answers with its PRes process image. Pure, zero heap, no stdlib,
 * host-testable; the raw-L2 transmit + the isochronous timing (the preempting-task model) are the device
 * step.
 */

#ifndef PROTOCORE_POWERLINK_H
#define PROTOCORE_POWERLINK_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_POWERLINK

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief EPL message types (EPSG DS 301). */
// POWERLINK message types + node ids: wire bytes, so integer constants in a namespacing struct.
#define EPL_MSG_SOC 0x01        ///< Start of Cycle (MN -> all, multicast).
#define EPL_MSG_PREQ 0x03       ///< Poll Request (MN -> CN, unicast).
#define EPL_MSG_PRES 0x04       ///< Poll Response (CN -> all, multicast, carries process data).
#define EPL_MSG_SOA 0x05        ///< Start of Async (MN -> all).
#define EPL_MSG_ASND 0x06       ///< Async Send.
#define EPL_NODE_BROADCAST 0xFF ///< broadcast node id (SoC/SoA destination).
#define EPL_NODE_MN 0xF0        ///< the Managing Node id (240).

/** @brief A parsed EPL basic frame (payload points into the input). */
typedef struct
{
    uint8_t msg_type;
    uint8_t dest;
    uint8_t source;
    const uint8_t *payload;
    size_t payload_len;
} EplFrame;

/** @brief What build takes: msg_type, dest, source, payload, ... */
typedef struct
{
    uint8_t msg_type;
    uint8_t dest;
    uint8_t source;
    const uint8_t *payload;
    size_t payload_len;
    uint8_t *out;
    size_t cap;
} PowerlinkBuildArgs;

/** @brief What soc takes: source, out, cap. */
typedef struct
{
    uint8_t source;
    uint8_t *out;
    size_t cap;
} PowerlinkSocArgs;

/** @brief What preq takes: dest_cn, source, pdo, pdo_len, out, cap. */
typedef struct
{
    uint8_t dest_cn;
    uint8_t source;
    const uint8_t *pdo;
    size_t pdo_len;
    uint8_t *out;
    size_t cap;
} PowerlinkPreqArgs;

/** @brief What pres takes: source_cn, pdo, pdo_len, out, cap. */
typedef struct
{
    uint8_t source_cn;
    const uint8_t *pdo;
    size_t pdo_len;
    uint8_t *out;
    size_t cap;
} PowerlinkPresArgs;

/** @brief What soa takes: source, payload, payload_len, out, cap. */
typedef struct
{
    uint8_t source;
    const uint8_t *payload;
    size_t payload_len;
    uint8_t *out;
    size_t cap;
} PowerlinkSoaArgs;

/** @brief What asnd takes: dest, source, payload, payload_len, out, ... */
typedef struct
{
    uint8_t dest;
    uint8_t source;
    const uint8_t *payload;
    size_t payload_len;
    uint8_t *out;
    size_t cap;
} PowerlinkAsndArgs;

/** @brief What parse takes: frame, len, out. */
typedef struct
{
    const uint8_t *frame;
    size_t len;
    EplFrame *out;
} PowerlinkParseArgs;

/**
 * @brief Ethernet POWERLINK (EPSG) basic frame codec (PROTOCORE_ENABLE_POWERLINK).
 *
 * A caller sets the members a call takes, invokes it through ::Powerlink with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Powerlink.build_args.msg_type = ...;
 *   Powerlink.build_args.dest = ...;
 *   Powerlink.build_args.source = ...;
 *   Powerlink.build_args.payload = ...;
 *   Powerlink.build_args.payload_len = ...;
 *   Powerlink.build_args.out = ...;
 *   Powerlink.build_args.cap = ...;
 *   Powerlink.build(work);
 *   // Powerlink.n is what the call reports
 *
 * @var PowerlinkNs::build_args  what build takes: msg_type, dest, source, payload,
 * @var PowerlinkNs::soc_args  what soc takes: source, out, cap
 * @var PowerlinkNs::preq_args  what preq takes: dest_cn, source, pdo, pdo_len, out, cap
 * @var PowerlinkNs::pres_args  what pres takes: source_cn, pdo, pdo_len, out, cap
 * @var PowerlinkNs::soa_args  what soa takes: source, payload, payload_len, out, cap
 * @var PowerlinkNs::asnd_args  what asnd takes: dest, source, payload, payload_len, out,
 * @var PowerlinkNs::parse_args  what parse takes: frame, len, out
 * @var PowerlinkNs::ok  a call's true/false outcome
 * @var PowerlinkNs::n  the frame length (3 + payload_len), or 0 on overflow / bad args
 * @var PowerlinkNs::build  build an EPL basic frame: [messageType][dest][source][payload...]
 * @var PowerlinkNs::soc  convenience: build an SoC (MN -> broadcast, no payload)
 * @var PowerlinkNs::preq  convenience: build a PReq to a CN carrying its output process image
 * @var PowerlinkNs::pres  convenience: build a PRes from a CN carrying its input process ...
 * @var PowerlinkNs::soa  convenience: build an SoA (MN -> broadcast) that opens the ...
 * @var PowerlinkNs::asnd  convenience: build an ASnd (asynchronous send) from source to dest. ...
 * @var PowerlinkNs::parse  parse an EPL basic frame. true if len >= 3 and the message type is ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    PowerlinkBuildArgs build_args;
    PowerlinkSocArgs soc_args;
    PowerlinkPreqArgs preq_args;
    PowerlinkPresArgs pres_args;
    PowerlinkSoaArgs soa_args;
    PowerlinkAsndArgs asnd_args;
    PowerlinkParseArgs parse_args;
    proto_bool ok;
    size_t n;
} PowerlinkVars;

/** @brief The operands and the outcome. */
extern PowerlinkVars PowerlinkV;

/** @brief The entries. */
typedef struct
{
    void (*const build)(uint8_t *restrict work);
    void (*const soc)(uint8_t *restrict work);
    void (*const preq)(uint8_t *restrict work);
    void (*const pres)(uint8_t *restrict work);
    void (*const soa)(uint8_t *restrict work);
    void (*const asnd)(uint8_t *restrict work);
    void (*const parse)(uint8_t *restrict work);
} PowerlinkNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in PowerlinkV or a region of the borrow at a fixed offset.
void protocore_powerlink_build(uint8_t *restrict work);
void protocore_powerlink_soc(uint8_t *restrict work);
void protocore_powerlink_preq(uint8_t *restrict work);
void protocore_powerlink_pres(uint8_t *restrict work);
void protocore_powerlink_soa(uint8_t *restrict work);
void protocore_powerlink_asnd(uint8_t *restrict work);
void protocore_powerlink_parse(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Powerlink.build(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const PowerlinkNs Powerlink __attribute__((unused)) = {
    .build = protocore_powerlink_build,
    .soc = protocore_powerlink_soc,
    .preq = protocore_powerlink_preq,
    .pres = protocore_powerlink_pres,
    .soa = protocore_powerlink_soa,
    .asnd = protocore_powerlink_asnd,
    .parse = protocore_powerlink_parse,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_POWERLINK

#endif // PROTOCORE_POWERLINK_H
