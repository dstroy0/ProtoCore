// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_quic_tls.h
 * @brief TLS 1.3 server handshake state machine for QUIC (RFC 9001 / RFC 8446).
 *
 * Drives the server side of the TLS 1.3 handshake that QUIC carries in CRYPTO frames. It ties the
 * key schedule (protocore_tls13_kdf), the handshake messages (protocore_tls13_msg), and the transport parameters
 * (quic_tp) together: it runs the transcript hash, consumes the client's ClientHello, produces the
 * server flight (ServerHello at the Initial level; EncryptedExtensions + Certificate +
 * CertificateVerify + Finished at the Handshake level), derives the Handshake and 1-RTT packet keys
 * for both directions, and verifies the client's Finished.
 *
 * The transport engine (protocore_quic_conn) owns CRYPTO stream reassembly and packet protection; this module
 * is transport-free. It consumes an in-order byte run (protocore_quic_tls_recv_crypto returns how many bytes it
 * used) and exposes the outbound flight per encryption level (protocore_quic_tls_flight), so it is fully
 * host-testable by feeding it a captured ClientHello and inspecting the flight and derived keys.
 *
 * Profile: TLS_AES_128_GCM_SHA256, X25519 (or the X25519MLKEM768 PQ/T hybrid when PROTOCORE_ENABLE_PQC_KEX),
 * Ed25519 certificate, no PSK / 0-RTT / client authentication. A client that offers X25519MLKEM768 but
 * sends only a classical key_share is answered with a HelloRetryRequest (RFC 8446 sec 4.1.4) so it
 * retries with the hybrid share instead of being downgraded to X25519. The ephemeral X25519 private key
 * and the ServerHello random are supplied in the config (the caller draws them from its RNG, or fixes
 * them in a test).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_QUIC_TLS_H
#define PROTOCORE_QUIC_TLS_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HTTP3

#include "network_drivers/presentation/http/http3/quic_crypto/quic_crypto.h" // the complete type a public struct below holds by value
#include "network_drivers/presentation/http/http3/quic_tp/quic_tp.h" // the complete type a public struct below holds by value
#include "network_drivers/tls/key_schedule/key_schedule.h" // the complete type a public struct below holds by value

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief QUIC encryption levels (RFC 9001 sec 4). 0-RTT is not supported. */
#define QUIC_ENC_INITIAL 0
#define QUIC_ENC_HANDSHAKE 1
#define QUIC_ENC_APP 2 ///< 1-RTT (application) keys

/** @brief Server handshake configuration (certificate, key, transport params, ephemeral inputs). */
typedef struct QuicTlsConfig
{
    const uint8_t *cert_der; ///< DER X.509 leaf certificate (Ed25519 public key)
    size_t cert_len;
    uint8_t ed25519_seed[32];   ///< Ed25519 private seed matching the certificate
    QuicTransportParams params; ///< the server's transport parameters (caller sets the CIDs)
    uint8_t ephemeral_priv[32]; ///< server X25519 private key
    uint8_t random[32];         ///< ServerHello random
#if PROTOCORE_ENABLE_PQC_KEX
    uint8_t mlkem_m[32]; ///< ML-KEM Encaps randomness (X25519MLKEM768 hybrid); fresh per handshake
#endif
} QuicTlsConfig;

/** @brief Handshake state (a mutually-exclusive internal state, not a wire value). */
typedef enum PROTO_ENUM_PACKED
{
    QTLS_START = 0,     ///< awaiting ClientHello
    QTLS_WAIT_FINISHED, ///< server flight sent; awaiting client Finished
    QTLS_DONE,          ///< client Finished verified
    QTLS_FAILED,        ///< a fatal handshake error (see alert)
} QtlsState;

/** @brief One server handshake's state (fixed storage, no heap). */
typedef struct
{
    QuicTlsConfig cfg;
    uint8_t *transcript;                         ///< running Transcript-Hash over the handshake messages
    Tls13KeySchedule ks;                         ///< TLS 1.3 key schedule, over @ref ks_store
    uint8_t ks_store[PROTOCORE_TLS13_KS_BORROW]; ///< the schedule's terms and its HKDF's bytes
    // The transcript hash and the one-off hashes taken beside it work out of these. Live and die with
    // this connection, so no hash on the handshake path touches a pool.
    uint8_t hash_work[PROTOCORE_TLS13_TRANSCRIPT_BORROW];
    uint8_t hash_work2[PROTOCORE_TLS13_TRANSCRIPT_BORROW];
    uint8_t sign_work[PROTOCORE_SHA512_BORROW];    ///< the CertificateVerify signature's SHA-512
    uint8_t keys_work[PROTOCORE_QUIC_KEYS_BORROW]; ///< the packet-key expansion at each encryption level

    QtlsState state;
    uint8_t alert; ///< TLS alert code (RFC 8446 sec 6) when state == QTLS_FAILED
#if PROTOCORE_ENABLE_PQC_KEX
    proto_bool hrr_sent; ///< a HelloRetryRequest was sent (X25519MLKEM768); the next ClientHello is the retry
#endif
    proto_bool hs_keys_ready; ///< Handshake-level keys derived (after ServerHello)
    proto_bool ap_keys_ready; ///< 1-RTT keys derived (after the server Finished)
    proto_bool complete;      ///< client Finished verified

    QuicPacketKeys hs_client; ///< Handshake: opens client packets
    QuicPacketKeys hs_server; ///< Handshake: seals server packets
    QuicPacketKeys ap_client; ///< 1-RTT: opens client packets
    QuicPacketKeys ap_server; ///< 1-RTT: seals server packets

    uint8_t hs_finished_hash[TLS13_SECRET_MAX]; ///< H(ClientHello..server Finished), to verify client Finished

#if PROTOCORE_ENABLE_PQC_KEX
    uint8_t flight_initial[1400]; ///< outbound Initial CRYPTO (ServerHello; hybrid key_share is ~1.1 KB)
#else
    uint8_t flight_initial[256]; ///< outbound Initial CRYPTO (ServerHello)
#endif
    size_t flight_initial_len;
    uint8_t flight_hs[PROTOCORE_H3_CRYPTO_BUF]; ///< outbound Handshake CRYPTO (EE..Finished)
    size_t flight_hs_len;

    QuicTransportParams peer; ///< the client's parsed transport parameters
    proto_bool have_peer;
} QuicTls;

/** @brief What server_init takes: qt, cfg. */
typedef struct
{
    QuicTls *qt;
    const QuicTlsConfig *cfg;
} QuicTlsServerServerInitArgs;

/** @brief What recv_crypto takes: qt, level, data, len. */
typedef struct
{
    QuicTls *qt;
    int level;
    const uint8_t *data;
    size_t len;
} QuicTlsServerRecvCryptoArgs;

/** @brief What flight takes: qt, level, len. */
typedef struct
{
    const QuicTls *qt;
    int level;
    size_t *len;
} QuicTlsServerFlightArgs;

/** @brief What keys takes: qt, level, is_server. */
typedef struct
{
    QuicTls *qt;
    int level;
    proto_bool is_server;
} QuicTlsServerKeysArgs;

/** @brief What peer_params takes: qt. */
typedef struct
{
    const QuicTls *qt;
} QuicTlsServerPeerParamsArgs;

/**
 * @brief TLS 1.3 server handshake state machine for QUIC (RFC 9001 / RFC 8446).
 *
 * A caller sets the members a call takes, invokes it through ::QuicTlsServer with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   QuicTlsServer.server_init_args.qt = ...;
 *   QuicTlsServer.server_init_args.cfg = ...;
 *   QuicTlsServer.server_init(work);
 *
 * @var QuicTlsServerNs::server_init_args  what server_init takes: qt, cfg
 * @var QuicTlsServerNs::recv_crypto_args  what recv_crypto takes: qt, level, data, len
 * @var QuicTlsServerNs::flight_args  what flight takes: qt, level, len
 * @var QuicTlsServerNs::keys_args  what keys takes: qt, level, is_server
 * @var QuicTlsServerNs::peer_params_args  what peer_params takes: qt
 * @var QuicTlsServerNs::ok  a call's true/false outcome
 * @var QuicTlsServerNs::n  the count a call reports
 * @var QuicTlsServerNs::bytes  a pointer to the flight bytes and its length via len (0 if none). ...
 * @var QuicTlsServerNs::pkt_keys  what a call reports
 * @var QuicTlsServerNs::peer  what a call reports
 * @var QuicTlsServerNs::server_init  initialize a server handshake with cfg (copied). Resets the ...
 * @var QuicTlsServerNs::recv_crypto  feed in-order CRYPTO stream bytes for encryption level level. ...
 * @var QuicTlsServerNs::flight  the pending outbound CRYPTO flight for level (QUIC_ENC_INITIAL / ...
 * @var QuicTlsServerNs::keys  the packet-protection keys for level (QUIC_ENC_HANDSHAKE / ...
 * @var QuicTlsServerNs::peer_params  the client's parsed transport parameters (valid once the ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    QuicTlsServerServerInitArgs server_init_args;
    QuicTlsServerRecvCryptoArgs recv_crypto_args;
    QuicTlsServerFlightArgs flight_args;
    QuicTlsServerKeysArgs keys_args;
    QuicTlsServerPeerParamsArgs peer_params_args;
    proto_bool ok;
    size_t n;
    const uint8_t *bytes;
    QuicPacketKeys *pkt_keys;
    const QuicTransportParams *peer;
} QuicTlsServerVars;

/** @brief The operands and the outcome. */
extern QuicTlsServerVars QuicTlsServerV;

/** @brief The entries. */
typedef struct
{
    void (*const server_init)(uint8_t *restrict work);
    void (*const recv_crypto)(uint8_t *restrict work);
    void (*const flight)(uint8_t *restrict work);
    void (*const keys)(uint8_t *restrict work);
    void (*const peer_params)(uint8_t *restrict work);
} QuicTlsServerNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in QuicTlsServerV or a region of the borrow at a fixed offset.
void protocore_quic_tls_server_server_init(uint8_t *restrict work);
void protocore_quic_tls_server_recv_crypto(uint8_t *restrict work);
void protocore_quic_tls_server_flight(uint8_t *restrict work);
void protocore_quic_tls_server_keys(uint8_t *restrict work);
void protocore_quic_tls_server_peer_params(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `QuicTlsServer.server_init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const QuicTlsServerNs QuicTlsServer __attribute__((unused)) = {
    .server_init = protocore_quic_tls_server_server_init,
    .recv_crypto = protocore_quic_tls_server_recv_crypto,
    .flight = protocore_quic_tls_server_flight,
    .keys = protocore_quic_tls_server_keys,
    .peer_params = protocore_quic_tls_server_peer_params,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3

#endif // PROTOCORE_QUIC_TLS_H
