// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file h3_server.h
 * @brief Bridge between the HTTP/3 server (protocore_quic_server) and the server's request pipeline.
 *
 * protocore_quic_server hands out a completed request as semantic fields. This module maps those fields
 * into the reserved dispatch slot's HttpReq, installs a response sink that frames the reply back
 * onto the originating QUIC stream, and runs the route table. The handler path is the one every
 * other protocol takes: send_text() writes through the sink rather than a pcb.
 *
 * The h2 side of the same seam is http2/h2_server.h.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_H3_SERVER_H
#define PROTOCORE_H3_SERVER_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_HTTP3

#include "network_drivers/presentation/http/http3/quic_server.h" // QuicServerRequestFn: the seam this fills

/**
 * @brief The request seam protocore_quic_server_begin() is handed.
 *
 * Maps @p method, @p path, @p authority and @p body onto the reserved dispatch slot and runs the
 * route table. The slot is released on return, so a handler that sends nothing leaves the stream
 * open. @p app is unused: the route table and the slot pools are single global owners.
 */
/** @brief RFC 9114: the QUIC connection and stream one request occupies. */
typedef struct
{
    uint32_t conn_id;   ///< the QUIC connection a response routes back on
    uint64_t stream_id; ///< the stream it is written on
} H3StreamRef;

/** @brief RFC 9114 sec 4.3.1 request pseudo-headers, and the body that follows them. */
typedef struct
{
    const char *method;    ///< the request method
    const char *path;      ///< its path, query included
    const char *authority; ///< its :authority, mapped to Host
    const uint8_t *body;   ///< its body bytes
    size_t body_len;       ///< how many
} H3ReqArgs;

/** @brief Where random bytes are drawn. */
typedef struct
{
    uint8_t *out; ///< where rng writes
    size_t len;   ///< how many bytes it draws
} H3RngArgs;

/** @brief The bridge's own state and the calls that reach it, described only in h3_server.c. */
struct H3ServerInternal;

/**
 * @brief The HTTP/3 request bridge: a QUIC stream's request, run through the route table.
 *
 * A caller sets the members a call takes and invokes it through ::H3Server.
 *
 * @var H3ServerNs::stream     the connection and stream a request arrived on
 * @var H3ServerNs::req        the request that stream carried
 * @var H3ServerNs::rng_args   where random bytes are drawn
 * @var H3ServerNs::request    map the request onto the reserved dispatch slot and run it
 * @var H3ServerNs::rng        fill rng_args.out with rng_args.len random bytes from the device source
 * @var H3ServerNs::internal   the bridge's state and the calls that reach it
 */
typedef struct
{
    H3StreamRef stream; ///< the connection and stream a request arrived on
    H3ReqArgs req;      ///< the request that stream carried
    H3RngArgs rng_args; ///< where random bytes are drawn

    void (*request)(struct H3ServerInternal *ctx);
    void (*rng)(struct H3ServerInternal *ctx);

    struct H3ServerInternal *internal;
} H3ServerNs;

/** @brief The one symbol this module exports. */
extern H3ServerNs H3Server;

// The QUIC server's seam dictates these two shapes, so they stay plain functions and carry their
// arguments onto the handle before the call that does the work.
void protocore_h3_server_request(void *app, uint32_t conn_id, uint64_t stream_id, const char *method, const char *path,
                                 const char *authority, const uint8_t *body, size_t body_len);

/**
 * @brief Fill @p out with @p len random bytes for QuicServerConfig::rng.
 *
 * The hardware TRNG on device, a deterministic PRNG on host. Keys the ephemeral X25519 exchange,
 * the ServerHello random, and the connection ids.
 */
void protocore_h3_server_rng(uint8_t *out, size_t len);

#endif // PROTOCORE_ENABLE_HTTP3

PROTOCORE_END_DECLS

#endif // PROTOCORE_H3_SERVER_H
