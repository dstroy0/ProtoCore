// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file profinet.h
 * @brief PROFINET DCP (Discovery and Configuration Protocol) frame codec (PROTOCORE_ENABLE_PROFINET).
 *
 * DCP is how PROFINET IO-Devices are discovered and named on the wire before an IO connection exists.
 * It rides raw L2 (ethertype 0x8892, PROFINET RT; see services/fieldbus/rawl2) with a fixed 10-octet frame header
 * followed by DCP blocks:
 *
 *   Header:  frameID(2) serviceID(1) serviceType(1) xid(4) responseDelayFactor(2) dataLength(2)
 *   Block:   option(1) suboption(1) blockLength(2) [blockInfo(2) for Set/Get responses] value...
 *
 * FrameIDs: 0xFEFE Identify-request (multicast), 0xFEFF Identify-response, 0xFEFD Get/Set. This builds
 * the DCP header + blocks and parses them (walking each block via a callback), so a device answers
 * Identify (with its NameOfStation / IP / device id) and handles Set (assign name/IP). Pure, zero heap,
 * host-testable; the raw-L2 transmit is the device step.
 */

#ifndef PROTOCORE_PROFINET_H
#define PROTOCORE_PROFINET_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_PROFINET

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

#define PN_FRAMEID_DCP_HELLO 0xFEFC
#define PN_FRAMEID_DCP_GETSET 0xFEFD
#define PN_FRAMEID_DCP_IDENT_REQ 0xFEFE
#define PN_FRAMEID_DCP_IDENT_RES 0xFEFF
#define PN_DCP_SERVICE_GET 0x03
#define PN_DCP_SERVICE_SET 0x04
#define PN_DCP_SERVICE_IDENTIFY 0x05
#define PN_DCP_TYPE_REQUEST 0x00
#define PN_DCP_TYPE_RESPONSE_SUCCESS 0x01
#define PN_DCP_OPT_IP 0x01
#define PN_DCP_SUB_IP_PARAM 0x02 ///< IP address / subnet / gateway.
#define PN_DCP_OPT_DEVICE 0x02
#define PN_DCP_SUB_DEV_NAME_OF_STATION 0x02
#define PN_DCP_SUB_DEV_ID 0x03
#define PN_DCP_OPT_ALL 0xFF
#define PN_DCP_SUB_ALL 0xFF
#define PN_DCP_HDR_LEN 12 ///< frameID(2) + the 10-octet DCP header

/** @brief A parsed DCP frame header. */
typedef struct
{
    uint16_t frame_id;
    uint8_t service_id;
    uint8_t service_type;
    uint32_t xid;
    uint16_t response_delay;
    uint16_t data_length;
} PnDcpHeader;
/** @brief One DCP block surfaced by protocore_pn_dcp_walk. */
typedef void (*protocore_pn_dcp_block_cb)(uint8_t option, uint8_t suboption, const uint8_t *value, size_t value_len,
                                          void *arg);
/** @brief What dcp_header takes: frame_id, service_id, service_type, ... */
typedef struct
{
    uint16_t frame_id;
    uint8_t service_id;
    uint8_t service_type;
    uint32_t xid;
    uint16_t response_delay; ///< ResponseDelayFactor: the window a device randomizes its Identify response over, in
                             ///< units of ...
    uint16_t data_length;    ///< the total length of the DCP blocks that follow (filled into the header)
    uint8_t *out;
    size_t cap;
} ProfinetDcpHeaderArgs;
/** @brief What dcp_block takes: option, suboption, value, value_len, ... */
typedef struct
{
    uint8_t option;
    uint8_t suboption;
    const uint8_t *value;
    size_t value_len;
    uint8_t *out;
    size_t cap;
} ProfinetDcpBlockArgs;
/** @brief What dcp_parse_header takes: frame, len, out. */
typedef struct
{
    const uint8_t *frame;
    size_t len;
    PnDcpHeader *out;
} ProfinetDcpParseHeaderArgs;
/** @brief What dcp_walk takes: blocks, len, cb, arg. */
typedef struct
{
    const uint8_t *blocks;
    size_t len;
    protocore_pn_dcp_block_cb cb;
    void *arg;
} ProfinetDcpWalkArgs;
/**
 * @brief PROFINET DCP (Discovery and Configuration Protocol) frame codec (PROTOCORE_ENABLE_PROFINET).
 *
 * A caller sets the members a call takes, invokes it through ::Profinet with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Profinet.dcp_header_args.frame_id = ...;
 *   Profinet.dcp_header_args.service_id = ...;
 *   Profinet.dcp_header_args.service_type = ...;
 *   Profinet.dcp_header_args.xid = ...;
 *   Profinet.dcp_header_args.response_delay = ...;
 *   Profinet.dcp_header_args.data_length = ...;
 *   Profinet.dcp_header_args.out = ...;
 *   Profinet.dcp_header_args.cap = ...;
 *   Profinet.dcp_header(work);
 *   // Profinet.n is what the call reports
 *
 * @var ProfinetNs::dcp_header_args  what dcp_header takes: frame_id, service_id, service_type,
 * @var ProfinetNs::dcp_block_args  what dcp_block takes: option, suboption, value, value_len,
 * @var ProfinetNs::dcp_parse_header_args  what dcp_parse_header takes: frame, len, out
 * @var ProfinetNs::dcp_walk_args  what dcp_walk takes: blocks, len, cb, arg
 * @var ProfinetNs::ok  true if every block fits; invokes cb per block (value excludes the ...
 * @var ProfinetNs::n  the block length written (4 + value_len, padded to even per DCP), ...
 * @var ProfinetNs::dcp_header  build a DCP frame header into out (>= PN_DCP_HDR_LEN bytes). 12, or ...
 * @var ProfinetNs::dcp_block  append a DCP block `[option][suboption][blockLength][value...]` (no ...
 * @var ProfinetNs::dcp_parse_header  parse the DCP header. true if len >= PN_DCP_HDR_LEN
 * @var ProfinetNs::dcp_walk  walk the DCP blocks after the header (blocks points at header+10, ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    ProfinetDcpHeaderArgs dcp_header_args;
    ProfinetDcpBlockArgs dcp_block_args;
    ProfinetDcpParseHeaderArgs dcp_parse_header_args;
    ProfinetDcpWalkArgs dcp_walk_args;
    proto_bool ok;
    size_t n;
} ProfinetVars;

/** @brief The operands and the outcome. */
extern ProfinetVars ProfinetV;

/** @brief The entries. */
typedef struct
{
    void (*const dcp_header)(uint8_t *restrict work);
    void (*const dcp_block)(uint8_t *restrict work);
    void (*const dcp_parse_header)(uint8_t *restrict work);
    void (*const dcp_walk)(uint8_t *restrict work);
} ProfinetNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in ProfinetV or a region of the borrow at a fixed offset.
void protocore_profinet_dcp_header(uint8_t *restrict work);
void protocore_profinet_dcp_block(uint8_t *restrict work);
void protocore_profinet_dcp_parse_header(uint8_t *restrict work);
void protocore_profinet_dcp_walk(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Profinet.dcp_header(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const ProfinetNs Profinet __attribute__((unused)) = {
    .dcp_header = protocore_profinet_dcp_header,
    .dcp_block = protocore_profinet_dcp_block,
    .dcp_parse_header = protocore_profinet_dcp_parse_header,
    .dcp_walk = protocore_profinet_dcp_walk,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_PROFINET

#endif // PROTOCORE_PROFINET_H
