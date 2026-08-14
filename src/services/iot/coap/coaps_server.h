// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file coaps_server.h
 * @brief The CoAP-over-DTLS server: a bound UDP port, a pool of DTLS connections, and the bridge.
 *
 * The socket half of @ref Coaps. It owns a fixed pool of DTLS 1.3 connections, binds the secured
 * CoAP port (RFC 7252 sec 12.7 registers 5684 and the service name "coaps"; sec 6.2 gives it the
 * "coaps" URI scheme), routes each inbound datagram to the connection for its peer, drives the
 * handshake and its retransmission timer (RFC 9147 sec 5.8), and hands established application
 * records to @ref Coaps. Resources are registered once through @ref Coap, so a plaintext server on
 * 5683 and this secured one serve the same table.
 *
 * The receive side and the connections run in different contexts: the transport delivers datagrams
 * as they arrive, so the receive path only copies each into a single-producer ingest ring, and the
 * poll drains that ring, runs the bridge, fires the retransmission timer, and reclaims connections
 * that failed or went quiet. The handshake engines therefore only ever run where the poll runs.
 * Where the build has no network stack there is no receive path: datagrams go in through @c ingest
 * and replies come out through the sink, which is what makes the whole server host-testable by
 * shuttling buffers to an in-test DTLS client.
 *
 * Raise @ref PROTOCORE_COAPS_MAX_CONNS for more simultaneous peers; each slot is one handshake
 * engine plus its per-connection key material.
 *
 * The module exports one symbol, @ref CoapsServer. Everything in coaps_server.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_COAPS_SERVER_H
#define PROTOCORE_COAPS_SERVER_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_DTLS && PROTOCORE_ENABLE_COAP

#ifndef PROTOCORE_COAPS_MAX_CONNS
#define PROTOCORE_COAPS_MAX_CONNS 2 ///< simultaneous connections; each slot is one handshake engine
#endif
#ifndef PROTOCORE_COAPS_INGEST_RING
#define PROTOCORE_COAPS_INGEST_RING 6 ///< datagrams buffered from the receive path until a poll drains them
#endif
#ifndef PROTOCORE_COAPS_PORT
#define PROTOCORE_COAPS_PORT 5684 ///< the port a bind takes by default (RFC 7252 sec 12.7)
#endif
#ifndef PROTOCORE_COAPS_IDLE_MS
#define PROTOCORE_COAPS_IDLE_MS 60000 ///< a connection with no inbound datagram for this long is reclaimed
#endif

/**
 * @brief The server's long-lived identity and the randomness each handshake draws from.
 *
 * @c cert_der and @c ed25519_seed are the certificate and the signing key that matches it.
 * @c cookie_key is the server-wide secret keying the HelloRetryRequest cookie's MAC (RFC 9147
 * sec 5.1). @c rng must be a CSPRNG: it is called per handshake for the X25519 ephemeral private key
 * and the ServerHello random. The certificate octets are referenced by pointer and must outlive the
 * server; the seeds are copied into storage.
 */
typedef struct
{
    const uint8_t *cert_der;               ///< the leaf certificate, DER, referenced and not copied
    size_t cert_len;                       ///< its length
    uint8_t ed25519_seed[32];              ///< the signing seed that matches @c cert_der
    uint8_t cookie_key[32];                ///< the HelloRetryRequest cookie secret (RFC 9147 sec 5.1)
    void (*rng)(uint8_t *out, size_t len); ///< the CSPRNG each handshake draws its ephemeral and random from
} CoapsServerIdentityArgs;

/** @brief The UDP endpoint the server receives on (RFC 7252 sec 12.7: port 5684, service "coaps"). */
typedef struct
{
    uint16_t port; ///< the port a begin binds, or 0 for @ref PROTOCORE_COAPS_PORT
} CoapsServerBindArgs;

#if !PROTOCORE_HAS_NET_STACK
/** @brief Where an outbound datagram goes where the build has no network stack. */
typedef void (*CoapsServerOutFn)(void *ctx, const uint8_t *datagram, size_t len, const char *ip, uint16_t port);

/** @brief The outbound sink and the context it is handed back. */
typedef struct
{
    CoapsServerOutFn fn; ///< what every outbound datagram is given to
    void *ctx;           ///< the opaque context that sink is given back
} CoapsServerSinkArgs;

/** @brief One datagram injected in place of a receive, and the peer it is attributed to. */
typedef struct
{
    const uint8_t *data; ///< the datagram's octets
    size_t len;          ///< how many
    const char *ip;      ///< the peer's address, as text
    uint16_t port;       ///< its port
} CoapsServerIngestArgs;
#endif

/** @brief The server's own state and the calls that reach it, described only in coaps_server.c. */
struct CoapsServerInternal;

/**
 * @brief The CoAP-over-DTLS server.
 *
 * A caller sets the members a call takes, invokes it through ::CoapsServer, and reads the outcome off
 * the same handle.
 *
 * No slot member: one server owns the pool, and a call that names an endpoint names it inside its own
 * argument group rather than at the top.
 *
 * @var CoapsServerNs::identity  the certificate, keys and CSPRNG a begin installs
 * @var CoapsServerNs::bind      the port a begin binds
 * @var CoapsServerNs::sink      where an outbound datagram goes with no network stack
 * @var CoapsServerNs::dgram     one datagram injected in place of a receive
 * @var CoapsServerNs::ok        a call's true/false outcome
 * @var CoapsServerNs::u8        the pool slots in use
 * @var CoapsServerNs::begin         install @c identity, bind @c bind.port, and route its datagrams into the pool
 * @var CoapsServerNs::poll          drain the ingest ring through the bridge, fire the retransmission timer
 *                                   (RFC 9147 sec 5.8), and reclaim failed or quiet connections
 * @var CoapsServerNs::active_conns  report the pool slots in use
 * @var CoapsServerNs::stop          stop polling, release every slot, and empty the ingest ring
 * @var CoapsServerNs::set_out_sink  install @c sink
 * @var CoapsServerNs::ingest        queue @c dgram as though it had been received
 * @var CoapsServerNs::internal      the server's state and the calls that reach it
 */
typedef struct
{
    CoapsServerIdentityArgs identity; ///< what the handshakes are run with
    CoapsServerBindArgs bind;         ///< what binding the receive port takes
#if !PROTOCORE_HAS_NET_STACK
    CoapsServerSinkArgs sink;    ///< where the replies go
    CoapsServerIngestArgs dgram; ///< what an injected datagram carries
#endif

    proto_bool ok;
    uint8_t u8;

    void (*begin)(struct CoapsServerInternal *ctx);
    void (*poll)(struct CoapsServerInternal *ctx);
    void (*active_conns)(struct CoapsServerInternal *ctx);
    void (*stop)(struct CoapsServerInternal *ctx);
#if !PROTOCORE_HAS_NET_STACK
    void (*set_out_sink)(struct CoapsServerInternal *ctx);
    void (*ingest)(struct CoapsServerInternal *ctx);
#endif

    struct CoapsServerInternal *internal;
} CoapsServerNs;

/** @brief The one symbol this module exports. */
extern CoapsServerNs CoapsServer;

#endif // PROTOCORE_ENABLE_DTLS && PROTOCORE_ENABLE_COAP

PROTOCORE_END_DECLS

#endif // PROTOCORE_COAPS_SERVER_H
