// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_quic_conn.h
 * @brief Stateful QUIC v1 server connection engine (RFC 9000 / RFC 9001).
 *
 * One QuicConn drives a single QUIC connection: it parses each inbound UDP datagram into its
 * coalesced packets, removes header + AEAD protection at the right encryption level (Initial keys
 * from the client's Destination Connection ID; Handshake and 1-RTT keys from the TLS handshake it
 * runs via protocore_quic_tls), dispatches the frames, reassembles the CRYPTO stream to advance the handshake,
 * tracks packet numbers to generate ACKs, and coalesces the outbound Initial / Handshake / 1-RTT
 * packets back into datagrams. Application streams are surfaced to HTTP/3 (protocore_h3_conn) through a small
 * callback + send API, so the transport engine has no HTTP dependency.
 *
 * It is transport-free (no lwIP): protocore_quic_conn_recv() takes a received datagram and protocore_quic_conn_send()
 * pulls the next datagram to transmit, so the engine is host-testable by shuttling byte buffers
 * between a server QuicConn and a client written in the test. protocore_quic_server wires it to protocore_udp.
 *
 * Scope (a faithful minimal server): QUIC v1 only, no Retry / 0-RTT / key update / connection
 * migration / connection-ID rotation, in-order CRYPTO and stream reassembly, and single-range ACKs.
 * Loss recovery is a Probe Timeout (RFC 9002): protocore_quic_conn_on_timeout() retransmits the outstanding
 * ack-eliciting flight (handshake CRYPTO and 1-RTT streams) with exponential backoff, reset on
 * acknowledged progress. Fatal handshake / frame errors emit a transport CONNECTION_CLOSE (RFC 9000
 * sec 19.19) instead of timing out. Small packets are padded to the header-protection minimum
 * (RFC 9001 sec 5.4.2). Fixed storage, no heap.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_QUIC_CONN_H
#define PROTOCORE_QUIC_CONN_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_HTTP3

#include "network_drivers/presentation/http/http3/quic_crypto.h"
#include "network_drivers/presentation/http/http3/quic_tls.h"

#ifndef PROTOCORE_QUIC_MAX_DATAGRAM
#define PROTOCORE_QUIC_MAX_DATAGRAM 1350 ///< largest UDP payload we send/accept (conservative < 1500 MTU)
#endif
#ifndef PROTOCORE_QUIC_STREAM_RX
#define PROTOCORE_QUIC_STREAM_RX 2048 ///< largest run of new in-order stream bytes delivered in one call
#endif
#ifndef PROTOCORE_QUIC_PTO_MS
#define PROTOCORE_QUIC_PTO_MS 1000 ///< base Probe Timeout for retransmitting the handshake flight (RFC 9002)
#endif

/** @brief Per-connection stream state (client-initiated + server-initiated). */
typedef struct
{
    uint64_t id;            ///< stream id (UINT64_MAX = free slot)
    uint64_t rx_off;        ///< next in-order byte offset expected
    uint64_t tx_off;        ///< next send offset
    proto_bool rx_fin;      ///< a FIN was received (final size known)
    proto_bool tx_fin;      ///< a FIN should be sent after the buffered tx bytes
    proto_bool tx_fin_sent; ///< the FIN has been sent
    uint8_t *tx;            ///< PROTOCORE_QUIC_STREAM_TX bytes of the connection's borrow
    size_t tx_have;         ///< bytes buffered to send
    size_t tx_sent;         ///< bytes of tx already put on the wire
} QuicStream;

struct QuicConn;

/** @brief HTTP/3 (or test) hooks the engine drives. All nullable. */
typedef struct
{
    /** @brief In-order stream bytes arrived on @p stream_id (@p fin marks the final bytes). */
    void (*on_stream_data)(void *app, struct QuicConn *qc, uint64_t stream_id, const uint8_t *data, size_t len,
                           proto_bool fin);
    /** @brief The handshake completed (client Finished verified); 1-RTT is open. */
    void (*on_handshake_done)(void *app, struct QuicConn *qc);
    void *app; ///< opaque, passed back to the callbacks
} QuicConnCallbacks;

/** @brief One packet-number space (Initial / Handshake / Application). */
typedef struct
{
    uint64_t next_pn;            ///< next packet number to send in this space
    int64_t largest_acked;       ///< largest of our PNs the peer has acknowledged (-1 = none)
    int64_t last_ae_pn;          ///< PN of the last ack-eliciting packet we sent here (-1 = none); loss recovery
    uint64_t largest_rx;         ///< largest PN received in this space
    proto_bool have_rx;          ///< at least one packet received
    proto_bool ack_eliciting_rx; ///< an ack-eliciting packet is unacknowledged (we owe an ACK)
    proto_bool discarded;        ///< this space's keys have been dropped (nothing more sent/received)
    uint64_t crypto_rx_off;      ///< in-order CRYPTO bytes already delivered to protocore_quic_tls
    uint8_t *crypto_rx;          ///< PROTOCORE_QUIC_CRYPTO_RX bytes of the connection's borrow
    size_t crypto_rx_have;       ///< contiguous CRYPTO bytes buffered at crypto_rx_off
    uint64_t crypto_tx_off;      ///< CRYPTO flight bytes already sent from this level
} QuicPnSpace;

/**
 * @brief This module's draw on the plaintext pool, declared here and asserted in quic_conn.c.
 *
 * One borrow per connection from the pool's persistent end, grouped by field so every stride is a
 * power of two: PROTOCORE_QUIC_MAX_STREAMS outbound stream buffers, then one CRYPTO reassembly window per
 * packet-number space. Both carry what the peer sent us and what we owe it, which is the
 * connection's working set for its life.
 */
#define PROTOCORE_QUIC_CONN_BORROW                                                                                     \
    (((size_t)PROTOCORE_QUIC_MAX_STREAMS * PROTOCORE_QUIC_STREAM_TX) + (3u * (size_t)PROTOCORE_QUIC_CRYPTO_RX))

/** @brief One QUIC connection's engine state (fixed storage, no heap). */
typedef struct QuicConn
{
    uint8_t scid[QUIC_MAX_CID_LEN]; ///< our connection ID (peer's DCID toward us)
    uint8_t scid_len;
    uint8_t dcid[QUIC_MAX_CID_LEN]; ///< peer's connection ID (our DCID toward the peer)
    uint8_t dcid_len;
    uint8_t odcid[QUIC_MAX_CID_LEN]; ///< client's original DCID (Initial keys + transport param)
    uint8_t odcid_len;

    QuicInitialSecrets initial; ///< Initial keys derived from odcid
    QuicTls tls;                ///< the TLS 1.3 handshake

    QuicPnSpace space[3]; ///< indexed by QUIC_ENC_*

    QuicStream streams[PROTOCORE_QUIC_MAX_STREAMS];

    QuicConnCallbacks cb;

    proto_bool handshake_done_queued; ///< a HANDSHAKE_DONE frame still needs sending
    proto_bool handshake_done_sent;
    proto_bool closed;            ///< a CONNECTION_CLOSE has been sent or received
    proto_bool draining;          ///< peer closed; we only drain
    uint64_t recv_bytes;          ///< total bytes received (anti-amplification budget)
    uint64_t sent_bytes;          ///< total bytes sent before address validation
    proto_bool address_validated; ///< handshake-complete or received enough to lift the 3x limit

    proto_bool pto_armed;     ///< a Probe Timeout is running for the outstanding handshake flight
    uint8_t pto_count;        ///< consecutive PTO expirations (exponential backoff exponent)
    uint32_t pto_deadline_ms; ///< when the PTO fires (caller's monotonic ms; valid when pto_armed)

    proto_bool close_queued;   ///< a CONNECTION_CLOSE is owed to the peer (fatal error hit)
    proto_bool close_is_app;   ///< send the application variant (0x1d); its error code is the app's
    proto_bool close_sent;     ///< the CONNECTION_CLOSE has been put on the wire
    uint64_t close_error;      ///< error code to report (RFC 9000 sec 20.1, or the app's own space)
    uint64_t close_frame_type; ///< frame type that triggered it (0 when not frame-specific)
    uint8_t close_level;       ///< encryption level to send the close at (the peer holds those keys)
} QuicConn;

/**
 * @brief Initialize a server connection from the client's first Initial packet.
 *
 * @param cfg        TLS server config (cert / key / transport params); the engine fills the
 *                   original_destination_connection_id and initial_source_connection_id parameters.
 * @param odcid      the Destination Connection ID from the client's first Initial (Initial keys).
 * @param odcid_len  its length.
 * @param peer_scid  the client's Source Connection ID (our DCID toward the client).
 * @param peer_scid_len its length.
 * @param our_scid   the connection ID we choose for ourselves (peer's DCID toward us).
 * @param our_scid_len its length.
 * @param cb         stream / handshake callbacks (may be all NULL).
 */
void protocore_quic_conn_init(struct QuicConn *qc, const QuicTlsConfig *cfg, const uint8_t *odcid, uint8_t odcid_len,
                              const uint8_t *peer_scid, uint8_t peer_scid_len, const uint8_t *our_scid,
                              uint8_t our_scid_len, const QuicConnCallbacks *cb);

/**
 * @brief Process one received UDP datagram (one or more coalesced QUIC packets).
 * @return true if the datagram was processed (even partially); false if it was undecryptable /
 * malformed enough to drop entirely. Frames drive the handshake, ACK state, and stream callbacks.
 */
proto_bool protocore_quic_conn_recv(struct QuicConn *qc, const uint8_t *datagram, size_t len);

/**
 * @brief Build the next outbound datagram (coalesced Initial / Handshake / 1-RTT packets).
 * @return its length, or 0 when there is nothing to send right now. Call repeatedly until it
 * returns 0. Honors the pre-validation 3x anti-amplification limit.
 */
size_t protocore_quic_conn_send(struct QuicConn *qc, uint8_t *out, size_t cap);

/**
 * @brief Drive loss recovery: if the server's handshake CRYPTO flight is outstanding (built but not
 * yet acknowledged) and the Probe Timeout has elapsed, mark the flight for retransmission (RFC 9002)
 * so the next protocore_quic_conn_send() re-sends it, and back the timer off exponentially. @p now_ms is the
 * caller's monotonic millisecond clock (protocore_quic_conn stays clock-free). A no-op once the flight is
 * acknowledged or the handshake completes. Call once per poll before protocore_quic_conn_send().
 */
void protocore_quic_conn_on_timeout(struct QuicConn *qc, uint32_t now_ms);

/**
 * @brief Queue @p len bytes (with optional @p fin) to send on @p stream_id.
 * @return bytes accepted into the stream's send buffer (may be < len if it is full).
 */
size_t protocore_quic_conn_stream_send(struct QuicConn *qc, uint64_t stream_id, const uint8_t *data, size_t len,
                                       proto_bool fin);

/**
 * @brief Initiate an immediate close: queue a transport CONNECTION_CLOSE (RFC 9000 sec 19.19) with
 * @p error_code so the next protocore_quic_conn_send() reports the error to the peer instead of leaving it to
 * time out. A no-op if the connection is already closing. The engine also calls this itself on a fatal
 * handshake / frame error (CRYPTO_ERROR / FRAME_ENCODING_ERROR).
 */
void protocore_quic_conn_close(struct QuicConn *qc, uint64_t error_code);

/**
 * @brief Initiate an immediate close reporting an application-protocol error: a CONNECTION_CLOSE
 * of type 0x1d (RFC 9000 sec 19.19), whose @p error_code comes from the application's own space -
 * HTTP/3's codes (RFC 9114 sec 8.1) rather than the transport's. Before 1-RTT keys exist the frame
 * is not permitted, and a transport close carrying APPLICATION_ERROR goes instead.
 */
void protocore_quic_conn_close_app(struct QuicConn *qc, uint64_t error_code);

/** @brief True once the TLS handshake has completed (client Finished verified). */
proto_bool protocore_quic_conn_established(const struct QuicConn *qc);

/** @brief True if the connection is closed or draining. */
proto_bool protocore_quic_conn_is_closed(const struct QuicConn *qc);

#endif // PROTOCORE_ENABLE_HTTP3
#endif // PROTOCORE_QUIC_CONN_H
