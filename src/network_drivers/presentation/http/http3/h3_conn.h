// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file h3_conn.h
 * @brief HTTP/3 application engine over QUIC streams (RFC 9114).
 *
 * Sits on top of the QUIC transport engine (::QuicConn) and speaks HTTP/3: on the handshake
 * completing it opens the server's unidirectional control stream (sending SETTINGS) and the two
 * QPACK encoder / decoder streams, reads the client's control stream, and on each client-initiated
 * bidirectional request stream reassembles the HTTP/3 frames - QPACK-decoding the HEADERS field
 * section into a request (method / path / authority + a small header set) and collecting the DATA
 * body - then hands the finished request to the application through a callback.
 * @ref H3ConnNs::respond serializes a response (HEADERS + DATA) back onto the request stream and
 * finishes it.
 *
 * QPACK is static-table only (we advertise QPACK_MAX_TABLE_CAPACITY = 0), so no dynamic-table state
 * is needed and the encoder / decoder streams carry only their stream-type byte. Fixed storage, no
 * heap; host-testable by feeding it decoded stream bytes through the ::QuicConn callback seam.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_H3_CONN_H
#define PROTOCORE_H3_CONN_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HTTP3

PROTOCORE_BEGIN_DECLS

// PROTOCORE_H3_CONN_BORROW - the bytes one connection runs out of - is stated in
// protocore_config.h, which sums it into the plaintext arena. The connection carries no key
// material, so its context leads that one span rather than taking a secure one of its own. A caller
// takes the span once and binds it; how it is carved is this module's and is never named here.

/**
 * @brief A completed request delivered to the application.
 * @param body / body_len  the request body (may be empty); valid only during the call.
 */
typedef void (*H3RequestFn)(void *app, uint8_t *h3, uint64_t stream_id, const char *method, const char *path,
                            const char *authority, const uint8_t *body, size_t body_len);

/** @brief The span a connection runs out of, and the QUIC transport under it. */
typedef struct
{
    uint8_t *b;  ///< PROTOCORE_H3_CONN_BORROW plaintext bytes; the context, then the streams
    uint8_t *qc; ///< the QUIC connection's context span (::QuicConnNs::bind, member ctx)
} H3ConnBind;

/** @brief What the application is told about, and the opaque it is told with. */
typedef struct
{
    H3RequestFn on_request; ///< invoked for each completed request
    void *app;              ///< opaque, passed back to the callback
} H3ConnAppArgs;

/** @brief One response, serialized onto a request stream. */
typedef struct
{
    uint64_t stream_id;       ///< the request stream it answers
    int status;               ///< HTTP status code
    const char *content_type; ///< optional; NULL omits the field
    const uint8_t *body;      ///< the body (may be empty)
    size_t body_len;          ///< its length
} H3ConnRespondArgs;

/** @brief The connection's own calls, described only in h3_conn.c. */
struct H3ConnInternal;

/**
 * @brief HTTP/3 server connection (RFC 9114).
 *
 * A caller binds the span and the QUIC connection under it, sets the members a call takes, invokes
 * it through ::H3Conn, and reads the outcome off the same handle.
 *
 *   H3Conn.bind.b = byte_span;
 *   H3Conn.bind.qc = quic_ctx_span;
 *   H3Conn.app_args.on_request = on_request;
 *   H3Conn.app_args.app = app;
 *   H3Conn.init(H3Conn.internal);
 *   H3Conn.respond_args.stream_id = id;
 *   H3Conn.respond_args.status = 200;
 *   H3Conn.respond(H3Conn.internal);
 *
 * @var H3ConnNs::bind          the span this connection runs out of, and the QUIC connection under it
 * @var H3ConnNs::app_args      what the application is told about
 * @var H3ConnNs::respond_args  one response, serialized onto a request stream
 * @var H3ConnNs::ok            a call's true/false outcome
 * @var H3ConnNs::init          open the connection and install its hooks on the bound QUIC connection
 * @var H3ConnNs::respond       serialize HEADERS + DATA onto a request stream and finish it
 *
 * @ref H3ConnNs::init overwrites the bound QUIC connection's callbacks with this engine's hooks, so
 * the application callback goes in @ref H3ConnNs::app_args rather than on the transport.
 *
 * The bound span is the CALLER's, at an address it knows. The caller releases it, and the pool wipes
 * on release; this module neither takes it, holds it past the connection, nor releases it. The span
 * IS the connection, so two connections are two spans and never collide.
 *
 * No storage member: a caller binds, sets operands and reads @ref H3ConnNs::ok, and that is all the
 * surface there is.
 */
typedef struct
{
    H3ConnBind bind;
    H3ConnAppArgs app_args;
    H3ConnRespondArgs respond_args;

    proto_bool ok;

    void (*const init)(struct H3ConnInternal *ctx);
    void (*const respond)(struct H3ConnInternal *ctx);

    struct H3ConnInternal *internal;
} H3ConnNs;

/** @brief The one symbol this module exports. */
extern H3ConnNs H3Conn;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3

#endif // PROTOCORE_H3_CONN_H
