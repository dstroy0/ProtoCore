// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tls.h
 * @brief Deterministic TLS engine: mbedTLS over a static memory pool (PROTOCORE_ENABLE_TLS).
 *
 * Wraps mbedTLS as a server-side TLS layer that keeps the library's zero-heap
 * guarantee: mbedTLS is pointed at a fixed BSS arena via
 * MBEDTLS_MEMORY_BUFFER_ALLOC_C (no system heap), per-connection ssl_context
 * lives in BSS, the RNG is the ESP32 hardware CSPRNG, and the transport BIO is
 * bridged directly to the existing lwIP `tcp_pcb` + per-connection rx ring - so
 * there is no socket layer and no extra task. The handshake is pumped from the
 * single `handle()` loop.
 *
 * This is the vendor arm, selected by PROTOCORE_HAS_VENDOR_TLS. Its complement inside
 * PROTOCORE_ENABLE_TLS is PROTOCORE_TLS_SOFTWARE, the portable TLS 1.3 over the TCP record
 * layer. The header compiles everywhere - without PROTOCORE_ENABLE_TLS and a vendor
 * stack the functions are no-op stubs, so call sites need no extra guards.
 *
 * Lifecycle per connection:
 * @code
 *   protocore_tls_conn_begin(slot);                 // at accept on the TLS port
 *   // each EvtType::EVT_DATA, until established:
 *   int h = protocore_tls_handshake(slot);          // 1 done, 0 pending, <0 fatal
 *   // once established, app data:
 *   int n = protocore_tls_read(slot, buf, sizeof buf);   // >0 plaintext, 0 again, <0 closed
 *   protocore_tls_write(slot, data, len);                // encrypts -> tcp_write
 *   protocore_tls_conn_end(slot);                    // close_notify + free slot ctx
 * @endcode
 */

#ifndef PROTOCORE_TLS_H
#define PROTOCORE_TLS_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

/** @brief Where a stepped TLS exchange stands. */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_TLS_READY = 0, ///< the move landed
    PROTOCORE_TLS_BUSY,      ///< the peer's bytes are still in flight; ask again on the next tick
    PROTOCORE_TLS_FAILED,    ///< the session did not stand up, or it is closed / fatal
} protocore_tls_state;

#if PROTOCORE_ENABLE_TLS && PROTOCORE_HAS_VENDOR_TLS

/**
 * @brief Initialize the global TLS engine: static pool, RNG, server cert/key.
 *
 * Call once before begin(). Parses the server certificate chain and private key
 * (PEM - NUL-terminated incl. the terminator in the length - or DER) and builds
 * the shared mbedTLS server config. All allocations come from the static arena.
 *
 * @param cert      Certificate (chain) buffer.
 * @param cert_len  Length incl. the trailing NUL for PEM.
 * @param key       Private key buffer.
 * @param key_len   Length incl. the trailing NUL for PEM.
 * @return true on success; false if the pool/cert/key setup failed.
 */
proto_bool protocore_tls_global_init(const uint8_t *cert, size_t cert_len, const uint8_t *key, size_t key_len);

/** @brief True once protocore_tls_global_init() has succeeded. */
proto_bool protocore_tls_ready(void);

/**
 * @brief The ALPN protocol negotiated for @p slot ("h2" or "http/1.1"), or nullptr if the client
 * offered no ALPN. Valid after the handshake completes. Used to select HTTP/2 vs HTTP/1.1.
 */
const char *protocore_tls_alpn(uint8_t slot);

/** @brief Begin a TLS session on connection @p slot (sets up ssl_context + BIO). */
proto_bool protocore_tls_conn_begin(uint8_t slot);

/**
 * @brief Advance the TLS handshake for @p slot.
 * @return 1 when established, 0 while still in progress (need more data),
 *         negative on a fatal error (caller should drop the connection).
 */
int protocore_tls_handshake(uint8_t slot);

#ifdef PROTOCORE_TLS_HS_BENCH
// Handshake-bench context (see tls.cpp): the last completed handshake's device-CPU time (summed over the
// pumped mbedtls_ssl_handshake calls, so network waits between pumps are excluded) and wall time. The rig
// firmware watches count and prints both. Compiled out unless PROTOCORE_TLS_HS_BENCH is defined.
typedef struct
{
    volatile long long last_cpu_us;
    volatile long long last_wall_us;
    volatile unsigned count;
    volatile long long pumps[8]; // per-pump device CPU (us) for pumps > 2 ms - localizes the cost to a flight
    volatile int n_pumps;
} TlsHsBenchCtx;
extern TlsHsBenchCtx protocore_tls_hs_bench;
#endif

/** @brief True once the handshake on @p slot has completed. */
proto_bool protocore_tls_established(uint8_t slot);

/**
 * @brief Read decrypted application data from @p slot.
 * @return >0 plaintext bytes, 0 if none are available yet, <0 on close/error.
 */
int protocore_tls_read(uint8_t slot, uint8_t *buf, size_t len);

/**
 * @brief Encrypt and send @p len bytes on @p slot (loops over partial writes).
 * @return bytes written, or <0 on error.
 */
int protocore_tls_write(uint8_t slot, const void *data, size_t len);

/** @brief Send close_notify and tear down the per-connection TLS context. */
void protocore_tls_conn_end(uint8_t slot);

/** @brief Tear down the TLS context without close_notify (abrupt disconnect/timeout). */
void protocore_tls_conn_free(uint8_t slot);

/** @brief Peak bytes ever used from the static arena (for sizing PROTOCORE_TLS_ARENA_SIZE). */
size_t protocore_tls_arena_peak(void);

/**
 * @brief TLS BIO send/recv callbacks (mbedTLS signatures) - the transport
 *        abstraction the engine reads/writes ciphertext through.
 *
 * Both sides conform to this: the server registers BIO functions that read the
 * connection's rx ring and write via the transport (Tcp.conn->raw_send), and the
 * outbound client passes its own pair to protocore_tls_client_run(). The engine itself
 * never touches lwIP directly.
 */
typedef int (*protocore_tls_bio_send_fn)(void *ctx, const unsigned char *buf, size_t len);
typedef int (*protocore_tls_bio_recv_fn)(void *ctx, unsigned char *buf, size_t len);

#if PROTOCORE_ENABLE_MTLS
/**
 * @brief Require a verified client certificate (mTLS): install the trust-anchor CA.
 *
 * Call after protocore_tls_global_init(). Parses @p ca (PEM - length incl. the trailing
 * NUL - or DER) as the CA chain and switches the server to
 * MBEDTLS_SSL_VERIFY_REQUIRED, so the handshake demands a client certificate that
 * chains to @p ca and aborts the connection otherwise.
 *
 * @return true on success; false if the engine is not initialized or the CA
 *         failed to parse.
 */
proto_bool protocore_tls_set_client_ca(const uint8_t *ca, size_t ca_len);

/**
 * @brief Copy the established peer's certificate subject DN into @p out.
 *
 * Valid once the handshake on @p slot has completed with a verified client cert.
 * @return the subject string length written (excl. NUL), or <0 if there is no
 *         verified peer certificate.
 */
int protocore_tls_peer_subject(uint8_t slot, char *out, size_t out_len);
#endif // PROTOCORE_ENABLE_MTLS

#if PROTOCORE_ENABLE_CLIENT_TLS
/**
 * @brief Install a CA trust anchor for outbound TLS (HTTPS/MQTTS) verification.
 *
 * Pass PEM (length incl. the trailing NUL) or DER; nullptr/0 clears it. With a CA
 * installed, the client handshake verifies the server's certificate chain and its
 * hostname (SNI) and aborts the connection on failure.
 */
void protocore_tls_client_set_ca(const uint8_t *ca, size_t ca_len);

/**
 * @brief Pin the outbound server's certificate by SHA-256 (32 bytes of the DER).
 *
 * After a successful handshake the peer certificate is hashed and constant-time
 * compared to @p sha256; a mismatch (or no peer cert) fails the connection. Pass
 * nullptr to clear. Can be combined with protocore_tls_client_set_ca().
 */
void protocore_tls_client_set_pin(const uint8_t sha256[32]);

/** @brief Clear any installed client CA and cert pin (back to encrypt-only). */
void protocore_tls_client_clear_verify(void);

// --- Persistent client TLS session (one outbound connection at a time) ---
// For a long-lived encrypted client (MQTTS): handshake once, then read/write
// application data over the caller's BIO until protocore_tls_client_session_end(). Honors the
// CA/pin installed above. The BIO callbacks read ciphertext from the caller's
// receive ring and write it to the socket.

/** @brief Begin a client TLS session to @p host over the given BIO. @return false on setup failure. */
proto_bool protocore_tls_client_session_begin(const char *host, protocore_tls_bio_send_fn send_fn,
                                              protocore_tls_bio_recv_fn recv_fn);

/** @brief True while a client TLS session is live (begun, not yet ended). The session is a singleton shared
 * across all client-TLS users, so a would-be caller checks this to avoid tearing down an active session. */
proto_bool protocore_tls_client_session_active(void);

/** @brief Advance the handshake. @return ::PROTOCORE_TLS_READY established (CA/pin checked), ::PROTOCORE_TLS_BUSY
 *  pending, ::PROTOCORE_TLS_FAILED fatal. */
protocore_tls_state protocore_tls_client_session_handshake(void);

/** @brief Read decrypted application data. @return >0 bytes, 0 none yet, <0 closed/error. */
int protocore_tls_client_session_read(uint8_t *buf, size_t len);

/** @brief Encrypt and send @p len bytes. @return bytes written, or <0 on error. */
int protocore_tls_client_session_write(const uint8_t *data, size_t len);

/** @brief Send close_notify and tear down the session. */
void protocore_tls_client_session_end(void);

/**
 * @brief Discard the saved TLS session so the next csess handshake is a full one.
 *
 * With PROTOCORE_ENABLE_TLS_RESUMPTION the client keeps the last session's ticket and
 * presents it on the next protocore_tls_client_session_begin() for an abbreviated handshake. Call
 * this to force a fresh full handshake (e.g. after a credential change). A no-op
 * when resumption is disabled.
 */
void protocore_tls_client_session_forget_session(void);
#endif // PROTOCORE_ENABLE_CLIENT_TLS

#else // stubs (TLS disabled or native build)

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

#endif // PROTOCORE_ENABLE_TLS && PROTOCORE_HAS_VENDOR_TLS

PROTOCORE_END_DECLS

#endif // PROTOCORE_TLS_H
