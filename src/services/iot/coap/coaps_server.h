// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file coaps_server.h
 * @brief CoAP-over-DTLS server front-end - binds a UDP port to a pool of DtlsConn + the CoAPs bridge.
 *
 * The socket / per-peer glue on top of pc_coaps_process(): it owns a fixed pool of @ref DtlsConn
 * handshake engines, binds the CoAPs UDP port (5684, coaps://) through the transport layer (pc_udp),
 * routes each inbound datagram to the connection for its peer address (a new peer opens a pool slot),
 * drives the DTLS 1.3 handshake and its retransmission timer, and hands established application records
 * to pc_coap_server_process() through pc_coaps_process(). CoAP resources are registered with the existing
 * pc_coap_server_add_resource() API; this module only carries them over DTLS, so a plaintext CoAP server
 * (pc_coap_server_begin on :5683) and this secured one can run side by side over the same resources.
 *
 * Threading (ESP32): pc_udp delivers datagrams on the lwIP thread, but the handshake engines must run
 * on the server loop, so the UDP handler only copies each datagram into a lock-free ingest ring;
 * pc_coaps_server_poll() (called from the loop) drains the ring, runs pc_coaps_process(), fires the PTO
 * retransmission timer, and reaps idle or failed connections. The engines therefore only ever run in
 * one context. On host builds there is no UDP; datagrams are injected with pc_coaps_server_ingest() and
 * replies captured through an output sink, so the whole server is host-testable by shuttling byte
 * buffers to an in-test DTLS client, exactly like pc_dtls_conn and pc_coaps_process themselves.
 *
 * Constrained-friendly: unlike the HTTP/3 pool this is not PSRAM-gated - a small DtlsConn pool fits
 * internal DRAM, which is the whole point of CoAP. Raise PC_COAPS_MAX_CONNS for more simultaneous
 * peers (each slot is one DtlsConn handshake engine plus its per-connection key material).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_COAPS_SERVER_H
#define PROTOCORE_COAPS_SERVER_H

#include "protocore_config.h"

#if PC_ENABLE_DTLS && PC_ENABLE_COAP

#ifndef PC_COAPS_MAX_CONNS
#define PC_COAPS_MAX_CONNS 2 ///< simultaneous CoAPs (DTLS) connections; each slot is one DtlsConn engine
#endif
#ifndef PC_COAPS_INGEST_RING
#define PC_COAPS_INGEST_RING 6 ///< datagrams buffered from the lwIP thread until pc_coaps_server_poll() drains them
#endif
#ifndef PC_COAPS_PORT
#define PC_COAPS_PORT 5684 ///< default UDP port the CoAPs server binds (coaps://, RFC 7252 §12.8)
#endif
#ifndef PC_COAPS_IDLE_MS
#define PC_COAPS_IDLE_MS 60000 ///< reclaim a connection with no inbound datagram for this long (§idle-reaping)
#endif

/**
 * @brief The server's long-lived identity plus a randomness source for the DTLS 1.3 handshakes.
 *
 * @c cert_der / @c ed25519_seed are the server's certificate and matching signing key. @c cookie_key
 * is the server-wide secret that keys the HelloRetryRequest cookie MAC (RFC 9147 §5.1). @c rng must be
 * a CSPRNG; the server calls it per handshake for the X25519 ephemeral private key and the ServerHello
 * random. The certificate bytes are referenced by pointer and must outlive the server; the seeds are
 * copied in.
 */
typedef struct
{
    const uint8_t *cert_der; ///< Ed25519 leaf certificate, DER (referenced by pointer, must outlive the server)
    size_t cert_len;
    uint8_t ed25519_seed[32];              ///< 32-byte Ed25519 signing seed (matches @c cert_der)
    uint8_t cookie_key[32];                ///< 32-byte HelloRetryRequest cookie secret
    void (*rng)(uint8_t *out, size_t len); ///< CSPRNG: per-handshake X25519 ephemeral + ServerHello random
} CoapsServerConfig;

/**
 * @brief Start the CoAPs server: install @p cfg, bind @p port over UDP, and route datagrams into the
 * DtlsConn pool. Register CoAP resources first with pc_coap_server_add_resource().
 *
 * @param port UDP port to bind, or 0 for @ref PC_COAPS_PORT (5684).
 * @return false if @p cfg is invalid, or (Arduino) the UDP bind fails; on host builds it always
 *         returns true and is driven through pc_coaps_server_ingest() / the output sink.
 */
proto_bool pc_coaps_server_begin(uint16_t port, const CoapsServerConfig *cfg);

/**
 * @brief Drive the server once: drain queued datagrams into their connections (running the handshake,
 * or decrypting a CoAP request and answering it), fire the DTLS retransmission timer for any
 * outstanding flight (RFC 9147 §5.8), and reap closed or idle (@ref PC_COAPS_IDLE_MS) connections.
 * Call every loop iteration. The monotonic clock is @ref pc_millis (no @c now_ms argument).
 */
void pc_coaps_server_poll();

/** @brief Number of pool slots currently in use (open connections). For diagnostics / tests. */
uint8_t pc_coaps_server_active_conns();

/** @brief Stop the server: close the UDP binding and release every pool slot. */
void pc_coaps_server_stop();

// ---------------------------------------------------------------------------
// Host / test seam (no UDP on host builds)
// ---------------------------------------------------------------------------
#if !PROTOCORE_HOT
/** @brief Sink invoked for every outbound datagram (host builds route sends here instead of UDP). */
using CoapsServerOutFn = void (*)(void *ctx, const uint8_t *datagram, size_t len, const char *ip, uint16_t port);

/** @brief Register the outbound-datagram sink used on host builds. */
void pc_coaps_server_set_out_sink_cb(CoapsServerOutFn fn, void *ctx);

/**
 * @brief Inject a received datagram from @p ip:@p port (the host-build stand-in for the UDP handler).
 * pc_coaps_server_poll() then processes it exactly as a real datagram. @return false if the ring is full.
 */
proto_bool pc_coaps_server_ingest(const uint8_t *datagram, size_t len, const char *ip, uint16_t port);
#endif

#endif // PC_ENABLE_DTLS && PC_ENABLE_COAP
#endif // PROTOCORE_COAPS_SERVER_H
