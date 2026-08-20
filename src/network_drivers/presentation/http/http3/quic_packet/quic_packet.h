// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_quic_packet.h
 * @brief QUIC packet headers and packet-number coding (RFC 9000 sec 17).
 *
 * The structural, version-independent layer of a QUIC packet: the long-header form (Initial /
 * 0-RTT / Handshake / Retry, plus the Version Negotiation packet whose Version is 0) and the
 * short-header 1-RTT form, and the packet-number truncation coding (sec 17.1, Appendix A.2/A.3).
 *
 * This is the unprotected structure only - it parses and builds the header fields that are not
 * covered by header protection (header form, version, connection IDs) and codes packet numbers.
 * Packet protection (AEAD) and header protection are layered on top by the QUIC crypto module.
 * Pure, zero heap, host-tested against the RFC worked examples.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_QUIC_PACKET_H
#define PROTOCORE_QUIC_PACKET_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HTTP3

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

#define QUIC_VERSION_1 0x00000001u ///< RFC 9000
#define QUIC_MAX_CID_LEN 20        ///< maximum connection-ID length in QUIC version 1

/** @brief Long-header packet types (RFC 9000 sec 17.2, Table 5). */
#define QUIC_LP_INITIAL 0x00
#define QUIC_LP_0RTT 0x01
#define QUIC_LP_HANDSHAKE 0x02
#define QUIC_LP_RETRY 0x03

/** @brief A parsed long header (invariant fields). A Version of 0 marks a Version Negotiation. */
typedef struct
{
    uint8_t first;                  ///< raw first byte
    uint8_t type;                   ///< long packet type (first & 0x30) >> 4; meaningful when version != 0
    uint32_t version;               ///< QUIC version; 0 = Version Negotiation
    uint8_t dcid_len;               ///< Destination Connection ID length
    uint8_t dcid[QUIC_MAX_CID_LEN]; ///< Destination Connection ID
    uint8_t scid_len;               ///< Source Connection ID length
    uint8_t scid[QUIC_MAX_CID_LEN]; ///< Source Connection ID
    size_t hdr_len;                 ///< bytes consumed up to the start of the type-specific payload
} QuicLongHeader;

/** @brief A parsed short header (1-RTT). The DCID length is known locally, not on the wire. */
typedef struct
{
    uint8_t first;                  ///< raw first byte
    uint8_t spin;                   ///< latency spin bit (0x20)
    uint8_t key_phase;              ///< key-phase bit (0x04)
    uint8_t pn_len;                 ///< packet-number length in bytes (1..4)
    uint8_t dcid_len;               ///< Destination Connection ID length (caller-supplied)
    uint8_t dcid[QUIC_MAX_CID_LEN]; ///< Destination Connection ID
    size_t hdr_len;                 ///< bytes up to the (protected) Packet Number field
} QuicShortHeader;

/** @brief What is_long_header takes: first. */
typedef struct
{
    uint8_t first;
} QuicPacketIsLongHeaderArgs;

/** @brief What parse_long_header takes: buf, len, out. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    QuicLongHeader *out;
} QuicPacketParseLongHeaderArgs;

/** @brief What build_long_header takes: out, cap, type, version, ... */
typedef struct
{
    uint8_t *out;
    size_t cap;
    uint8_t type;
    uint32_t version;
    const uint8_t *dcid;
    uint8_t dcid_len;
    const uint8_t *scid;
    uint8_t scid_len;
    uint8_t pn_len;
} QuicPacketBuildLongHeaderArgs;

/** @brief What parse_short_header takes: buf, len, dcid_len, out. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    uint8_t dcid_len;
    QuicShortHeader *out;
} QuicPacketParseShortHeaderArgs;

/** @brief What build_version_negotiation takes: out, cap, dcid, ... */
typedef struct
{
    uint8_t *out;
    size_t cap;
    const uint8_t *dcid;
    uint8_t dcid_len;
    const uint8_t *scid;
    uint8_t scid_len;
    const uint32_t *versions;
    size_t nversions;
} QuicPacketBuildVersionNegotiationArgs;

/** @brief What pn_length takes: full_pn, largest_acked. */
typedef struct
{
    uint64_t full_pn;
    int64_t largest_acked;
} QuicPacketPnLengthArgs;

/** @brief What pn_encode takes: out, cap, full_pn, largest_acked. */
typedef struct
{
    uint8_t *out;
    size_t cap;
    uint64_t full_pn;
    int64_t largest_acked;
} QuicPacketPnEncodeArgs;

/** @brief What pn_decode takes: largest_pn, truncated_pn, pn_nbits. */
typedef struct
{
    uint64_t largest_pn;
    uint64_t truncated_pn;
    uint8_t pn_nbits;
} QuicPacketPnDecodeArgs;

/**
 * @brief QUIC packet headers and packet-number coding (RFC 9000 sec 17).
 *
 * A caller sets the members a call takes, invokes it through ::QuicPacket with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   QuicPacket.is_long_header_args.first = ...;
 *   QuicPacket.is_long_header(work);
 *   // QuicPacket.ok is what the call reports
 *
 * @var QuicPacketNs::is_long_header_args  what is_long_header takes: first
 * @var QuicPacketNs::parse_long_header_args  what parse_long_header takes: buf, len, out
 * @var QuicPacketNs::build_long_header_args  what build_long_header takes: out, cap, type, version,
 * @var QuicPacketNs::parse_short_header_args  what parse_short_header takes: buf, len, dcid_len, out
 * @var QuicPacketNs::build_version_negotiation_args  what build_version_negotiation takes: out, cap, dcid,
 * @var QuicPacketNs::pn_length_args  what pn_length takes: full_pn, largest_acked
 * @var QuicPacketNs::pn_encode_args  what pn_encode takes: out, cap, full_pn, largest_acked
 * @var QuicPacketNs::pn_decode_args  what pn_decode takes: largest_pn, truncated_pn, pn_nbits
 * @var QuicPacketNs::ok  a call's true/false outcome
 * @var QuicPacketNs::n  bytes written, or 0 on overflow / bad length
 * @var QuicPacketNs::u8  what a call reports
 * @var QuicPacketNs::u64  what a call reports
 * @var QuicPacketNs::is_long_header  true if byte 0 selects the long header form (0x80 set)
 * @var QuicPacketNs::parse_long_header  parse a long header. false if truncated or a connection ID exceeds ...
 * @var QuicPacketNs::build_long_header  build a long header's invariant fields (first byte .. Source ...
 * @var QuicPacketNs::parse_short_header  parse a short (1-RTT) header given the locally chosen dcid_len. ...
 * @var QuicPacketNs::build_version_negotiation  build a Version Negotiation packet (RFC 9000 sec 17.2.1): Version 0 ...
 * @var QuicPacketNs::pn_length  packet-number length in bytes (1..4) for full_pn; largest_acked < 0 ...
 * @var QuicPacketNs::pn_encode  encode full_pn truncated to ::QuicPacketNs::pn_length bytes, ...
 * @var QuicPacketNs::pn_decode  recover the full packet number from a truncated_pn of pn_nbits bits ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    QuicPacketIsLongHeaderArgs is_long_header_args;
    QuicPacketParseLongHeaderArgs parse_long_header_args;
    QuicPacketBuildLongHeaderArgs build_long_header_args;
    QuicPacketParseShortHeaderArgs parse_short_header_args;
    QuicPacketBuildVersionNegotiationArgs build_version_negotiation_args;
    QuicPacketPnLengthArgs pn_length_args;
    QuicPacketPnEncodeArgs pn_encode_args;
    QuicPacketPnDecodeArgs pn_decode_args;
    proto_bool ok;
    size_t n;
    uint8_t u8;
    uint64_t u64;
} QuicPacketVars;

/** @brief The operands and the outcome. */
extern QuicPacketVars QuicPacketV;

/** @brief The entries. */
typedef struct
{
    void (*const is_long_header)(uint8_t *restrict work);
    void (*const parse_long_header)(uint8_t *restrict work);
    void (*const build_long_header)(uint8_t *restrict work);
    void (*const parse_short_header)(uint8_t *restrict work);
    void (*const build_version_negotiation)(uint8_t *restrict work);
    void (*const pn_length)(uint8_t *restrict work);
    void (*const pn_encode)(uint8_t *restrict work);
    void (*const pn_decode)(uint8_t *restrict work);
} QuicPacketNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in QuicPacketV or a region of the borrow at a fixed offset.
void protocore_quic_packet_is_long_header(uint8_t *restrict work);
void protocore_quic_packet_parse_long_header(uint8_t *restrict work);
void protocore_quic_packet_build_long_header(uint8_t *restrict work);
void protocore_quic_packet_parse_short_header(uint8_t *restrict work);
void protocore_quic_packet_build_version_negotiation(uint8_t *restrict work);
void protocore_quic_packet_pn_length(uint8_t *restrict work);
void protocore_quic_packet_pn_encode(uint8_t *restrict work);
void protocore_quic_packet_pn_decode(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `QuicPacket.is_long_header(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const QuicPacketNs QuicPacket __attribute__((unused)) = {
    .is_long_header = protocore_quic_packet_is_long_header,
    .parse_long_header = protocore_quic_packet_parse_long_header,
    .build_long_header = protocore_quic_packet_build_long_header,
    .parse_short_header = protocore_quic_packet_parse_short_header,
    .build_version_negotiation = protocore_quic_packet_build_version_negotiation,
    .pn_length = protocore_quic_packet_pn_length,
    .pn_encode = protocore_quic_packet_pn_encode,
    .pn_decode = protocore_quic_packet_pn_decode,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3

#endif // PROTOCORE_QUIC_PACKET_H
