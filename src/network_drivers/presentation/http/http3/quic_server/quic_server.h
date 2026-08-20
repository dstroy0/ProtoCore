// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_quic_server.h
 * @brief HTTP/3 server glue - binds UDP to a pool of QUIC + HTTP/3 connections (RFC 9000/9114).
 *
 * The last piece of the HTTP/3 stack: it owns a fixed pool of QuicConn + H3Conn engines, binds the
 * HTTP/3 UDP port through the transport layer (protocore_udp), routes each inbound datagram to the right
 * connection by its Destination Connection ID (a new client Initial opens a pool slot), drives the
 * handshake + streams, and pulls the outbound datagrams back onto the wire. A completed HTTP/3
 * request is surfaced through a single callback; the application answers with protocore_quic_server_respond().
 *
 * Threading (ESP32): protocore_udp delivers datagrams on the lwIP thread, but requests must be dispatched
 * on the server's worker/main loop, so the UDP handler only copies each datagram into a lock-free
 * ingest ring; protocore_quic_server_poll() (called from the loop) drains the ring, runs the engines, and
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

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HTTP3

PROTOCORE_BEGIN_DECLS

#include "network_drivers/presentation/http/http3/h3_conn/h3_conn.h"
#include "network_drivers/presentation/http/http3/quic_conn/quic_conn.h"

#ifndef PROTOCORE_HTTP3_PORT
#define PROTOCORE_HTTP3_PORT 443 ///< default UDP port the HTTP/3 server binds (QUIC)
#endif
#ifndef PROTOCORE_QUIC_SCID_LEN
#define PROTOCORE_QUIC_SCID_LEN 8 ///< length of the connection ID the server chooses for itself
#endif
#ifndef PROTOCORE_QUIC_IDLE_MS
#define PROTOCORE_QUIC_IDLE_MS 30000 ///< reclaim a connection idle this long (also advertised as max_idle_timeout)
#endif

/**
 * @brief A completed HTTP/3 request handed to the application on the poll thread.
 *
 * Reply synchronously with protocore_quic_server_respond(@p conn_id, @p stream_id, ...) (typically from inside
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
/** @brief What binding the server takes: the port, its keys, and where requests go. */
typedef struct
{
    uint16_t port;                  ///< the UDP port a begin binds
    const QuicServerConfig *cfg;    ///< the certificate, its key, and the randomness source
    QuicServerRequestFn on_request; ///< what a completed request is delivered to, on the poll thread
    void *app;                      ///< the opaque pointer that callback is given back
} QuicBeginArgs;
/** @brief RFC 9000 sec 2.1: the connection and stream a response is written on. */
typedef struct
{
    uint32_t conn_id;   ///< the connection a response routes back on
    uint64_t stream_id; ///< the stream it finishes
} QuicStreamRef;
/** @brief What one response carries. */
typedef struct
{
    int status;               ///< the status that response carries
    const char *content_type; ///< its media type
    const uint8_t *body;      ///< its body bytes
    size_t body_len;          ///< how many
} QuicRespArgs;
/**
 * @brief The HTTP/3 server: a QUIC connection pool over one bound UDP port.
 *
 * A caller sets the members a call takes, invokes it through ::QuicServer, and reads the outcome off
 * the same handle.
 *
 * @var QuicServerNs::begin_args    what binding the server takes
 * @var QuicServerNs::now_ms        the caller's monotonic clock, so this module stays platform-agnostic
 * @var QuicServerNs::stream        the connection and stream a response names
 * @var QuicServerNs::resp          what that response carries
 * @var QuicServerNs::ok            a call's true/false outcome
 * @var QuicServerNs::u8            the pool slots currently in use
 * @var QuicServerNs::begin         install begin_args.cfg, bind its port over UDP, route datagrams into the pool
 * @var QuicServerNs::poll          drive the server once: drain, run the engines, flush; closed or
 *                                  idle (PROTOCORE_QUIC_IDLE_MS) connections are reaped here
 * @var QuicServerNs::respond       send HEADERS + DATA, finishing the stream; call from within the
 *                                  request callback
 * @var QuicServerNs::active_conns  open connections, for diagnostics and tests
 * @var QuicServerNs::stop          close the UDP binding and release every pool slot
 */
typedef struct
{
    uint32_t now_ms;          ///< the caller's monotonic clock, so this module stays platform-agnostic
    QuicBeginArgs begin_args; ///< what binding the server takes
    QuicStreamRef stream;     ///< the connection and stream a response names
    QuicRespArgs resp;        ///< what that response carries
    proto_bool ok;
    uint8_t u8;
} QuicServerVars;

/** @brief The operands and the outcome. */
extern QuicServerVars QuicServerV;

/** @brief The entries. */
typedef struct
{
    void (*const begin)(uint8_t *restrict work);
    void (*const poll)(uint8_t *restrict work);
    void (*const respond)(uint8_t *restrict work);
    void (*const active_conns)(uint8_t *restrict work);
    void (*const stop)(uint8_t *restrict work);
} QuicServerNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in QuicServerV or a region of the borrow at a fixed offset.
void protocore_quic_server_begin(uint8_t *restrict work);
void protocore_quic_server_poll(uint8_t *restrict work);
void protocore_quic_server_respond(uint8_t *restrict work);
void protocore_quic_server_active_conns(uint8_t *restrict work);
void protocore_quic_server_stop(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `QuicServer.begin(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const QuicServerNs QuicServer __attribute__((unused)) = {
    .begin = protocore_quic_server_begin,
    .poll = protocore_quic_server_poll,
    .respond = protocore_quic_server_respond,
    .active_conns = protocore_quic_server_active_conns,
    .stop = protocore_quic_server_stop,
};

/**
 * @brief The PROTOCORE_QUIC_SERVER_BORROW bytes this server runs out of.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the server lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_quic_server_span(void);
/**
 * @brief The response sink the HTTP/3 bridge installs; its shape is the seam's, not this module's.
 */
proto_bool protocore_quic_server_respond(uint32_t conn_id, uint64_t stream_id, int status, const char *content_type,
                                         const uint8_t *body, size_t body_len);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3

#endif // PROTOCORE_QUIC_SERVER_H
