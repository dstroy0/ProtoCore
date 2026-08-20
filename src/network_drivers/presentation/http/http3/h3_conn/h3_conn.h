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

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

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

/** @brief The QUIC transport under this connection. The connection's own storage is the borrow. */
typedef struct
{
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

/**
 * @brief HTTP/3 server connection (RFC 9114).
 *
 * The connection's storage is the CALLER's: it arrives as the entry's borrow, and this file lays
 * its context and per-stream buffers out at compile-time offsets inside it. A caller binds the QUIC
 * connection under it, sets the members a call takes, invokes it through ::H3Conn with those bytes,
 * and reads the outcome off the same handle.
 *
 *   H3Conn.bind.qc = quic_ctx_span;
 *   H3Conn.app_args.on_request = on_request;
 *   H3Conn.app_args.app = app;
 *   H3Conn.init(byte_span);
 *   H3Conn.respond_args.stream_id = id;
 *   H3Conn.respond_args.status = 200;
 *   H3Conn.respond(byte_span);
 *
 * @var H3ConnNs::bind          the QUIC connection under this one; the storage is the borrow
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
} H3ConnVars;

/** @brief The operands and the outcome. */
extern H3ConnVars H3ConnV;

/** @brief The entries. */
typedef struct
{
    void (*const init)(uint8_t *restrict work);
    void (*const respond)(uint8_t *restrict work);
} H3ConnNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in H3ConnV or a region of the borrow at a fixed offset.
void protocore_h3_conn_init(uint8_t *restrict work);
void protocore_h3_conn_respond(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `H3Conn.init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const H3ConnNs H3Conn __attribute__((unused)) = {
    .init = protocore_h3_conn_init,
    .respond = protocore_h3_conn_respond,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3

#endif // PROTOCORE_H3_CONN_H
