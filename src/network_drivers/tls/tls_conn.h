// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tls_conn.h
 * @brief TLS 1.3 handshake driver over the stream record layer (RFC 8446 sec 4).
 *
 * The TCP counterpart to pc_dtls_conn: it drives one TLS 1.3 handshake to completion over
 * pc_tls_record, wiring the shared message builders (pc_tls13_msg) and key schedule (pc_tls13_kdf)
 * to a byte stream. TCP delivers in order and exactly once, so there is no flight buffer, no
 * retransmission timer and no reassembler - what remains is the transcript hash, the schedule, and
 * a phase.
 *
 * Both ends live here. A server consumes a ClientHello and answers the one-round-trip full
 * handshake; a client sends the ClientHello and consumes that answer:
 *
 *   ClientHello ->                                                     (TLSPlaintext)
 *              <- ServerHello                                          (TLSPlaintext)
 *              <- EncryptedExtensions, Certificate, CertificateVerify, Finished   (TLSCiphertext)
 *   Finished ->                                                        (TLSCiphertext)
 *   application data, protected with the application traffic keys
 *
 * Profile: the single spec-valid suite the whole hand-rolled TLS 1.3 stack uses -
 * TLS_AES_128_GCM_SHA256, X25519 key exchange, an Ed25519 credential. Per
 * core_setup/board_profiles/pc_platform.h the portable arm authenticates by RFC 7250 raw public
 * key: there is no X.509 chain building, no name matching, and no RSA or ECDSA. A peer that offers
 * none of the profile is a handshake failure.
 *
 * Nothing here blocks and nothing here owns a socket. The caller feeds received bytes and sends
 * whatever @ref TlsConnNs::process writes back, exactly as pc_dtls_conn is fed datagrams.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TLS_CONN_H
#define PROTOCORE_TLS_CONN_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_TLS_SOFTWARE

#include "crypto/hash/sha256.h"
#include "network_drivers/presentation/http/http3/tls13_msg.h"
#include "network_drivers/tls/tls13_kdf.h"
#include "network_drivers/tls/tls_record.h"

/** @brief Handshake progress. A client and a server walk the same phases from opposite ends. */
typedef enum PROTO_ENUM_PACKED
{
    TLS_CONN_START,         ///< server: awaiting ClientHello; client: nothing sent yet
    TLS_CONN_WAIT_SH,       ///< client only: ClientHello sent, awaiting ServerHello
    TLS_CONN_WAIT_FLIGHT,   ///< client only: awaiting the encrypted server flight
    TLS_CONN_WAIT_FINISHED, ///< server only: flight sent, awaiting the client Finished
    TLS_CONN_DONE,          ///< handshake complete; application keys installed
    TLS_CONN_FAILED         ///< fatal (see @ref TlsConnNs::alert)
} TlsConnState;

/** @brief Which end of the handshake this connection drives. */
typedef enum PROTO_ENUM_PACKED
{
    TLS_ROLE_SERVER = 0,
    TLS_ROLE_CLIENT
} TlsRole;

/**
 * @brief The long-lived credential plus this handshake's fresh randomness.
 *
 * @c ed25519_seed is this end's signing key and @c ed25519_pub the raw public key it presents
 * (RFC 7250); a client that only verifies leaves both null. @c peer_pub is the raw public key the
 * peer must present - this is the whole of peer authentication on the portable arm, so a client
 * with no @c peer_pub is encrypt-only and unauthenticated. @c ephemeral_priv and @c random must be
 * freshly generated per connection from a CSPRNG.
 */
typedef struct
{
    const uint8_t *ed25519_seed;   ///< 32-byte Ed25519 signing seed, or NULL when this end does not sign
    const uint8_t *ed25519_pub;    ///< 32-byte raw public key this end presents (matches @c ed25519_seed)
    const uint8_t *peer_pub;       ///< 32-byte raw public key the peer must present, or NULL to skip verification
    const uint8_t *ephemeral_priv; ///< 32-byte X25519 ephemeral private key (fresh per handshake)
    const uint8_t *random;         ///< 32-byte Hello random (fresh per handshake)
    const char *hostname;          ///< client: the SNI to offer, or NULL
} TlsConnConfig;

/**
 * @brief One TLS 1.3 handshake: the session state, and the three regions of one secure-pool borrow.
 *
 * The borrow is taken once by @ref TlsConnNs::init from the pool's persistent end and split by
 * offset - TX at 0, RX at PC_TLS_CONN_MSG_CAP, TERMS after it. No storage is declared here: a
 * message is built in TX and handed to the record layer, a record is opened into RX, and the four
 * 32-byte handshake terms sit in TERMS. No heap.
 */
typedef struct
{
    const TlsConnConfig *cfg; ///< the caller's, borrowed for the life of the connection
    TlsConnState state;
    TlsRole role;
    uint8_t alert; ///< RFC 8446 sec 6 alert code when @c state is FAILED (0 otherwise)

    pc_sha256_ctx transcript; ///< running Transcript-Hash over the handshake messages
    Tls13KeySchedule ks;      ///< TLS 1.3 key schedule
    TlsRecordKeys hs_tx;      ///< handshake traffic keys, this end writing
    TlsRecordKeys hs_rx;      ///< handshake traffic keys, this end reading
    TlsRecordKeys ap_tx;      ///< application traffic keys, this end writing
    TlsRecordKeys ap_rx;      ///< application traffic keys, this end reading
    proto_bool hs_keys_ready; ///< the handshake traffic keys are installed
    proto_bool ap_keys_ready; ///< the application traffic keys are installed

    uint8_t *tx;             ///< PC_TLS_CONN_MSG_CAP: a message built to send, or a received record opened into it
    uint8_t *rx;             ///< PC_TLS_CONN_REC_CAP: the record the worker filled
    uint8_t *terms;          ///< PC_TLS_CONN_TERMS_CAP: the five 32-byte terms, at TLS_TERM_* offsets
    uint8_t *hash_work;      ///< PC_SHA256_BORROW: the bytes @ref transcript works out of
    uint8_t *sign_work;      ///< PC_SHA512_BORROW: the bytes the CertificateVerify signature works out of
    uint8_t *ks_work;        ///< PC_TLS13_KS_BORROW: the bytes the key schedule works out of
    Tls13ClientHello *hello; ///< the peer's parsed ClientHello
} TlsConn;

/**
 * @brief One TLS 1.3 handshake over a stream. TlsConn is the caller's struct; this drives it.
 *
 * @var TlsConnNs::init         bind a connection to its role and configuration. The borrow is
 *                              taken on first use and kept, so a connection initialised again
 *                              reuses the bytes it already holds
 * @var TlsConnNs::start        client only: write the ClientHello; bytes written, or 0
 * @var TlsConnNs::process      the worker filled @ref TlsConn::rx with one record of @p rx_len bytes;
 *                              consume it, writing whatever it owes into @p out
 * @var TlsConnNs::established  whether the handshake has completed
 * @var TlsConnNs::alert        the alert that ended the connection, or 0
 * @var TlsConnNs::seal_app     seal application data into one record; bytes written, or 0
 * @var TlsConnNs::open_app     open one received application record; false on an AEAD failure
 */
typedef struct
{
    proto_bool (*init)(TlsConn *c, TlsRole role, const TlsConnConfig *cfg);
    size_t (*start)(TlsConn *c, uint8_t *out, size_t out_cap);
    int (*process)(TlsConn *c, size_t rx_len, uint8_t *out, size_t out_cap);
    proto_bool (*established)(const TlsConn *c);
    uint8_t (*alert)(const TlsConn *c);
    size_t (*seal_app)(TlsConn *c, const uint8_t *data, size_t len, uint8_t *out, size_t out_cap);
    proto_bool (*open_app)(TlsConn *c, const uint8_t *rec, size_t rec_len, uint8_t *out, size_t out_cap,
                           size_t *out_len);
} TlsConnNs;

/** @brief The one symbol this module exports. */
extern const TlsConnNs TlsConnection;

#endif // PC_TLS_SOFTWARE

PROTO_END_DECLS

#endif // PROTOCORE_TLS_CONN_H
