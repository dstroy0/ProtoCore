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
#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_TLS_SOFTWARE

PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// The connection resource (RFC 8446): what a handshake and a record layer act on
// ---------------------------------------------------------------------------
// This file owns the connection; handshake/ drives it and record/ frames for it.

#include "network_drivers/presentation/http/http3/tls13_msg/tls13_msg.h"
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
    const char *hostname;          ///< client: the SNI to offer, and the name a certificate must speak for
    // Client: the trust anchor a presented X.509 chain is validated to (RFC 5280 sec 6.1). When it
    // is set the peer is authenticated by certificate - the chain to this anchor, and @c hostname
    // matched by RFC 6125 - and @c peer_pub is not consulted. When it is null the peer is
    // authenticated by raw public key against @c peer_pub, which is what this arm does by default.
    const uint8_t *ca_der; ///< one DER trust anchor, or NULL
    size_t ca_len;         ///< its length
    uint64_t now;          ///< seconds since the POSIX epoch, for the validity window (sec 6.1.3 (a)(2))
    // Server: the DER certificate to present instead of a raw public key. @c ed25519_seed still
    // signs the CertificateVerify, so it must be the key this certificate carries.
    const uint8_t *cert_der; ///< one DER certificate, or NULL to present the raw public key
    size_t cert_len;         ///< its length
    // Server: the protocols this listener answers, in descending preference (RFC 7301 sec 3.1). A
    // null list leaves ALPN unanswered; a client that offered it then gets no extension back.
    const char *const *alpn; ///< NUL-terminated protocol names, or NULL
    uint8_t alpn_count;      ///< how many
    // The suite this end runs: a client offers it, a server answers with it, and it fixes the AEAD,
    // the key schedule's hash and the Transcript-Hash together (RFC 8446 sec 7.1). Zero-initialising
    // the config leaves TLS_CIPHER_AES_128_GCM_SHA256, the sec 9.1 mandatory-to-implement suite.
    TlsCipher cipher;
} TlsConnConfig;

/**
 * @brief One TLS 1.3 handshake: the session state, and the three regions of one secure-pool borrow.
 *
 * The borrow is taken once by @ref TlsConnNs::init from the pool's persistent end and split by
 * offset - TX at 0, RX at PROTOCORE_TLS_CONN_MSG_CAP, TERMS after it. No storage is declared here: a
 * message is built in TX and handed to the record layer, a record is opened into RX, and the four
 * TLS13_SECRET_MAX-strided handshake terms sit in TERMS. No heap.
 */
typedef struct
{
    const TlsConnConfig *cfg; ///< the caller's, borrowed for the life of the connection
    TlsConnState state;
    TlsRole role;
    uint8_t alert; ///< RFC 8446 sec 6 alert code when @c state is FAILED (0 otherwise)

    uint8_t *transcript;      ///< running Transcript-Hash over the handshake messages
    Tls13KeySchedule ks;      ///< TLS 1.3 key schedule
    TlsRecordKeys hs_tx;      ///< handshake traffic keys, this end writing
    TlsRecordKeys hs_rx;      ///< handshake traffic keys, this end reading
    TlsRecordKeys ap_tx;      ///< application traffic keys, this end writing
    TlsRecordKeys ap_rx;      ///< application traffic keys, this end reading
    proto_bool hrr_sent;      ///< a HelloRetryRequest has been answered on this connection (sec 4.1.4)
    proto_bool hs_keys_ready; ///< the handshake traffic keys are installed
    proto_bool ap_keys_ready; ///< the application traffic keys are installed

    uint8_t *tx; ///< PROTOCORE_TLS_CONN_MSG_CAP: a message built to send, or a received record opened into it
    uint8_t *rx; ///< PROTOCORE_TLS_CONN_REC_CAP: the record the worker filled
    uint8_t
        *terms; ///< PROTOCORE_TLS_CONN_TERMS_CAP: the five terms, one TLS13_SECRET_MAX slot each, at TLS_TERM_* offsets
    uint8_t *hash_work;      ///< PROTOCORE_TLS13_TRANSCRIPT_BORROW: the bytes @ref transcript works out of
    uint8_t *sign_work;      ///< PROTOCORE_SHA512_BORROW: the bytes the CertificateVerify signature works out of
    uint8_t *ks_work;        ///< PROTOCORE_TLS13_KS_BORROW: the bytes the key schedule works out of
    Tls13ClientHello *hello; ///< the peer's parsed ClientHello
    const char *alpn;        ///< the protocol selected from TlsConnConfig::alpn, or NULL when none was
} TlsConn;

// The handle that drives one connection (network_drivers/tls/handshake). Declared here because
// this file owns ::TlsConn: the driver acts on the resource, so the resource publishes the seam
// and the driver defines it. A separate header would have to include this one, and this one
// would have to reach back for the handle.
/** @brief RFC 8446 sec 4: which end this connection drives, and what it presents. */
typedef struct
{
    TlsRole role;             ///< which end of the handshake this connection drives
    const TlsConnConfig *cfg; ///< the credential and this handshake's randomness
} TlsInitArgs;

/** @brief The bytes one call moves: a received record in, application data out. */
typedef struct
{
    size_t rx_len;       ///< how much of TlsConn::rx the worker filled
    const uint8_t *data; ///< application bytes a seal takes
    size_t len;          ///< how many
    const uint8_t *rec;  ///< the received application record an open takes
    size_t rec_len;      ///< how many bytes of it there are
} TlsConnIoArgs;

/** @brief Where a call writes, and what it wrote. */
typedef struct
{
    uint8_t *out;    ///< where the records this end owes are written
    size_t out_cap;  ///< how much room it has
    size_t *out_len; ///< where an open reports the plaintext length, or NULL
} TlsConnOut;

/**
 * @brief One TLS 1.3 handshake over a stream. ::TlsConn is the resource; this drives it.
 *
 * A caller sets the members a call takes, invokes it through ::TlsConnection, and reads the outcome
 * off the same handle. The connection itself is the caller's, named in @ref TlsConnNs::conn.
 *
 * @var TlsConnNs::conn         the connection every call acts on
 * @var TlsConnNs::init_args    which end this connection drives, and what it presents
 * @var TlsConnNs::io           the bytes one call moves
 * @var TlsConnNs::out_args     where a call writes, and what it wrote
 * @var TlsConnNs::ok           a call's true/false outcome
 * @var TlsConnNs::n            bytes written to @c out_args.out
 * @var TlsConnNs::i32          bytes written, or a negative alert-bearing failure
 * @var TlsConnNs::u8           the alert a lookup reports
 * @var TlsConnNs::init         bind a connection to its role and configuration. The borrow is taken
 *                              on first use and kept, so a connection initialised again reuses the
 *                              bytes it already holds
 * @var TlsConnNs::start        client only: write the ClientHello; bytes written, or 0
 * @var TlsConnNs::process      the worker filled @ref TlsConn::rx with one record of @c io.rx_len
 *                              bytes; consume it, writing whatever it owes into @c out_args.out
 * @var TlsConnNs::established  whether the handshake has completed
 * @var TlsConnNs::alert        the alert that ended the connection, or 0
 * @var TlsConnNs::seal_app     seal application data into one record; bytes written, or 0
 * @var TlsConnNs::open_app     open one received application record; false on an AEAD failure
 *
 * No storage member: one secure-pool borrow per connection lives in ::TlsConn, taken by
 * @ref TlsConnNs::init and split by offset.
 */
typedef struct
{
    TlsConn *conn;
    TlsInitArgs init_args;
    TlsConnIoArgs io;
    TlsConnOut out_args;
    proto_bool ok;
    size_t n;
    int i32;
    uint8_t u8;
} TlsConnectionVars;

/** @brief The operands and the outcome. */
extern TlsConnectionVars TlsConnectionV;

/** @brief The entries. */
typedef struct
{
    void (*const init)(uint8_t *restrict work);
    void (*const start)(uint8_t *restrict work);
    void (*const process)(uint8_t *restrict work);
    void (*const established)(uint8_t *restrict work);
    void (*const alert)(uint8_t *restrict work);
    void (*const seal_app)(uint8_t *restrict work);
    void (*const open_app)(uint8_t *restrict work);
} TlsConnNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in TlsConnectionV or a region of the borrow at a fixed offset.
void protocore_tls_connection_init(uint8_t *restrict work);
void protocore_tls_connection_start(uint8_t *restrict work);
void protocore_tls_connection_process(uint8_t *restrict work);
void protocore_tls_connection_established(uint8_t *restrict work);
void protocore_tls_connection_alert(uint8_t *restrict work);
void protocore_tls_connection_seal_app(uint8_t *restrict work);
void protocore_tls_connection_open_app(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `TlsConnection.init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const TlsConnNs TlsConnection __attribute__((unused)) = {
    .init = protocore_tls_connection_init,
    .start = protocore_tls_connection_start,
    .process = protocore_tls_connection_process,
    .established = protocore_tls_connection_established,
    .alert = protocore_tls_connection_alert,
    .seal_app = protocore_tls_connection_seal_app,
    .open_app = protocore_tls_connection_open_app,
};

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

/**
 * @brief Install the credential this end presents, before any connection begins.
 *
 * The two arms take different credentials, because they are different engines:
 *   - software (PROTOCORE_TLS_SOFTWARE): RFC 7250 raw public keys. @p cert is the 32-byte Ed25519
 *     public key and @p key the 32-byte signing seed that matches it.
 *   - a vendor engine (PROTOCORE_HAS_VENDOR_TLS): whatever that engine parses, an X.509 chain and
 *     its private key for the mbedTLS binding.
 *
 * @return true once a connection may begin.
 */
proto_bool protocore_tls_global_init(const uint8_t *cert, size_t cert_len, const uint8_t *key, size_t key_len);

/** @brief Whether a credential is installed and a connection may begin. */
proto_bool protocore_tls_ready(void);

/** @brief Stand a TLS connection up on @p slot, taking the pcb it will write through. */
proto_bool protocore_tls_conn_begin(uint8_t slot);

/**
 * @brief Pump the handshake on @p slot with whatever ciphertext has arrived.
 *
 * @return < 0 the connection failed and must be aborted; 0 still handshaking, ask again when more
 *         ciphertext lands; > 0 the handshake completed.
 */
int protocore_tls_handshake(uint8_t slot);

/** @brief Whether the handshake on @p slot has completed. */
proto_bool protocore_tls_established(uint8_t slot);

/**
 * @brief Open one received application record on @p slot into @p buf.
 *
 * @return the plaintext length, 0 when no whole record has arrived, or < 0 on an AEAD failure,
 *         which RFC 8446 sec 5.2 makes fatal to the connection.
 */
int protocore_tls_read(uint8_t slot, uint8_t *buf, size_t len);

/**
 * @brief Seal @p len bytes into one application record on @p slot and put it on the wire.
 *
 * @return @p len once the record is queued, or < 0 when it did not fit one record or the transport
 *         refused it.
 */
int protocore_tls_write(uint8_t slot, const void *data, size_t len);

/** @brief End the connection on @p slot and wipe every key generation it installed. */
void protocore_tls_conn_end(uint8_t slot);

/** @brief Release @p slot's connection without ending it: the transport is already gone. */
void protocore_tls_conn_free(uint8_t slot);

/** @brief The high-water mark of the pool this engine's connections are taken from. */
size_t protocore_tls_arena_peak(void);

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
static inline void protocore_tls_client_set_pin(const uint8_t pin[32])
{
    (void)pin;
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
