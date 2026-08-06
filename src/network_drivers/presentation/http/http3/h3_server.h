// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file h3_server.h
 * @brief Bridge between the HTTP/3 server (pc_quic_server) and the server's request pipeline.
 *
 * pc_quic_server hands out a completed request as semantic fields. This module maps those fields
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

PROTO_BEGIN_DECLS

#if PC_ENABLE_HTTP3

#include "network_drivers/presentation/http/http3/quic_server.h" // QuicServerRequestFn: the seam this fills

/**
 * @brief The request seam pc_quic_server_begin() is handed.
 *
 * Maps @p method, @p path, @p authority and @p body onto the reserved dispatch slot and runs the
 * route table. The slot is released on return, so a handler that sends nothing leaves the stream
 * open. @p app is unused: the route table and the slot pools are single global owners.
 */
void pc_h3_server_request(void *app, uint32_t conn_id, uint64_t stream_id, const char *method, const char *path,
                          const char *authority, const uint8_t *body, size_t body_len);

/**
 * @brief Fill @p out with @p len random bytes for QuicServerConfig::rng.
 *
 * The hardware TRNG on device, a deterministic PRNG on host. Keys the ephemeral X25519 exchange,
 * the ServerHello random, and the connection ids.
 */
void pc_h3_server_rng(uint8_t *out, size_t len);

#endif // PC_ENABLE_HTTP3

PROTO_END_DECLS

#endif // PROTOCORE_H3_SERVER_H
