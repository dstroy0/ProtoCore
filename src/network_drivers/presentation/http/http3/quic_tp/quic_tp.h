// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_quic_tp.h
 * @brief QUIC transport parameters (RFC 9000 sec 18) carried in the TLS quic_transport_parameters
 *        extension (RFC 9001 sec 8.2).
 *
 * Each endpoint states its transport limits (flow-control windows, stream limits, idle timeout, and
 * the connection IDs used during the handshake) as a sequence of `ID (varint) | Length (varint) |
 * Value` parameters. The server carries its parameters in the EncryptedExtensions message and reads
 * the client's from the ClientHello. This module encodes the set a minimal server advertises and
 * parses a peer's, applying the RFC 9000 sec 18.2 defaults and rejecting malformed or illegal values
 * (bad varints, oversized connection IDs, a duplicated parameter, out-of-range limits).
 *
 * Pure, zero heap, host-tested (round-trip + the spec defaults + malformed-input rejection).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_QUIC_TP_H
#define PROTOCORE_QUIC_TP_H

#include "network_drivers/presentation/http/http3/quic_packet/quic_packet.h" // the complete type a public struct below holds by value

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HTTP3

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief Transport parameter identifiers (RFC 9000 sec 18.2 / Table 7). */
#define QUIC_TP_ORIGINAL_DCID 0x00              ///< server: DCID of the client's first Initial
#define QUIC_TP_MAX_IDLE_TIMEOUT 0x01           ///< varint, milliseconds (0 = disabled)
#define QUIC_TP_STATELESS_RESET_TOKEN 0x02      ///< 16 bytes (server only)
#define QUIC_TP_MAX_UDP_PAYLOAD_SIZE 0x03       ///< varint, default 65527, min 1200
#define QUIC_TP_INITIAL_MAX_DATA 0x04           ///< varint, connection flow-control window
#define QUIC_TP_INITIAL_MAX_SD_BIDI_LOCAL 0x05  ///< varint
#define QUIC_TP_INITIAL_MAX_SD_BIDI_REMOTE 0x06 ///< varint
#define QUIC_TP_INITIAL_MAX_SD_UNI 0x07         ///< varint
#define QUIC_TP_INITIAL_MAX_STREAMS_BIDI 0x08   ///< varint
#define QUIC_TP_INITIAL_MAX_STREAMS_UNI 0x09    ///< varint
#define QUIC_TP_ACK_DELAY_EXPONENT 0x0a         ///< varint, default 3, max 20
#define QUIC_TP_MAX_ACK_DELAY 0x0b              ///< varint, default 25, < 2^14
#define QUIC_TP_DISABLE_ACTIVE_MIGRATION 0x0c   ///< zero-length flag
#define QUIC_TP_ACTIVE_CID_LIMIT 0x0e           ///< varint, default 2, min 2
#define QUIC_TP_INITIAL_SCID 0x0f               ///< SCID of this endpoint's first Initial
#define QUIC_TP_RETRY_SCID 0x10                 ///< server: SCID of a Retry it sent

/** @brief The transport parameters we encode / decode, with RFC 9000 sec 18.2 defaults applied. */
typedef struct
{
    proto_bool has_original_dcid;
    uint8_t original_dcid[QUIC_MAX_CID_LEN];
    uint8_t original_dcid_len;
    proto_bool has_initial_scid;
    uint8_t initial_scid[QUIC_MAX_CID_LEN];
    uint8_t initial_scid_len;
    proto_bool has_retry_scid;
    uint8_t retry_scid[QUIC_MAX_CID_LEN];
    uint8_t retry_scid_len;

    uint64_t max_idle_timeout;           ///< default 0
    uint64_t max_udp_payload_size;       ///< default 65527
    uint64_t initial_max_data;           ///< default 0
    uint64_t initial_max_sd_bidi_local;  ///< default 0
    uint64_t initial_max_sd_bidi_remote; ///< default 0
    uint64_t initial_max_sd_uni;         ///< default 0
    uint64_t initial_max_streams_bidi;   ///< default 0
    uint64_t initial_max_streams_uni;    ///< default 0
    uint64_t ack_delay_exponent;         ///< default 3
    uint64_t max_ack_delay;              ///< default 25 (ms)
    uint64_t active_connection_id_limit; ///< default 2
    proto_bool disable_active_migration; ///< default false
} QuicTransportParams;

/** @brief What defaults takes: tp. */
typedef struct
{
    QuicTransportParams *tp;
} QuicTpDefaultsArgs;

/** @brief What encode takes: tp, out, cap. */
typedef struct
{
    const QuicTransportParams *tp;
    uint8_t *out;
    size_t cap;
} QuicTpEncodeArgs;

/** @brief What parse takes: buf, len, tp. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    QuicTransportParams *tp;
} QuicTpParseArgs;

/**
 * @brief QUIC transport parameters (RFC 9000 sec 18) carried in the TLS quic_transport_parameters extension
 * (RFC 9001 sec 8.2).
 *
 * A caller sets the members a call takes, invokes it through ::QuicTp with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   QuicTp.defaults_args.tp = ...;
 *   QuicTp.defaults(work);
 *
 * @var QuicTpNs::defaults_args  what defaults takes: tp
 * @var QuicTpNs::encode_args  what encode takes: tp, out, cap
 * @var QuicTpNs::parse_args  what parse takes: buf, len, tp
 * @var QuicTpNs::ok  a call's true/false outcome
 * @var QuicTpNs::n  the count a call reports
 * @var QuicTpNs::defaults  fill tp with the RFC 9000 sec 18.2 default values (all connection ...
 * @var QuicTpNs::encode  encode the server's transport parameters into out. Emits, in ...
 * @var QuicTpNs::parse  parse a peer's transport parameters (starting from the spec ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    QuicTpDefaultsArgs defaults_args;
    QuicTpEncodeArgs encode_args;
    QuicTpParseArgs parse_args;
    proto_bool ok;
    size_t n;
} QuicTpVars;

/** @brief The operands and the outcome. */
extern QuicTpVars QuicTpV;

/** @brief The entries. */
typedef struct
{
    void (*const defaults)(uint8_t *restrict work);
    void (*const encode)(uint8_t *restrict work);
    void (*const parse)(uint8_t *restrict work);
} QuicTpNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in QuicTpV or a region of the borrow at a fixed offset.
void protocore_quic_tp_defaults(uint8_t *restrict work);
void protocore_quic_tp_encode(uint8_t *restrict work);
void protocore_quic_tp_parse(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `QuicTp.defaults(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const QuicTpNs QuicTp __attribute__((unused)) = {
    .defaults = protocore_quic_tp_defaults,
    .encode = protocore_quic_tp_encode,
    .parse = protocore_quic_tp_parse,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3

#endif // PROTOCORE_QUIC_TP_H
