// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tls.h
 * @brief Deterministic TLS engine: mbedTLS over a static memory pool (PC_ENABLE_TLS).
 *
 * Wraps mbedTLS as a server-side TLS layer that keeps the library's zero-heap
 * guarantee: mbedTLS is pointed at a fixed BSS arena via
 * MBEDTLS_MEMORY_BUFFER_ALLOC_C (no system heap), per-connection ssl_context
 * lives in BSS, the RNG is the ESP32 hardware CSPRNG, and the transport BIO is
 * bridged directly to the existing lwIP `tcp_pcb` + per-connection rx ring - so
 * there is no socket layer and no extra task. The handshake is pumped from the
 * single `handle()` loop.
 *
 * ESP32/Arduino only - mbedTLS is not part of the native build. The header
 * compiles everywhere (the functions are no-op stubs unless PC_ENABLE_TLS and
 * ARDUINO are both set) so call sites need no extra guards.
 *
 * Lifecycle per connection:
 * @code
 *   pc_tls_conn_begin(slot);                 // at accept on the TLS port
 *   // each EvtType::EVT_DATA, until established:
 *   int h = pc_tls_handshake(slot);          // 1 done, 0 pending, <0 fatal
 *   // once established, app data:
 *   int n = pc_tls_read(slot, buf, sizeof buf);   // >0 plaintext, 0 again, <0 closed
 *   pc_tls_write(slot, data, len);                // encrypts -> tcp_write
 *   pc_tls_conn_end(slot);                    // close_notify + free slot ctx
 * @endcode
 */

#ifndef PROTOCORE_TLS_H
#define PROTOCORE_TLS_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_TLS && PROTOCORE_HOT

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
proto_bool pc_tls_global_init(const uint8_t *cert, size_t cert_len, const uint8_t *key, size_t key_len);

/** @brief True once pc_tls_global_init() has succeeded. */
proto_bool pc_tls_ready(void);

/**
 * @brief The ALPN protocol negotiated for @p slot ("h2" or "http/1.1"), or nullptr if the client
 * offered no ALPN. Valid after the handshake completes. Used to select HTTP/2 vs HTTP/1.1.
 */
const char *pc_tls_alpn(uint8_t slot);

/** @brief Begin a TLS session on connection @p slot (sets up ssl_context + BIO). */
proto_bool pc_tls_conn_begin(uint8_t slot);

/**
 * @brief Advance the TLS handshake for @p slot.
 * @return 1 when established, 0 while still in progress (need more data),
 *         negative on a fatal error (caller should drop the connection).
 */
int pc_tls_handshake(uint8_t slot);

#ifdef PC_TLS_HS_BENCH
// Handshake-bench context (see tls.cpp): the last completed handshake's device-CPU time (summed over the
// pumped mbedtls_ssl_handshake calls, so network waits between pumps are excluded) and wall time. The rig
// firmware watches count and prints both. Compiled out unless PC_TLS_HS_BENCH is defined.
typedef struct
{
    volatile long long last_cpu_us;
    volatile long long last_wall_us;
    volatile unsigned count;
    volatile long long pumps[8]; // per-pump device CPU (us) for pumps > 2 ms - localizes the cost to a flight
    volatile int n_pumps;
} TlsHsBenchCtx;
extern TlsHsBenchCtx pc_tls_hs_bench;
#endif

/** @brief True once the handshake on @p slot has completed. */
proto_bool pc_tls_established(uint8_t slot);

/**
 * @brief Read decrypted application data from @p slot.
 * @return >0 plaintext bytes, 0 if none are available yet, <0 on close/error.
 */
int pc_tls_read(uint8_t slot, uint8_t *buf, size_t len);

/**
 * @brief Encrypt and send @p len bytes on @p slot (loops over partial writes).
 * @return bytes written, or <0 on error.
 */
int pc_tls_write(uint8_t slot, const void *data, size_t len);

/** @brief Send close_notify and tear down the per-connection TLS context. */
void pc_tls_conn_end(uint8_t slot);

/** @brief Tear down the TLS context without close_notify (abrupt disconnect/timeout). */
void pc_tls_conn_free(uint8_t slot);

/** @brief Peak bytes ever used from the static arena (for sizing PC_TLS_ARENA_SIZE). */
size_t pc_tls_arena_peak(void);

/**
 * @brief TLS BIO send/recv callbacks (mbedTLS signatures) - the transport
 *        abstraction the engine reads/writes ciphertext through.
 *
 * Both sides conform to this: the server registers BIO functions that read the
 * connection's rx ring and write via the transport (Tcp.conn->raw_send), and the
 * outbound client passes its own pair to pc_tls_client_run(). The engine itself
 * never touches lwIP directly.
 */
typedef int (*pc_tls_bio_send_fn)(void *ctx, const unsigned char *buf, size_t len);
typedef int (*pc_tls_bio_recv_fn)(void *ctx, unsigned char *buf, size_t len);

#if PC_ENABLE_MTLS
/**
 * @brief Require a verified client certificate (mTLS): install the trust-anchor CA.
 *
 * Call after pc_tls_global_init(). Parses @p ca (PEM - length incl. the trailing
 * NUL - or DER) as the CA chain and switches the server to
 * MBEDTLS_SSL_VERIFY_REQUIRED, so the handshake demands a client certificate that
 * chains to @p ca and aborts the connection otherwise.
 *
 * @return true on success; false if the engine is not initialized or the CA
 *         failed to parse.
 */
proto_bool pc_tls_set_client_ca(const uint8_t *ca, size_t ca_len);

/**
 * @brief Copy the established peer's certificate subject DN into @p out.
 *
 * Valid once the handshake on @p slot has completed with a verified client cert.
 * @return the subject string length written (excl. NUL), or <0 if there is no
 *         verified peer certificate.
 */
int pc_tls_peer_subject(uint8_t slot, char *out, size_t out_len);
#endif // PC_ENABLE_MTLS

#if PC_ENABLE_HTTP_CLIENT_TLS
/**
 * @brief Run a blocking client-side TLS exchange over caller-supplied BIO callbacks.
 *
 * Performs a TLS 1.2+ client handshake (SNI = @p host, server cert not verified -
 * see note), writes @p req, then reads the decrypted response into @p out until
 * the peer closes or @p out fills. Uses the shared static arena (installs the
 * allocator if the server side has not). Yields with pcdelay() while waiting, up to
 * @p deadline_ms (millis() timestamp).
 *
 * NOTE: server authentication is OFF by default (no trust store on the device);
 * the transport is encrypted but unauthenticated unless a CA and/or a cert pin is
 * installed via pc_tls_client_set_ca() / pc_tls_client_set_pin().
 *
 * @return 0 on success (@p out_len set), <0 on handshake/verification/IO failure.
 */
int pc_tls_client_run(const char *host, const uint8_t *req, size_t reqlen, uint8_t *out, size_t out_cap,
                      size_t *out_len, pc_tls_bio_send_fn send_fn, pc_tls_bio_recv_fn recv_fn, uint32_t deadline_ms);
#endif // PC_ENABLE_HTTP_CLIENT_TLS

#if PC_ENABLE_CLIENT_TLS
/**
 * @brief Install a CA trust anchor for outbound TLS (HTTPS/MQTTS) verification.
 *
 * Pass PEM (length incl. the trailing NUL) or DER; nullptr/0 clears it. With a CA
 * installed, the client handshake verifies the server's certificate chain and its
 * hostname (SNI) and aborts the connection on failure.
 */
void pc_tls_client_set_ca(const uint8_t *ca, size_t ca_len);

/**
 * @brief Pin the outbound server's certificate by SHA-256 (32 bytes of the DER).
 *
 * After a successful handshake the peer certificate is hashed and constant-time
 * compared to @p sha256; a mismatch (or no peer cert) fails the connection. Pass
 * nullptr to clear. Can be combined with pc_tls_client_set_ca().
 */
void pc_tls_client_set_pin(const uint8_t sha256[32]);

/** @brief Clear any installed client CA and cert pin (back to encrypt-only). */
void pc_tls_client_clear_verify(void);

// --- Persistent client TLS session (one outbound connection at a time) ---
// For a long-lived encrypted client (MQTTS): handshake once, then read/write
// application data over the caller's BIO until pc_tls_client_session_end(). Honors the
// CA/pin installed above. The BIO callbacks read ciphertext from the caller's
// receive ring and write it to the socket.

/** @brief Begin a client TLS session to @p host over the given BIO. @return false on setup failure. */
proto_bool pc_tls_client_session_begin(const char *host, pc_tls_bio_send_fn send_fn, pc_tls_bio_recv_fn recv_fn);

/** @brief True while a client TLS session is live (begun, not yet ended). The session is a singleton shared
 * across all client-TLS users, so a would-be caller checks this to avoid tearing down an active session. */
proto_bool pc_tls_client_session_active(void);

/** @brief Advance the handshake. @return 1 established (CA/pin checked), 0 pending, <0 fatal. */
int pc_tls_client_session_handshake(void);

/** @brief Read decrypted application data. @return >0 bytes, 0 none yet, <0 closed/error. */
int pc_tls_client_session_read(uint8_t *buf, size_t len);

/** @brief Encrypt and send @p len bytes. @return bytes written, or <0 on error. */
int pc_tls_client_session_write(const uint8_t *data, size_t len);

/** @brief Send close_notify and tear down the session. */
void pc_tls_client_session_end(void);

/**
 * @brief Discard the saved TLS session so the next csess handshake is a full one.
 *
 * With PC_ENABLE_TLS_RESUMPTION the client keeps the last session's ticket and
 * presents it on the next pc_tls_client_session_begin() for an abbreviated handshake. Call
 * this to force a fresh full handshake (e.g. after a credential change). A no-op
 * when resumption is disabled.
 */
void pc_tls_client_session_forget_session(void);
#endif // PC_ENABLE_CLIENT_TLS

#else // stubs (TLS disabled or native build)

static inline proto_bool pc_tls_global_init(const uint8_t *cert, size_t cert_len, const uint8_t *key, size_t key_len)
{
    (void)cert;
    (void)cert_len;
    (void)key;
    (void)key_len;
    return PROTO_FALSE;
}
static inline proto_bool pc_tls_ready(void)
{
    return PROTO_FALSE;
}
static inline proto_bool pc_tls_conn_begin(uint8_t slot)
{
    (void)slot;
    return PROTO_FALSE;
}
static inline int pc_tls_handshake(uint8_t slot)
{
    (void)slot;
    return -1;
}
static inline proto_bool pc_tls_established(uint8_t slot)
{
    (void)slot;
    return PROTO_FALSE;
}
static inline int pc_tls_read(uint8_t slot, uint8_t *buf, size_t len)
{
    (void)slot;
    (void)buf;
    (void)len;
    return -1;
}
static inline int pc_tls_write(uint8_t slot, const void *data, size_t len)
{
    (void)slot;
    (void)data;
    (void)len;
    return -1;
}
static inline void pc_tls_conn_end(uint8_t slot)
{
    (void)slot;
}
static inline void pc_tls_conn_free(uint8_t slot)
{
    (void)slot;
}
static inline size_t pc_tls_arena_peak(void)
{
    return 0;
}

#if PC_ENABLE_MTLS
static inline proto_bool pc_tls_set_client_ca(const uint8_t *ca, size_t ca_len)
{
    (void)ca;
    (void)ca_len;
    return PROTO_FALSE;
}
static inline int pc_tls_peer_subject(uint8_t slot, char *out, size_t out_len)
{
    (void)slot;
    (void)out;
    (void)out_len;
    return -1;
}
#endif // PC_ENABLE_MTLS

#if PC_ENABLE_CLIENT_TLS
typedef int (*pc_tls_bio_send_fn)(void *ctx, const unsigned char *buf, size_t len);
typedef int (*pc_tls_bio_recv_fn)(void *ctx, unsigned char *buf, size_t len);
static inline void pc_tls_client_set_ca(const uint8_t *ca, size_t ca_len)
{
    (void)ca;
    (void)ca_len;
}
static inline void pc_tls_client_set_pin(const uint8_t sha256[32])
{
    (void)sha256;
}
static inline void pc_tls_client_clear_verify(void)
{
}
static inline proto_bool pc_tls_client_session_begin(const char *host, pc_tls_bio_send_fn send_fn,
                                                     pc_tls_bio_recv_fn recv_fn)
{
    (void)host;
    (void)send_fn;
    (void)recv_fn;
    return PROTO_FALSE;
}
static inline proto_bool pc_tls_client_session_active(void)
{
    return PROTO_FALSE;
}
static inline int pc_tls_client_session_handshake(void)
{
    return -1;
}
static inline int pc_tls_client_session_read(uint8_t *buf, size_t len)
{
    (void)buf;
    (void)len;
    return -1;
}
static inline int pc_tls_client_session_write(const uint8_t *data, size_t len)
{
    (void)data;
    (void)len;
    return -1;
}
static inline void pc_tls_client_session_end(void)
{
}
static inline void pc_tls_client_session_forget_session(void)
{
}
#endif // PC_ENABLE_CLIENT_TLS

#if PC_ENABLE_HTTP_CLIENT_TLS
static inline int pc_tls_client_run(const char *host, const uint8_t *req, size_t reqlen, uint8_t *out, size_t out_cap,
                                    size_t *out_len, pc_tls_bio_send_fn send_fn, pc_tls_bio_recv_fn recv_fn,
                                    uint32_t deadline_ms)
{
    (void)host;
    (void)req;
    (void)reqlen;
    (void)out;
    (void)out_cap;
    (void)out_len;
    (void)send_fn;
    (void)recv_fn;
    (void)deadline_ms;
    return -1;
}
#endif // PC_ENABLE_HTTP_CLIENT_TLS

#endif // PC_ENABLE_TLS && PROTOCORE_HOT

PROTO_END_DECLS

#endif // PROTOCORE_TLS_H
