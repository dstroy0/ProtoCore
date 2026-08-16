// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file quic_conn.h
 * @brief Stateful QUIC v1 server connection engine (RFC 9000 / RFC 9001).
 *
 * One connection: each inbound UDP datagram is parsed into its coalesced packets, header and AEAD
 * protection removed at the right encryption level (Initial keys from the client's Destination
 * Connection ID, Handshake and 1-RTT keys from the TLS handshake this runs), the frames dispatched,
 * the CRYPTO stream reassembled to advance the handshake, packet numbers tracked to generate ACKs,
 * and the outbound Initial / Handshake / 1-RTT packets coalesced back into datagrams. Application
 * streams surface through the callbacks below, so the transport engine has no HTTP dependency.
 *
 * Transport-free (no lwIP): @ref QuicConnNs::recv takes a received datagram and @ref QuicConnNs::send
 * pulls the next one to transmit, so the engine is host-testable by shuttling byte buffers between a
 * server connection and a client written in the test. protocore_quic_server wires it to protocore_udp.
 *
 * Scope (a faithful minimal server): QUIC v1 only, no Retry / 0-RTT / key update / connection
 * migration / connection-ID rotation, in-order CRYPTO and stream reassembly, and single-range ACKs.
 * Loss recovery is a Probe Timeout (RFC 9002): @ref QuicConnNs::on_timeout retransmits the
 * outstanding ack-eliciting flight with exponential backoff, reset on acknowledged progress. Fatal
 * handshake / frame errors emit a transport CONNECTION_CLOSE (RFC 9000 sec 19.19) instead of timing
 * out. Small packets are padded to the header-protection minimum (RFC 9001 sec 5.4.2). Fixed
 * storage, no heap.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_QUIC_CONN_H
#define PROTOCORE_QUIC_CONN_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HTTP3

PROTOCORE_BEGIN_DECLS

#ifndef PROTOCORE_QUIC_MAX_DATAGRAM
#define PROTOCORE_QUIC_MAX_DATAGRAM 1350 ///< largest UDP payload we send/accept (conservative < 1500 MTU)
#endif
#ifndef PROTOCORE_QUIC_STREAM_RX
#define PROTOCORE_QUIC_STREAM_RX 2048 ///< largest run of new in-order stream bytes delivered in one call
#endif
#ifndef PROTOCORE_QUIC_PTO_MS
#define PROTOCORE_QUIC_PTO_MS 1000 ///< base Probe Timeout for retransmitting the handshake flight (RFC 9002)
#endif

// A connection runs out of two spans, both stated in protocore_config.h, which sums each into its
// own arena: PROTOCORE_QUIC_CONN_CTX_BORROW secure bytes for the context, because the TLS handshake
// it holds is key material, and PROTOCORE_QUIC_CONN_BORROW plaintext bytes for what it owes each
// stream and the CRYPTO window per packet-number space. A caller takes both once and binds them.
// How each is carved is this module's and is never named here.

/** @brief The TLS server configuration a connection is initialised against; see quic_tls.h. */
struct QuicTlsConfig;

/** @brief The two spans one connection runs out of. */
typedef struct
{
    uint8_t *ctx; ///< PROTOCORE_QUIC_CONN_CTX_BORROW secure bytes; the connection's identity
    uint8_t *b;   ///< PROTOCORE_QUIC_CONN_BORROW plaintext bytes; stream TX and CRYPTO RX
} QuicConnBind;

/** @brief HTTP/3 (or test) hooks the engine drives. All nullable. */
typedef struct
{
    /** @brief In-order stream bytes arrived on @p stream_id (@p fin marks the final bytes). */
    void (*on_stream_data)(void *app, uint8_t *qc, uint64_t stream_id, const uint8_t *data, size_t len,
                           proto_bool fin);
    /** @brief The handshake completed (client Finished verified); 1-RTT is open. */
    void (*on_handshake_done)(void *app, uint8_t *qc);
    void *app; ///< opaque, passed back to the callbacks
} QuicConnCallbacks;

/** @brief The client's first Initial packet, as a connection is opened from it. */
typedef struct
{
    const struct QuicTlsConfig *cfg; ///< cert / key / transport params; the engine fills the two connection ids
    const uint8_t *odcid;            ///< the Destination Connection ID from that Initial (Initial keys)
    uint8_t odcid_len;               ///< its length
    const uint8_t *peer_scid;        ///< the client's Source Connection ID (our DCID toward it)
    uint8_t peer_scid_len;           ///< its length
    const uint8_t *our_scid;         ///< the connection ID we choose for ourselves (its DCID toward us)
    uint8_t our_scid_len;            ///< its length
} QuicConnInitArgs;

/** @brief One received datagram (one or more coalesced QUIC packets). */
typedef struct
{
    const uint8_t *datagram; ///< the bytes as they arrived
    size_t len;              ///< how many
} QuicConnRecvArgs;

/** @brief Where the next outbound datagram lands. */
typedef struct
{
    uint8_t *out; ///< the buffer
    size_t cap;   ///< its capacity
} QuicConnSendArgs;

/** @brief The caller's monotonic clock, read for loss recovery. */
typedef struct
{
    uint32_t now_ms; ///< milliseconds; this engine keeps no clock of its own
} QuicConnTimeoutArgs;

/** @brief Bytes queued to send on one stream. */
typedef struct
{
    uint64_t stream_id;  ///< the stream
    const uint8_t *data; ///< the bytes
    size_t len;          ///< how many
    proto_bool fin;      ///< finish the stream after these bytes
} QuicConnStreamSendArgs;

/** @brief The error a close reports. */
typedef struct
{
    uint64_t error_code; ///< RFC 9000 sec 20.1, or the application's own space for close_app
} QuicConnCloseArgs;

/** @brief The connection's own calls, described only in quic_conn.c. */
struct QuicConnInternal;

/**
 * @brief QUIC v1 server connection (RFC 9000 / RFC 9001).
 *
 * A caller binds the two spans once, sets the members a call takes, invokes it through ::QuicConn,
 * and reads the outcome off the same handle.
 *
 *   QuicConn.bind.ctx = ctx_span;
 *   QuicConn.bind.b = byte_span;
 *   QuicConn.init_args.cfg = cfg;
 *   QuicConn.init(QuicConn.internal);
 *   QuicConn.recv_args.datagram = pkt;
 *   QuicConn.recv_args.len = pkt_len;
 *   QuicConn.recv(QuicConn.internal);
 *
 * @var QuicConnNs::bind              the two spans one connection runs out of
 * @var QuicConnNs::cb                the hooks the engine drives
 * @var QuicConnNs::init_args         the client's first Initial, as a connection is opened from it
 * @var QuicConnNs::recv_args         one received datagram
 * @var QuicConnNs::send_args         where the next outbound datagram lands
 * @var QuicConnNs::timeout_args      the caller's monotonic clock
 * @var QuicConnNs::stream_send_args  bytes queued to send on one stream
 * @var QuicConnNs::close_args        the error a close reports
 * @var QuicConnNs::ok                a call's true/false outcome
 * @var QuicConnNs::n                 bytes the last send or stream_send produced
 * @var QuicConnNs::established       whether the handshake has completed (client Finished verified)
 * @var QuicConnNs::closed            whether the connection is closed or draining
 * @var QuicConnNs::init              open a server connection from the client's first Initial
 * @var QuicConnNs::callbacks         install @ref QuicConnNs::cb on the bound connection
 * @var QuicConnNs::recv              process one datagram: frames drive the handshake, ACKs and streams
 * @var QuicConnNs::send              build the next outbound datagram; @ref QuicConnNs::n is 0 when idle
 * @var QuicConnNs::on_timeout        drive loss recovery; call once per poll before @ref QuicConnNs::send
 * @var QuicConnNs::stream_send       queue bytes on a stream
 * @var QuicConnNs::close             queue a transport CONNECTION_CLOSE (RFC 9000 sec 19.19)
 * @var QuicConnNs::close_app         queue the application variant (0x1d), error code in the app's space
 * @var QuicConnNs::is_established    read the handshake state into @ref QuicConnNs::established
 * @var QuicConnNs::is_closed         read the close state into @ref QuicConnNs::closed
 *
 * @ref QuicConnNs::send is called repeatedly until @ref QuicConnNs::n is 0, and honors the
 * pre-validation 3x anti-amplification limit.
 *
 * The bound spans are the CALLER's, at addresses it knows. The caller releases them, and each pool
 * wipes on release; this module neither takes them, holds them past the connection, nor releases
 * them. The context span IS the connection, so two connections are two spans and never collide.
 *
 * No storage member: a caller binds, sets operands and reads @ref QuicConnNs::ok, and that is all
 * the surface there is.
 */
typedef struct
{
    QuicConnBind bind;
    QuicConnCallbacks cb;
    QuicConnInitArgs init_args;
    QuicConnRecvArgs recv_args;
    QuicConnSendArgs send_args;
    QuicConnTimeoutArgs timeout_args;
    QuicConnStreamSendArgs stream_send_args;
    QuicConnCloseArgs close_args;

    proto_bool ok;
    size_t n;
    proto_bool established;
    proto_bool closed;

    void (*const init)(struct QuicConnInternal *ctx);
    void (*const callbacks)(struct QuicConnInternal *ctx);
    void (*const recv)(struct QuicConnInternal *ctx);
    void (*const send)(struct QuicConnInternal *ctx);
    void (*const on_timeout)(struct QuicConnInternal *ctx);
    void (*const stream_send)(struct QuicConnInternal *ctx);
    void (*const close)(struct QuicConnInternal *ctx);
    void (*const close_app)(struct QuicConnInternal *ctx);
    void (*const is_established)(struct QuicConnInternal *ctx);
    void (*const is_closed)(struct QuicConnInternal *ctx);

    struct QuicConnInternal *internal;
} QuicConnNs;

/** @brief The one symbol this module exports. */
extern QuicConnNs QuicConn;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3

#endif // PROTOCORE_QUIC_CONN_H
