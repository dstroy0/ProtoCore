// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_quic_tp.h
 * @brief QUIC transport parameters (RFC 9000 sec 18) carried in the TLS pc_quic_transport_parameters
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

#include "protocore_config.h"

#if PC_ENABLE_HTTP3

#include "network_drivers/presentation/http/http3/quic_packet.h" // QUIC_MAX_CID_LEN
#include <stddef.h>
#include <stdint.h>

/** @brief Transport parameter identifiers (RFC 9000 sec 18.2 / Table 7). */
struct QuicTp
{
    static constexpr uint8_t QUIC_TP_ORIGINAL_DCID = 0x00;              ///< server: DCID of the client's first Initial
    static constexpr uint8_t QUIC_TP_MAX_IDLE_TIMEOUT = 0x01;           ///< varint, milliseconds (0 = disabled)
    static constexpr uint8_t QUIC_TP_STATELESS_RESET_TOKEN = 0x02;      ///< 16 bytes (server only)
    static constexpr uint8_t QUIC_TP_MAX_UDP_PAYLOAD_SIZE = 0x03;       ///< varint, default 65527, min 1200
    static constexpr uint8_t QUIC_TP_INITIAL_MAX_DATA = 0x04;           ///< varint, connection flow-control window
    static constexpr uint8_t QUIC_TP_INITIAL_MAX_SD_BIDI_LOCAL = 0x05;  ///< varint
    static constexpr uint8_t QUIC_TP_INITIAL_MAX_SD_BIDI_REMOTE = 0x06; ///< varint
    static constexpr uint8_t QUIC_TP_INITIAL_MAX_SD_UNI = 0x07;         ///< varint
    static constexpr uint8_t QUIC_TP_INITIAL_MAX_STREAMS_BIDI = 0x08;   ///< varint
    static constexpr uint8_t QUIC_TP_INITIAL_MAX_STREAMS_UNI = 0x09;    ///< varint
    static constexpr uint8_t QUIC_TP_ACK_DELAY_EXPONENT = 0x0a;         ///< varint, default 3, max 20
    static constexpr uint8_t QUIC_TP_MAX_ACK_DELAY = 0x0b;              ///< varint, default 25, < 2^14
    static constexpr uint8_t QUIC_TP_DISABLE_ACTIVE_MIGRATION = 0x0c;   ///< zero-length flag
    static constexpr uint8_t QUIC_TP_ACTIVE_CID_LIMIT = 0x0e;           ///< varint, default 2, min 2
    static constexpr uint8_t QUIC_TP_INITIAL_SCID = 0x0f;               ///< SCID of this endpoint's first Initial
    static constexpr uint8_t QUIC_TP_RETRY_SCID = 0x10;                 ///< server: SCID of a Retry it sent
};

/** @brief The transport parameters we encode / decode, with RFC 9000 sec 18.2 defaults applied. */
struct QuicTransportParams
{
    bool has_original_dcid;
    uint8_t original_dcid[QUIC_MAX_CID_LEN];
    uint8_t original_dcid_len;
    bool has_initial_scid;
    uint8_t initial_scid[QUIC_MAX_CID_LEN];
    uint8_t initial_scid_len;
    bool has_retry_scid;
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
    bool disable_active_migration;       ///< default false
};

/** @brief Fill @p tp with the RFC 9000 sec 18.2 default values (all connection IDs absent). */
void pc_quic_tp_defaults(QuicTransportParams *tp);

/**
 * @brief Encode the server's transport parameters into @p out.
 *
 * Emits, in ascending ID order: original_destination_connection_id and initial_source_connection_id
 * (only if present), initial_max_data, the three initial_max_stream_data_* windows, the two
 * initial_max_streams_* limits, max_idle_timeout, max_udp_payload_size, active_connection_id_limit,
 * and disable_active_migration (only if set). @return bytes written, or 0 on overflow.
 */
size_t pc_quic_tp_encode(const QuicTransportParams *tp, uint8_t *out, size_t cap);

/**
 * @brief Parse a peer's transport parameters (starting from the spec defaults).
 *
 * Walks ID/Length/Value triples, stores the parameters this module knows, and skips unknown IDs
 * (forward compatibility / GREASE). @return false on a malformed encoding, an oversized connection
 * ID, a duplicated known parameter, or an out-of-range value (RFC 9000 sec 18.2 limits):
 * ack_delay_exponent > 20, max_ack_delay >= 2^14, max_udp_payload_size < 1200,
 * active_connection_id_limit < 2, or a non-zero-length disable_active_migration.
 */
bool pc_quic_tp_parse(const uint8_t *buf, size_t len, QuicTransportParams *tp);

#endif // PC_ENABLE_HTTP3
#endif // PROTOCORE_QUIC_TP_H
