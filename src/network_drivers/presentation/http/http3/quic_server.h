// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_quic_server.h
 * @brief HTTP/3 server glue - binds UDP to a pool of QUIC + HTTP/3 connections (RFC 9000/9114).
 *
 * The last piece of the HTTP/3 stack: it owns a fixed pool of QuicConn + H3Conn engines, binds the
 * HTTP/3 UDP port through the transport layer (pc_udp), routes each inbound datagram to the right
 * connection by its Destination Connection ID (a new client Initial opens a pool slot), drives the
 * handshake + streams, and pulls the outbound datagrams back onto the wire. A completed HTTP/3
 * request is surfaced through a single callback; the application answers with pc_quic_server_respond().
 *
 * Threading (ESP32): pc_udp delivers datagrams on the lwIP thread, but requests must be dispatched
 * on the server's worker/main loop, so the UDP handler only copies each datagram into a lock-free
 * ingest ring; pc_quic_server_poll() (called from the loop) drains the ring, runs the engines, and
 * sends replies. The engines therefore only ever run in one context.
 *
 * The pool (QuicConn + H3Conn per slot + the ingest ring) is large, so like HTTP/2 it is a
 * PSRAM-class feature. No heap; fixed storage. This module has no PC dependency - the
 * request/response seam is a plain callback - so the route-dispatch bridge lives in the server.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_QUIC_SERVER_H
#define PROTOCORE_QUIC_SERVER_H

#include "protocore_config.h"

#if PC_ENABLE_HTTP3

#include "network_drivers/presentation/http/http3/h3_conn.h"
#include "network_drivers/presentation/http/http3/quic_conn.h"

#ifndef PC_QUIC_MAX_CONNS
#define PC_QUIC_MAX_CONNS 2 ///< simultaneous HTTP/3 connections (each is a QuicConn + H3Conn, PSRAM-class)
#endif
#ifndef PC_QUIC_INGEST_RING
#define PC_QUIC_INGEST_RING 8 ///< datagrams buffered from the lwIP thread until pc_quic_server_poll() drains them
#endif
#ifndef PC_HTTP3_PORT
#define PC_HTTP3_PORT 443 ///< default UDP port the HTTP/3 server binds (QUIC)
#endif
#ifndef PC_QUIC_SCID_LEN
#define PC_QUIC_SCID_LEN 8 ///< length of the connection ID the server chooses for itself
#endif
#ifndef PC_QUIC_IDLE_MS
#define PC_QUIC_IDLE_MS 30000 ///< reclaim a connection idle this long (also advertised as max_idle_timeout)
#endif

/**
 * @brief A completed HTTP/3 request handed to the application on the poll thread.
 *
 * Reply synchronously with pc_quic_server_respond(@p conn_id, @p stream_id, ...) (typically from inside
 * this call). @p body / @p body_len are valid only during the call.
 */
typedef void (*QuicServerRequestFn)(void *app, uint32_t conn_id, uint64_t stream_id, const char *method,
                                    const char *path, const char *authority, const uint8_t *body, size_t body_len);

/** @brief Server configuration: the Ed25519 leaf certificate + its key, and a randomness source. */
typedef struct
{
    const uint8_t *cert_der; ///< DER X.509 leaf certificate (Ed25519 public key)
    size_t cert_len;
    uint8_t ed25519_seed[32];              ///< Ed25519 private seed matching the certificate
    void (*rng)(uint8_t *out, size_t len); ///< fills @p out with @p len random bytes (ephemeral keys, SCIDs)
} QuicServerConfig;

/**
 * @brief Start the HTTP/3 server: install @p cfg, bind @p port over UDP, and route datagrams into the
 * connection pool. @p on_request is invoked (on the poll thread) for each completed request.
 * @return false on a null config or RNG, or when the UDP bind fails.
 */
proto_bool pc_quic_server_begin(uint16_t port, const QuicServerConfig *cfg, QuicServerRequestFn on_request, void *app);

/**
 * @brief Drive the server once: drain queued inbound datagrams into their connections, run the
 * handshake + HTTP/3 engines (which may fire @p on_request), and flush outbound datagrams. Call every
 * loop iteration. @p now_ms is the caller's monotonic millisecond clock (the module stays
 * platform-agnostic); closed or idle (PC_QUIC_IDLE_MS) connections are reaped here.
 */
void pc_quic_server_poll(uint32_t now_ms);

/**
 * @brief Send an HTTP/3 response (HEADERS + DATA, finishing the stream) for @p stream_id on the
 * connection @p conn_id. Call from within the request callback. @return false on a stale conn_id /
 * stream or a serialization overflow.
 */
proto_bool pc_quic_server_respond(uint32_t conn_id, uint64_t stream_id, int status, const char *content_type,
                                  const uint8_t *body, size_t body_len);

/** @brief Number of pool slots currently in use (open connections). For diagnostics / tests. */
uint8_t pc_quic_server_active_conns(void);

/** @brief Stop the server: close the UDP binding and release every pool slot. */
void pc_quic_server_stop(void);

#endif // PC_ENABLE_HTTP3
#endif // PROTOCORE_QUIC_SERVER_H
