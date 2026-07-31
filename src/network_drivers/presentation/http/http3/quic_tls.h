// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_quic_tls.h
 * @brief TLS 1.3 server handshake state machine for QUIC (RFC 9001 / RFC 8446).
 *
 * Drives the server side of the TLS 1.3 handshake that QUIC carries in CRYPTO frames. It ties the
 * key schedule (pc_tls13_kdf), the handshake messages (pc_tls13_msg), and the transport parameters
 * (pc_quic_tp) together: it runs the transcript hash, consumes the client's ClientHello, produces the
 * server flight (ServerHello at the Initial level; EncryptedExtensions + Certificate +
 * CertificateVerify + Finished at the Handshake level), derives the Handshake and 1-RTT packet keys
 * for both directions, and verifies the client's Finished.
 *
 * The transport engine (pc_quic_conn) owns CRYPTO stream reassembly and packet protection; this module
 * is transport-free. It consumes an in-order byte run (pc_quic_tls_recv_crypto returns how many bytes it
 * used) and exposes the outbound flight per encryption level (pc_quic_tls_flight), so it is fully
 * host-testable by feeding it a captured ClientHello and inspecting the flight and derived keys.
 *
 * Profile: TLS_AES_128_GCM_SHA256, X25519 (or the X25519MLKEM768 PQ/T hybrid when PC_ENABLE_PQC_KEX),
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

#include "protocore_config.h"

#if PC_ENABLE_HTTP3

#include "crypto/hash/sha256.h"
#include "crypto/kdf/tls13_kdf.h"
#include "network_drivers/presentation/http/http3/quic_crypto.h"
#include "network_drivers/presentation/http/http3/quic_tp.h"
#include <stddef.h>
#include <stdint.h>

/** @brief QUIC encryption levels (RFC 9001 sec 4). 0-RTT is not supported. */
struct QuicEnc
{
    static constexpr uint8_t QUIC_ENC_INITIAL = 0;
    static constexpr uint8_t QUIC_ENC_HANDSHAKE = 1;
    static constexpr uint8_t QUIC_ENC_APP = 2; ///< 1-RTT (application) keys
};

/** @brief Server handshake configuration (certificate, key, transport params, ephemeral inputs). */
struct QuicTlsConfig
{
    const uint8_t *cert_der; ///< DER X.509 leaf certificate (Ed25519 public key)
    size_t cert_len;
    uint8_t ed25519_seed[32];   ///< Ed25519 private seed matching the certificate
    QuicTransportParams params; ///< the server's transport parameters (caller sets the CIDs)
    uint8_t ephemeral_priv[32]; ///< server X25519 private key
    uint8_t random[32];         ///< ServerHello random
#if PC_ENABLE_PQC_KEX
    uint8_t mlkem_m[32]; ///< ML-KEM Encaps randomness (X25519MLKEM768 hybrid); fresh per handshake
#endif
};

/** @brief Handshake state (a mutually-exclusive internal state, not a wire value). */
enum class QtlsState : uint8_t
{
    QTLS_START = 0,     ///< awaiting ClientHello
    QTLS_WAIT_FINISHED, ///< server flight sent; awaiting client Finished
    QTLS_DONE,          ///< client Finished verified
    QTLS_FAILED,        ///< a fatal handshake error (see alert)
};

/** @brief One server handshake's state (fixed storage, no heap). */
struct QuicTls
{
    QuicTlsConfig cfg;
    pc_sha256_ctx transcript; ///< running Transcript-Hash over the handshake messages
    Tls13KeySchedule ks;

    QtlsState state;
    uint8_t alert; ///< TLS alert code (RFC 8446 sec 6) when state == QtlsState::QTLS_FAILED
#if PC_ENABLE_PQC_KEX
    bool hrr_sent; ///< a HelloRetryRequest was sent (X25519MLKEM768); the next ClientHello is the retry
#endif
    bool hs_keys_ready; ///< Handshake-level keys derived (after ServerHello)
    bool ap_keys_ready; ///< 1-RTT keys derived (after the server Finished)
    bool complete;      ///< client Finished verified

    QuicPacketKeys hs_client; ///< Handshake: opens client packets
    QuicPacketKeys hs_server; ///< Handshake: seals server packets
    QuicPacketKeys ap_client; ///< 1-RTT: opens client packets
    QuicPacketKeys ap_server; ///< 1-RTT: seals server packets

    uint8_t hs_finished_hash[32]; ///< H(ClientHello..server Finished), to verify client Finished

#if PC_ENABLE_PQC_KEX
    uint8_t flight_initial[1400]; ///< outbound Initial CRYPTO (ServerHello; hybrid key_share is ~1.1 KB)
#else
    uint8_t flight_initial[256]; ///< outbound Initial CRYPTO (ServerHello)
#endif
    size_t flight_initial_len;
    uint8_t flight_hs[PC_H3_CRYPTO_BUF]; ///< outbound Handshake CRYPTO (EE..Finished)
    size_t flight_hs_len;

    QuicTransportParams peer; ///< the client's parsed transport parameters
    bool have_peer;
};

/** @brief Initialize a server handshake with @p cfg (copied). Resets the transcript and state. */
void pc_quic_tls_server_init(QuicTls *qt, const QuicTlsConfig *cfg);

/**
 * @brief Feed in-order CRYPTO stream bytes for encryption level @p level.
 *
 * Consumes as many complete handshake messages as @p data holds. At QuicEnc::QUIC_ENC_INITIAL it expects the
 * ClientHello and, on success, builds the whole server flight and derives the Handshake + 1-RTT keys.
 * At QuicEnc::QUIC_ENC_HANDSHAKE it expects the client Finished and verifies it. On a fatal error it sets the
 * state to QtlsState::QTLS_FAILED and an alert. @return the number of leading bytes of @p data consumed (a
 * partial trailing message is left for the next call).
 */
size_t pc_quic_tls_recv_crypto(QuicTls *qt, int level, const uint8_t *data, size_t len);

/**
 * @brief The pending outbound CRYPTO flight for @p level (QuicEnc::QUIC_ENC_INITIAL / QuicEnc::QUIC_ENC_HANDSHAKE).
 * @return a pointer to the flight bytes and its length via @p len (0 if none). The transport engine
 * fragments these into CRYPTO frames and tracks its own send offset / retransmission.
 */
const uint8_t *pc_quic_tls_flight(const QuicTls *qt, int level, size_t *len);

/**
 * @brief The packet-protection keys for @p level (QuicEnc::QUIC_ENC_HANDSHAKE / QuicEnc::QUIC_ENC_APP), @p is_server
 * picking the seal (server) or open (client) direction. @return NULL if those keys are not ready.
 */
QuicPacketKeys *pc_quic_tls_keys(QuicTls *qt, int level, bool is_server);

/** @brief The client's parsed transport parameters (valid once the ClientHello is processed). */
const QuicTransportParams *pc_quic_tls_peer_params(const QuicTls *qt);

#endif // PC_ENABLE_HTTP3
#endif // PROTOCORE_QUIC_TLS_H
