// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file devicenet.h
 * @brief DeviceNet link-adaptation codec (PROTOCORE_ENABLE_DEVICENET) - the CAN-specific layer of
 *        "CIP over CAN".
 *
 * DeviceNet (ODVA) carries CIP over classic CAN. The CIP application layer (services, EPATH,
 * data) is the same one the EtherNet/IP codec uses, so build the message body with the
 * existing `Cip.*` functions (`PROTOCORE_ENABLE_CIP`); this module supplies the DeviceNet-specific
 * link adaptation that is NOT part of CIP:
 *
 *  - The 11-bit CAN **identifier** as a Message Group (1..4) + Message ID + MAC ID, per the
 *    DeviceNet identifier allocation:
 *    @code
 *      Group 1: 0  MsgID(4)  SourceMAC(6)              ids 0x000-0x3FF
 *      Group 2: 10 MAC(6)    MsgID(3)                  ids 0x400-0x5FF
 *      Group 3: 11 MsgID(3)  SourceMAC(6)              ids 0x600-0x7BF
 *      Group 4: 11111        MsgID(6)                  ids 0x7C0-0x7EF
 *    @endcode
 *  - The explicit-message **header octet** (FRAG | XID | MAC ID).
 *  - The **fragmentation protocol** (type + modulo-64 count) and a reassembler for explicit
 *    messages longer than one 8-octet frame.
 *
 * Pure and host-tested. Drive it from the ESP32 TWAI peripheral or an MCP2515 over SPI to
 * bridge a DeviceNet segment onto Wi-Fi.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DEVICENET_H
#define PROTOCORE_DEVICENET_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_DEVICENET

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

// Message-group identifier bases / field widths.
#define DEVICENET_G1_BASE 0x000u ///< Message Group 1 (0x000-0x3FF)
#define DEVICENET_G2_BASE 0x400u ///< Message Group 2 (0x400-0x5FF)
#define DEVICENET_G3_BASE 0x600u ///< Message Group 3 (0x600-0x7BF)
#define DEVICENET_G4_BASE 0x7C0u ///< Message Group 4 (0x7C0-0x7EF)
#define DEVICENET_MAC_MASK 0x3Fu ///< MAC IDs are 0..63

// Common Group 2 message IDs (predefined master/slave connection set).
#define DEVICENET_G2_UNCONNECTED_EXPLICIT_REQ 4u ///< unconnected explicit request to a slave
#define DEVICENET_G2_EXPLICIT_RESPONSE 3u        ///< explicit / unconnected response from a slave
#define DEVICENET_G2_POLL_COMMAND 5u             ///< Poll command / change-of-state to a slave
#define DEVICENET_G2_DUP_MAC_CHECK 7u            ///< Duplicate MAC ID check

// Explicit-message header octet fields.
#define DEVICENET_HDR_FRAG 0x80u ///< this body is fragmented (a fragmentation octet follows)
#define DEVICENET_HDR_XID 0x40u  ///< transaction-id bit

// Fragmentation octet: type in the top 2 bits, modulo-64 count in the low 6.
#define DEVICENET_FRAG_FIRST 0x00u  ///< first fragment
#define DEVICENET_FRAG_MIDDLE 0x40u ///< middle fragment
#define DEVICENET_FRAG_LAST 0x80u   ///< last fragment
#define DEVICENET_FRAG_ACK 0xC0u    ///< fragment acknowledge
#define DEVICENET_FRAG_TYPE_MASK 0xC0u
#define DEVICENET_FRAG_COUNT_MASK 0x3Fu

/** @brief DeviceNet message groups. */
typedef enum PROTO_ENUM_PACKED
{
    DEVICENET_GROUP_1 = 1,
    DEVICENET_GROUP_2 = 2,
    DEVICENET_GROUP_3 = 3,
    DEVICENET_GROUP_4 = 4,
} DeviceNetGroup;

/** @brief A decoded DeviceNet identifier. */
typedef struct
{
    DeviceNetGroup group;
    uint8_t msg_id; ///< message id within the group
    uint8_t mac_id; ///< source / node MAC id (0..63; not present for Group 4)
} DeviceNetId;

/** @brief Result of feeding a frame to the fragmentation reassembler. */
typedef enum PROTO_ENUM_PACKED
{
    DEVICENET_FRAG_IGNORED = 0,
    DEVICENET_FRAG_STARTED,
    DEVICENET_FRAG_PROGRESS,
    DEVICENET_FRAG_COMPLETE,
    DEVICENET_FRAG_ERR,
} DeviceNetFragResult;

/** @brief Fragmented-message reassembly context. */
typedef struct
{
    proto_bool active;
    uint8_t next_count;                       ///< next expected modulo-64 fragment count
    uint16_t len;                             ///< octets stored so far
    uint8_t buf[PROTOCORE_DEVICENET_MSG_MAX]; ///< reassembled body (excludes the fragmentation octets)
} DeviceNetFragRx;

#include "shared/can/can.h" // CanFrame: the type a parameter points at

/** @brief What encode_id takes: id, group, msg_id, mac_id. */
typedef struct
{
    uint32_t *id;
    DeviceNetGroup group;
    uint8_t msg_id;
    uint8_t mac_id;
} DevicenetEncodeIdArgs;

/** @brief What decode_id takes: can_id, out. */
typedef struct
{
    uint32_t can_id;
    DeviceNetId *out;
} DevicenetDecodeIdArgs;

/** @brief What msg_header takes: frag, xid, mac_id. */
typedef struct
{
    proto_bool frag;
    proto_bool xid;
    uint8_t mac_id;
} DevicenetMsgHeaderArgs;

/** @brief What frag_octet takes: type, count. */
typedef struct
{
    uint8_t type;
    uint8_t count;
} DevicenetFragOctetArgs;

/** @brief What build_explicit takes: out, group, msg_id, mac_id, ... */
typedef struct
{
    CanFrame *out;
    DeviceNetGroup group;
    uint8_t msg_id;
    uint8_t mac_id;
    const uint8_t *body;
    uint8_t body_len;
} DevicenetBuildExplicitArgs;

/** @brief What build_fragment takes: out, group, msg_id, mac_id, xid, ... */
typedef struct
{
    CanFrame *out;
    DeviceNetGroup group;
    uint8_t msg_id;
    uint8_t mac_id;
    proto_bool xid;
    uint8_t frag_type;
    uint8_t frag_count;
    const uint8_t *data;
    uint8_t data_len;
} DevicenetBuildFragmentArgs;

/** @brief What frag_reset takes: rx. */
typedef struct
{
    DeviceNetFragRx *rx;
} DevicenetFragResetArgs;

/** @brief What frag_feed takes: rx, body, body_len. */
typedef struct
{
    DeviceNetFragRx *rx;
    const uint8_t *body;
    uint8_t body_len;
} DevicenetFragFeedArgs;

/**
 * @brief DeviceNet link-adaptation codec (PROTOCORE_ENABLE_DEVICENET) - the CAN-specific layer of "CIP over CAN".
 * DeviceNet (ODVA) carries CIP over classic CAN.
 *
 * A caller sets the members a call takes, invokes it through ::Devicenet with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Devicenet.encode_id_args.id = ...;
 *   Devicenet.encode_id_args.group = ...;
 *   Devicenet.encode_id_args.msg_id = ...;
 *   Devicenet.encode_id_args.mac_id = ...;
 *   Devicenet.encode_id(work);
 *   // Devicenet.ok is what the call reports
 *
 * @var DevicenetNs::encode_id_args  what encode_id takes: id, group, msg_id, mac_id
 * @var DevicenetNs::decode_id_args  what decode_id takes: can_id, out
 * @var DevicenetNs::msg_header_args  what msg_header takes: frag, xid, mac_id
 * @var DevicenetNs::frag_octet_args  what frag_octet takes: type, count
 * @var DevicenetNs::build_explicit_args  what build_explicit takes: out, group, msg_id, mac_id,
 * @var DevicenetNs::build_fragment_args  what build_fragment takes: out, group, msg_id, mac_id, xid,
 * @var DevicenetNs::frag_reset_args  what frag_reset takes: rx
 * @var DevicenetNs::frag_feed_args  what frag_feed takes: rx, body, body_len
 * @var DevicenetNs::ok  true on success; false on a null out, data_len > 6, a null data ...
 * @var DevicenetNs::value  the value a call reports
 * @var DevicenetNs::frag  what a call reports
 * @var DevicenetNs::encode_id  encode a DeviceNet 11-bit CAN id. mac_id is ignored for Group 4
 * @var DevicenetNs::decode_id  decode an 11-bit CAN id into its DeviceNet group / message id / MAC ...
 * @var DevicenetNs::msg_header  compose the explicit-message header octet (FRAG / XID / MAC id)
 * @var DevicenetNs::frag_octet  compose a fragmentation octet from a type (DEVICENET_FRAG_*) and a ...
 * @var DevicenetNs::build_explicit  build a single-frame explicit message: [header octet][body...] at ...
 * @var DevicenetNs::build_fragment  build one fragment of a fragmented explicit message (the sender ...
 * @var DevicenetNs::frag_reset  reset a reassembly context to idle
 * @var DevicenetNs::frag_feed  feed a received frame's body (the octets after the CAN id) to the ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    DevicenetEncodeIdArgs encode_id_args;
    DevicenetDecodeIdArgs decode_id_args;
    DevicenetMsgHeaderArgs msg_header_args;
    DevicenetFragOctetArgs frag_octet_args;
    DevicenetBuildExplicitArgs build_explicit_args;
    DevicenetBuildFragmentArgs build_fragment_args;
    DevicenetFragResetArgs frag_reset_args;
    DevicenetFragFeedArgs frag_feed_args;
    proto_bool ok;
    uint8_t value;
    DeviceNetFragResult frag;
} DevicenetVars;

/** @brief The operands and the outcome. */
extern DevicenetVars DevicenetV;

/** @brief The entries. */
typedef struct
{
    void (*const encode_id)(uint8_t *restrict work);
    void (*const decode_id)(uint8_t *restrict work);
    void (*const msg_header)(uint8_t *restrict work);
    void (*const frag_octet)(uint8_t *restrict work);
    void (*const build_explicit)(uint8_t *restrict work);
    void (*const build_fragment)(uint8_t *restrict work);
    void (*const frag_reset)(uint8_t *restrict work);
    void (*const frag_feed)(uint8_t *restrict work);
} DevicenetNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in DevicenetV or a region of the borrow at a fixed offset.
void protocore_devicenet_encode_id(uint8_t *restrict work);
void protocore_devicenet_decode_id(uint8_t *restrict work);
void protocore_devicenet_msg_header(uint8_t *restrict work);
void protocore_devicenet_frag_octet(uint8_t *restrict work);
void protocore_devicenet_build_explicit(uint8_t *restrict work);
void protocore_devicenet_build_fragment(uint8_t *restrict work);
void protocore_devicenet_frag_reset(uint8_t *restrict work);
void protocore_devicenet_frag_feed(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Devicenet.encode_id(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const DevicenetNs Devicenet __attribute__((unused)) = {
    .encode_id = protocore_devicenet_encode_id,
    .decode_id = protocore_devicenet_decode_id,
    .msg_header = protocore_devicenet_msg_header,
    .frag_octet = protocore_devicenet_frag_octet,
    .build_explicit = protocore_devicenet_build_explicit,
    .build_fragment = protocore_devicenet_build_fragment,
    .frag_reset = protocore_devicenet_frag_reset,
    .frag_feed = protocore_devicenet_frag_feed,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DEVICENET

#endif // PROTOCORE_DEVICENET_H
