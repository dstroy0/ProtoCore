// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tls.h
 * @brief The TLS connection resource (RFC 8446), and the no-op surface a build without TLS sees.
 *
 * This file owns the connection: ::TlsConn, its configuration and its handshake phase. The layers
 * that act on it are beside it - handshake/ drives the handshake (RFC 8446 sec 4), record/ frames
 * for it (sec 5), key_schedule/ derives its secrets (sec 7.1).
 *
 * The portable TLS 1.3 arm (PROTOCORE_TLS_SOFTWARE) is the implementation; a caller reaches it
 * through ::TlsConnection. The slot-indexed protocore_tls_* calls below are no-ops, so a call site
 * that predates the portable arm still compiles and needs no extra guards.
 */

#ifndef PROTOCORE_TLS_H
#define PROTOCORE_TLS_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// The connection resource (RFC 8446): what a handshake and a record layer act on
// ---------------------------------------------------------------------------
// This file owns the connection; handshake/ drives it and record/ frames for it.
#if PROTOCORE_TLS_SOFTWARE

#include "crypto/hash/sha256.h"
#include "network_drivers/presentation/http/http3/tls13_msg.h"
#include "network_drivers/tls/key_schedule/key_schedule.h"
#include "network_drivers/tls/record/record.h"

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
    // Server: the protocols this listener answers, in descending preference (RFC 7301 sec 3.1). A
    // null list leaves ALPN unanswered; a client that offered it then gets no extension back.
    const char *const *alpn; ///< NUL-terminated protocol names, or NULL
    uint8_t alpn_count;      ///< how many
} TlsConnConfig;

/**
 * @brief One TLS 1.3 handshake: the session state, and the three regions of one secure-pool borrow.
 *
 * The borrow is taken once by @ref TlsConnNs::init from the pool's persistent end and split by
 * offset - TX at 0, RX at PROTOCORE_TLS_CONN_MSG_CAP, TERMS after it. No storage is declared here: a
 * message is built in TX and handed to the record layer, a record is opened into RX, and the four
 * 32-byte handshake terms sit in TERMS. No heap.
 */
typedef struct
{
    const TlsConnConfig *cfg; ///< the caller's, borrowed for the life of the connection
    TlsConnState state;
    TlsRole role;
    uint8_t alert; ///< RFC 8446 sec 6 alert code when @c state is FAILED (0 otherwise)

    protocore_sha256_ctx transcript; ///< running Transcript-Hash over the handshake messages
    Tls13KeySchedule ks;             ///< TLS 1.3 key schedule
    TlsRecordKeys hs_tx;             ///< handshake traffic keys, this end writing
    TlsRecordKeys hs_rx;             ///< handshake traffic keys, this end reading
    TlsRecordKeys ap_tx;             ///< application traffic keys, this end writing
    TlsRecordKeys ap_rx;             ///< application traffic keys, this end reading
    proto_bool hs_keys_ready;        ///< the handshake traffic keys are installed
    proto_bool ap_keys_ready;        ///< the application traffic keys are installed

    uint8_t *tx;        ///< PROTOCORE_TLS_CONN_MSG_CAP: a message built to send, or a received record opened into it
    uint8_t *rx;        ///< PROTOCORE_TLS_CONN_REC_CAP: the record the worker filled
    uint8_t *terms;     ///< PROTOCORE_TLS_CONN_TERMS_CAP: the five 32-byte terms, at TLS_TERM_* offsets
    uint8_t *hash_work; ///< PROTOCORE_SHA256_BORROW: the bytes @ref transcript works out of
    uint8_t *sign_work; ///< PROTOCORE_SHA512_BORROW: the bytes the CertificateVerify signature works out of
    uint8_t *ks_work;   ///< PROTOCORE_TLS13_KS_BORROW: the bytes the key schedule works out of
    Tls13ClientHello *hello; ///< the peer's parsed ClientHello
    const char *alpn;        ///< the protocol selected from TlsConnConfig::alpn, or NULL when none was
} TlsConn;

/** @brief The connection standing on @p slot, or NULL when the index is out of range. */
TlsConn *protocore_tls_conn_at(uint8_t slot);

/**
 * @brief The protocol ALPN selected on @p slot, or NULL when none was negotiated.
 *
 * Points into the listener's ::TlsConnConfig::alpn list, which outlives the connection.
 */
const char *protocore_tls_alpn(uint8_t slot);

#endif // PROTOCORE_TLS_SOFTWARE

/** @brief Where a stepped TLS exchange stands. */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_TLS_READY = 0, ///< the move landed
    PROTOCORE_TLS_BUSY,      ///< the peer's bytes are still in flight; ask again on the next tick
    PROTOCORE_TLS_FAILED,    ///< the session did not stand up, or it is closed / fatal
} protocore_tls_state;

static inline proto_bool protocore_tls_global_init(const uint8_t *cert, size_t cert_len, const uint8_t *key,
                                                   size_t key_len)
{
    (void)cert;
    (void)cert_len;
    (void)key;
    (void)key_len;
    return PROTO_FALSE;
}
static inline proto_bool protocore_tls_ready(void)
{
    return PROTO_FALSE;
}
static inline proto_bool protocore_tls_conn_begin(uint8_t slot)
{
    (void)slot;
    return PROTO_FALSE;
}
static inline int protocore_tls_handshake(uint8_t slot)
{
    (void)slot;
    return -1;
}
static inline proto_bool protocore_tls_established(uint8_t slot)
{
    (void)slot;
    return PROTO_FALSE;
}
static inline int protocore_tls_read(uint8_t slot, uint8_t *buf, size_t len)
{
    (void)slot;
    (void)buf;
    (void)len;
    return -1;
}
static inline int protocore_tls_write(uint8_t slot, const void *data, size_t len)
{
    (void)slot;
    (void)data;
    (void)len;
    return -1;
}
static inline void protocore_tls_conn_end(uint8_t slot)
{
    (void)slot;
}
static inline void protocore_tls_conn_free(uint8_t slot)
{
    (void)slot;
}
static inline size_t protocore_tls_arena_peak(void)
{
    return 0;
}

#if PROTOCORE_ENABLE_MTLS
static inline proto_bool protocore_tls_set_client_ca(const uint8_t *ca, size_t ca_len)
{
    (void)ca;
    (void)ca_len;
    return PROTO_FALSE;
}
static inline int protocore_tls_peer_subject(uint8_t slot, char *out, size_t out_len)
{
    (void)slot;
    (void)out;
    (void)out_len;
    return -1;
}
#endif // PROTOCORE_ENABLE_MTLS

#if PROTOCORE_ENABLE_CLIENT_TLS
typedef int (*protocore_tls_bio_send_fn)(void *ctx, const unsigned char *buf, size_t len);
typedef int (*protocore_tls_bio_recv_fn)(void *ctx, unsigned char *buf, size_t len);
static inline void protocore_tls_client_set_ca(const uint8_t *ca, size_t ca_len)
{
    (void)ca;
    (void)ca_len;
}
static inline void protocore_tls_client_set_pin(const uint8_t sha256[32])
{
    (void)sha256;
}
static inline void protocore_tls_client_clear_verify(void)
{
}
static inline proto_bool protocore_tls_client_session_begin(const char *host, protocore_tls_bio_send_fn send_fn,
                                                            protocore_tls_bio_recv_fn recv_fn)
{
    (void)host;
    (void)send_fn;
    (void)recv_fn;
    return PROTO_FALSE;
}
static inline proto_bool protocore_tls_client_session_active(void)
{
    return PROTO_FALSE;
}
static inline protocore_tls_state protocore_tls_client_session_handshake(void)
{
    return PROTOCORE_TLS_FAILED;
}
static inline int protocore_tls_client_session_read(uint8_t *buf, size_t len)
{
    (void)buf;
    (void)len;
    return -1;
}
static inline int protocore_tls_client_session_write(const uint8_t *data, size_t len)
{
    (void)data;
    (void)len;
    return -1;
}
static inline void protocore_tls_client_session_end(void)
{
}
static inline void protocore_tls_client_session_forget_session(void)
{
}
#endif // PROTOCORE_ENABLE_CLIENT_TLS

PROTOCORE_END_DECLS

#endif // PROTOCORE_TLS_H
