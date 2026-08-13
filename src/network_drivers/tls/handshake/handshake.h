// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file handshake.h
 * @brief TLS 1.3 handshake driver over the stream record layer (RFC 8446 sec 4).
 *
 * The TCP counterpart to protocore_dtls_conn: it drives one TLS 1.3 handshake to completion over
 * ::TlsRecord, wiring the shared message builders (protocore_tls13_msg) and key schedule (::Tls13Ks)
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
 * core_setup/board_profiles/protocore_platform.h the portable arm authenticates by RFC 7250 raw public
 * key: there is no X.509 chain building, no name matching, and no RSA or ECDSA. A peer that offers
 * none of the profile is a handshake failure.
 *
 * Nothing here blocks and nothing here owns a socket. The caller feeds received bytes and sends
 * whatever @ref TlsConnNs::process writes back, exactly as protocore_dtls_conn is fed datagrams.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TLS_HANDSHAKE_H
#define PROTOCORE_TLS_HANDSHAKE_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_TLS_SOFTWARE

#include "network_drivers/tls/tls.h" // TlsConn, TlsConnConfig, TlsRole: the resource this drives

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

/** @brief The driver's own calls, described only in handshake.c. */
struct TlsConnInternal;

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
 * @var TlsConnNs::internal     the connection in hand, and the calls that drive it
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

    void (*init)(struct TlsConnInternal *ctx);
    void (*start)(struct TlsConnInternal *ctx);
    void (*process)(struct TlsConnInternal *ctx);
    void (*established)(struct TlsConnInternal *ctx);
    void (*alert)(struct TlsConnInternal *ctx);
    void (*seal_app)(struct TlsConnInternal *ctx);
    void (*open_app)(struct TlsConnInternal *ctx);

    struct TlsConnInternal *internal;
} TlsConnNs;

/** @brief The one symbol this module exports. */
extern TlsConnNs TlsConnection;

#endif // PROTOCORE_TLS_SOFTWARE

PROTOCORE_END_DECLS

#endif // PROTOCORE_TLS_HANDSHAKE_H
