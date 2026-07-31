// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_transport.h
 * @brief SSH transport-layer protocol state machine (RFC 4253).
 *
 * Sits on top of the binary packet layer (ssh_packet.*) and the crypto
 * primitives (ssh_dh, ssh_rsa, pc_aes256ctr, pc_hmac_sha256). Drives the
 * handshake: identification-string (banner) exchange → algorithm negotiation
 * (KEXINIT) → Diffie-Hellman key exchange (KEXDH) → NEWKEYS → key install,
 * then hands off to the user-auth layer (ssh_auth.*).
 *
 * ── Supported algorithms (crypto-agnostic KEX; steered to a runtime preference) ─
 *   kex            : diffie-hellman-group14-sha256   (RFC 8268)
 *                    curve25519-sha256               (RFC 8731)
 *                    ecdh-sha2-nistp256              (RFC 5656 §4)
 *   host key / sig : rsa-sha2-512, rsa-sha2-256       (RFC 8332)
 *                    ecdsa-sha2-nistp256              (RFC 5656)
 *                    ssh-ed25519                      (RFC 8709)
 *   cipher (both)  : aes256-ctr                       (RFC 4344)
 *   MAC (both)     : hmac-sha2-256                    (RFC 6668)
 *   compression    : none
 *
 * KEX method and host-key type are negotiated: the server advertises both suites in
 * ssh_kex_set_prefer_rsa() order (default: RSA/DH, hardware-accelerated on ESP32) and
 * picks the first mutually supported one it holds a key for. Cipher / MAC / compression
 * are fixed; the connection is accepted only if the client offers each of those.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SSH_TRANSPORT_H
#define PROTOCORE_SSH_TRANSPORT_H

#include "crypto/hash/sha256.h"
#include "network_drivers/presentation/ssh/crypto/ssh_kexhash.h"
#include "network_drivers/presentation/ssh/transport/ssh_keymat.h"
#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Sizing
// ---------------------------------------------------------------------------

/** @brief Max stored length of an SSH identification string (RFC 4253 §4.2: 255). */
#define SSH_VERSION_MAX 256

/** @brief Max stored size of our own KEXINIT (I_S). Sized for the full advertised suite: the
 *  kex list (mlkem + dh + ecdh-nistp256 + curve25519 x2 + ext-info-s), all three host-key types,
 *  the cipher (chacha + 2x aes) and MAC (2x etm + 2x plain) lists, and zlib s2c compression
 *  (worst case ~580 bytes; 704 leaves headroom for future algorithm additions). */
#define PC_SSH_KEXINIT_S_MAX 704

/** @brief Server identification string (no CR LF; appended on the wire). */
#define SSH_SERVER_VERSION "SSH-2.0-1.0"

// ---------------------------------------------------------------------------
// Handshake phase
// ---------------------------------------------------------------------------

/** @brief SSH connection lifecycle phase. */
enum class SshPhase : uint8_t
{
    SSH_PHASE_BANNER,  ///< Awaiting the client identification string.
    SSH_PHASE_KEXINIT, ///< Awaiting the client KEXINIT.
    SSH_PHASE_DH_INIT, ///< Awaiting SSH_MSG_KEXDH_INIT.
    SSH_PHASE_NEWKEYS, ///< Awaiting SSH_MSG_NEWKEYS.
    SSH_PHASE_SERVICE, ///< Awaiting SERVICE_REQUEST ("ssh-userauth").
    SSH_PHASE_AUTH,    ///< User authentication in progress (RFC 4252).
    SSH_PHASE_OPEN     ///< Authenticated; connection/channel protocol active.
};

// ---------------------------------------------------------------------------
// Per-connection transport state
// ---------------------------------------------------------------------------

/**
 * @brief SSH transport/session state for one connection (BSS pool).
 *
 * Holds the handshake phase plus the few values that must persist across
 * messages to compute the exchange hash H: the client and server
 * identification strings (V_C, V_S) and the two KEXINIT payloads (I_C, I_S).
 * The exchange hash from the first KEX is retained as the session id, which
 * is required for key derivation and for every later re-key.
 */
/** @brief Negotiated key-exchange method (crypto-agnostic KEX dispatch). */
enum class SshKexAlg : uint8_t
{
    SSH_KEX_DH_GROUP14 = 0,      ///< diffie-hellman-group14-sha256 (HW-accelerated MPI on ESP32)
    SSH_KEX_CURVE25519 = 1,      ///< curve25519-sha256 (RFC 8731, X25519)
    SSH_KEX_MLKEM768_X25519 = 2, ///< mlkem768x25519-sha256 (PQ/T hybrid, draft-ietf-sshm-mlkem-hybrid-kex)
    SSH_KEX_ECDH_NISTP256 = 3,   ///< ecdh-sha2-nistp256 (NIST P-256 ECDH, RFC 5656 §4)
    SSH_KEX_SNTRUP761_X25519 = 4 ///< sntrup761x25519-sha512@openssh.com (PQ/T hybrid, SHA-512 exchange hash)
};

/** @brief Negotiated host-key / signature algorithm. */
enum class SshHostkeyAlg : uint8_t
{
    SSH_HOSTKEY_RSA_SHA256 = 0,    ///< rsa-sha2-256 (HW-accelerated on ESP32)
    SSH_HOSTKEY_ED25519 = 1,       ///< ssh-ed25519 (RFC 8032)
    SSH_HOSTKEY_RSA_SHA512 = 2,    ///< rsa-sha2-512 (same "ssh-rsa" key, SHA-512 signature; RFC 8332)
    SSH_HOSTKEY_ECDSA_NISTP256 = 3 ///< ecdsa-sha2-nistp256 (NIST P-256, RFC 5656)
};

struct SshSession
{
    SshPhase phase; ///< Current handshake phase.

    SshKexAlg kex_alg;         ///< negotiated in KEXINIT.
    SshHostkeyAlg hostkey_alg; ///< negotiated in KEXINIT.
    uint8_t cipher_alg;        ///< SSH_CIPHER_* negotiated in KEXINIT (0 = aes256-ctr).
    uint8_t mac_alg;           ///< SSH_MAC_* negotiated in KEXINIT (aes cipher only; 0 = hmac-sha2-256).
    uint8_t ecdh_sk[32];       ///< Server X25519 ephemeral private (curve25519 KEX only; wiped after).
    uint8_t ecdh_pk[32];       ///< Server X25519 ephemeral public (curve25519 KEX only).

    char v_c[SSH_VERSION_MAX]; ///< Client identification string (no CR LF).
    uint16_t v_c_len;          ///< Length of v_c.

    uint8_t banner_buf[SSH_VERSION_MAX]; ///< Accumulator for the inbound banner.
    uint16_t banner_len;                 ///< Bytes buffered in banner_buf.

    uint8_t i_c[SSH_KEXINIT_MAX];      ///< Client KEXINIT payload (for H).
    uint16_t i_c_len;                  ///< Length of i_c.
    uint8_t i_s[PC_SSH_KEXINIT_S_MAX]; ///< Server KEXINIT payload (for H).
    uint16_t i_s_len;                  ///< Length of i_s.

    uint8_t session_id[SSH_KEXHASH_MAX_LEN]; ///< H from the first KEX (RFC 4253 §7.2); 32 or 64 bytes.
    uint8_t session_id_len;                  ///< session_id length (the first KEX's exchange-hash length).
    bool have_session_id;                    ///< True once the first KEX completes.

    bool ext_info_c;       ///< Client advertised ext-info-c (RFC 8308): send EXT_INFO.
    bool authed;           ///< True after successful user authentication.
    uint8_t auth_failures; ///< Failed USERAUTH_REQUESTs (brute-force limit, RFC 4252 §4).
    uint32_t last_kex_ms;  ///< pc_millis() when the last KEX completed (server-initiated re-key timer).
};

/** @brief Static pool of SSH session state (BSS), one per SSH slot. */
extern SshSession ssh_sess[MAX_SSH_CONNS];

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

/**
 * @brief Reset transport state for slot @p i to the start of a handshake.
 */
void ssh_transport_init(uint8_t i);

/**
 * @brief Write the server identification string ("SSH-2.0-…\r\n") to @p out.
 *
 * Sent verbatim (not inside a binary packet) at connection start, before any
 * KEXINIT. The CR LF is included on the wire but excluded from V_S used in H.
 *
 * @return 0 on success, -1 if @p cap is too small.
 */
int ssh_transport_server_banner(uint8_t *out, size_t *out_len, size_t cap);

/**
 * @brief Feed raw bytes while awaiting the client identification string.
 *
 * Accumulates bytes until a CR LF (or bare LF) terminates a line beginning
 * with "SSH-". Earlier non-SSH lines (allowed by RFC 4253 §4.2) are skipped.
 * On completion the client version (without CR LF) is stored in v_c.
 *
 * @param[in]  i         SSH slot.
 * @param[in]  data      Inbound bytes.
 * @param[in]  len       Number of bytes in @p data.
 * @param[out] consumed  Bytes consumed from @p data (the banner may be followed
 *                       immediately by binary packets).
 * @return 1 when the banner is complete, 0 if more data is needed, -1 on error.
 */
int ssh_transport_recv_banner(uint8_t i, const uint8_t *data, size_t len, size_t *consumed);

/**
 * @brief Build the server KEXINIT payload for slot @p i (RFC 4253 §7.1).
 *
 * Stores a copy in ssh_sess[i].i_s for later exchange-hash computation.
 *
 * @return 0 on success, -1 on buffer overflow.
 */
int ssh_kexinit_build(uint8_t i, uint8_t *payload, size_t *len, size_t cap);

/**
 * @brief Parse and negotiate the client KEXINIT payload (RFC 4253 §7.1).
 *
 * Stores a copy in ssh_sess[i].i_c. Verifies the client offers every algorithm
 * this server supports (one per category).
 *
 * @return 0 if negotiation succeeds, -1 if any required algorithm is absent or
 *         the payload is malformed.
 */
int ssh_kexinit_parse(uint8_t i, const uint8_t *payload, size_t len);

/**
 * @brief Steer KEX / host-key negotiation toward RSA + DH-group14 (default) or toward
 *        the modern curve25519 + ed25519 suite.
 *
 * On ESP32 the RSA/DH path runs on the hardware MPI accelerator, while curve25519 /
 * ed25519 are software; a device that wants the accelerated handshake keeps the default
 * (prefer RSA), while one that wants modern crypto out of the box calls this with false.
 * The server still advertises both suites (for whatever keys it holds), so a client that
 * only supports one still connects - this only sets the server's preference order.
 *
 * Runtime-selectable so one firmware can flip per deployment. Default: prefer RSA.
 */
void ssh_kex_set_prefer_rsa(bool prefer);

/** @brief Current negotiation preference (true = prefer RSA/DH, the ESP32-accelerated path). */
bool ssh_kex_prefer_rsa(void);

/**
 * @brief Install an ssh-ed25519 host key from its 32-byte seed (RFC 8032 private key).
 *
 * Enables the ssh-ed25519 host-key algorithm for negotiation and derives the public key.
 * The RSA host key (loaded via ssh_rsa) and this may both be present; negotiation picks
 * one per ssh_kex_set_prefer_rsa(). If neither is installed the handshake cannot complete.
 */
void pc_ssh_hostkey_ed25519_set(const uint8_t seed[32]);

/** @brief True if an ssh-ed25519 host key has been installed. */
bool pc_ssh_hostkey_ed25519_available(void);

/**
 * @brief Install an ecdsa-sha2-nistp256 host key from its 32-byte P-256 private scalar.
 *
 * Enables the ecdsa-sha2-nistp256 host-key algorithm (RFC 5656) for negotiation and derives
 * the public point. May coexist with the RSA and ssh-ed25519 host keys; negotiation picks one
 * per ssh_kex_set_prefer_rsa(). An invalid scalar (0 or >= the group order) is ignored.
 */
void pc_ssh_hostkey_ecdsa_set(const uint8_t priv[32]);

/** @brief True if an ecdsa-sha2-nistp256 host key has been installed. */
bool pc_ssh_hostkey_ecdsa_available(void);

/**
 * @brief Generate the server ephemeral for the negotiated KEX method (call after parse).
 *
 * Branches on ssh_sess[i].kex_alg: for diffie-hellman-group14 it delegates to
 * ssh_dh_generate(); for curve25519-sha256 it draws a random X25519 scalar and computes
 * the matching public value into ssh_sess[i].ecdh_sk / ecdh_pk; for ecdh-sha2-nistp256 it
 * draws a random P-256 scalar into ecdh_sk (the 65-byte public point is re-derived when the
 * KEXDH_INIT arrives). Must run after ssh_kexinit_parse() has set kex_alg.
 *
 * @return 0 on success, -1 on error.
 */
int ssh_kex_generate(uint8_t i);

/**
 * @brief Build SSH_MSG_EXT_INFO advertising server-sig-algs (RFC 8308).
 *
 * Tells the client which public-key signature algorithms the server will accept
 * for userauth (rsa-sha2-512, rsa-sha2-256, ssh-ed25519); without it a modern
 * OpenSSH client refuses to sign an RSA key ("no mutual signature algorithm").
 * Sent once, right after NEWKEYS, when the client advertised ext-info-c.
 * @return 0 on success, -1 on buffer overflow.
 */
int ssh_extinfo_build(uint8_t *out, size_t *len, size_t cap);

/**
 * @brief Compute the SSH exchange hash H (RFC 4253 §8).
 *
 *   H = SHA256( string(V_C) || string(V_S) || string(I_C) || string(I_S)
 *               || string(K_S) || mpint(e) || mpint(f) || mpint(K) )
 *
 * V_C/V_S/I_C/I_S are taken from ssh_sess[i]; the rest are supplied. The
 * 256-byte big-endian integers are re-encoded as SSH mpints (minimal length,
 * leading 0x00 when the high bit is set).
 *
 * @param[in]  i      SSH slot.
 * @param[in]  e_be   Client DH public value e (256-byte big-endian).
 * @param[in]  f_be   Server DH public value f (256-byte big-endian).
 * @param[in]  k_be   Shared secret K (256-byte big-endian).
 * @param[in]  ks     Server host-key blob K_S.
 * @param[in]  ks_len Length of @p ks.
 * @param[out] out    32-byte exchange hash.
 * @return 0 on success, -1 on bad slot.
 */
int ssh_kex_exchange_hash(uint8_t i, const uint8_t *e_be, const uint8_t *f_be, const uint8_t *k_be, const uint8_t *ks,
                          size_t ks_len, uint8_t out[PC_SHA256_DIGEST_LEN]);

/**
 * @brief Parse SSH_MSG_KEXDH_INIT, extracting the client DH value e.
 *
 * Payload: byte(30) || mpint(e). The mpint is normalized into a fixed 256-byte
 * big-endian buffer (leading sign/zero bytes stripped, right-aligned).
 *
 * @param[in]  payload  KEXDH_INIT payload.
 * @param[in]  len      Payload length.
 * @param[out] e_be     256-byte big-endian client public value.
 * @return 0 on success, -1 if malformed or e exceeds 2048 bits.
 */
int ssh_kexdh_parse_init(const uint8_t *payload, size_t len, uint8_t e_be[256]);

/**
 * @brief Build SSH_MSG_KEXDH_REPLY (RFC 4253 §8, RFC 8332 §3).
 *
 * Payload: byte(31) || string(K_S) || mpint(f) || string(signature), where the
 * signature blob is string("rsa-sha2-256") || string(@p sig).
 *
 * @param[in]  ks       Server host-key blob K_S.
 * @param[in]  ks_len   Length of @p ks.
 * @param[in]  f_be     Server DH public value f (256-byte big-endian).
 * @param[in]  sig      Raw RSA signature over the exchange hash H.
 * @param[in]  sig_len  Length of @p sig (256 for RSA-2048).
 * @param[out] out      Output payload buffer.
 * @param[out] out_len  Bytes written.
 * @param[in]  cap      Capacity of @p out.
 * @return 0 on success, -1 on overflow.
 */
int ssh_kexdh_build_reply(const uint8_t *ks, size_t ks_len, const uint8_t *f_be, const uint8_t *sig, size_t sig_len,
                          uint8_t *out, size_t *out_len, size_t cap);

/**
 * @brief Handle KEXDH/ECDH_INIT (msg 30) end-to-end and produce the reply payload.
 *
 * Branches on the negotiated KEX method (ssh_sess[i].kex_alg): computes the shared
 * secret K = e^y mod p (DH-group14) or K = X25519(sk, Q_C) (curve25519), builds the
 * method-correct exchange hash H (e/f as mpints for DH, Q_C/Q_S as strings for curve),
 * signs H with the negotiated host key (rsa-sha2-512/256 or ssh-ed25519), assembles
 * SSH_MSG_KEXDH_REPLY, and derives the six session keys (installed into ssh_keys[i];
 * encryption is not activated until NEWKEYS - see ssh_newkeys_complete()). On the first
 * KEX the exchange hash is saved as the session id. K is wiped from the stack before
 * returning.
 *
 * Requires ssh_kex_generate(i) and a host key (pc_ssh_rsa_load_pubkey() and/or
 * pc_ssh_hostkey_ed25519_set()) to have been called.
 *
 * @param[in]  i          SSH slot.
 * @param[in]  payload    KEXDH_INIT payload.
 * @param[in]  len        Payload length.
 * @param[out] reply_out  KEXDH_REPLY payload buffer.
 * @param[out] reply_len  Bytes written to @p reply_out.
 * @param[in]  cap        Capacity of @p reply_out.
 * @return 0 on success, -1 on validation/crypto/buffer error.
 */
int ssh_kexdh_handle(uint8_t i, const uint8_t *payload, size_t len, uint8_t *reply_out, size_t *reply_len, size_t cap);

#ifdef PC_SSH_KEX_BENCH
// Wall-clock KEX bench (perf / FEATURE_PERFORMANCE): one owned context holding the two device-side compute
// spans of a key exchange, in microseconds. ssh_kex_generate records the ephemeral-keygen span (one X25519
// base multiply for a curve25519 KEX) into last_kexgen_us; ssh_kexdh_handle records the reply span
// (shared-secret X25519 + host-key sign + exchange hash + KDF + reply assembly) into last_kexreply_us and
// bumps kex_count. The rig firmware watches kex_count and prints both over its own serial - src writes no
// output. Compiled out entirely unless PC_SSH_KEX_BENCH is defined (a rig-only measurement build).
struct SshKexBenchCtx
{
    volatile long long last_kexgen_us;   ///< ssh_kex_generate: ephemeral X25519 base-multiply span.
    volatile long long last_kexreply_us; ///< ssh_kexdh_handle: reply span (shared secret + sign + hash + KDF).
    volatile unsigned kex_count;         ///< bumped after each completed KEX; the rig prints on change.
};
extern SshKexBenchCtx pc_ssh_kex_bench;
#endif

/**
 * @brief Activate the outbound direction after emitting our SSH_MSG_NEWKEYS.
 *
 * Call this right after sending the server's NEWKEYS: it turns on the outbound cipher/MAC (enc_out) and
 * starts the s2c compression stream. Per RFC 4253 sec 7.3 each direction activates independently, so the
 * outbound turns on when we send, not when the peer's NEWKEYS arrives.
 */
void ssh_newkeys_sent(uint8_t i);

/**
 * @brief Complete the NEWKEYS exchange: activate the inbound direction and advance phase.
 *
 * Called once the client's SSH_MSG_NEWKEYS has been received (the server having already sent its own,
 * via ssh_newkeys_sent()). Turns on the inbound cipher/MAC (enc_in), clears kex_active, and moves to
 * SshPhase::SSH_PHASE_SERVICE (or back to SshPhase::SSH_PHASE_OPEN on a re-key).
 */
void ssh_newkeys_complete(uint8_t i);

/**
 * @brief True if slot @p i has reached the re-key threshold (RFC 4253 §9).
 *
 * Checks both packet sequence numbers against SSH_REKEY_PACKET_THRESHOLD.
 */
bool ssh_rekey_needed(uint8_t i);

/**
 * @brief Pure re-key decision (RFC 4253 §9: "after each gigabyte ... or after each hour").
 *
 * @param seq_send / @param seq_recv the outbound / inbound packet counters (a data-volume proxy).
 * @param elapsed_ms milliseconds since the last KEX completed.
 * @param pkt_threshold the packet-count trigger (SSH_REKEY_PACKET_THRESHOLD).
 * @param time_threshold_ms the elapsed-time trigger (SSH_REKEY_TIME_MS); 0 disables the time trigger.
 * @return true if either a packet counter or the elapsed time has crossed its threshold.
 */
bool ssh_rekey_due(uint32_t seq_send, uint32_t seq_recv, uint32_t elapsed_ms, uint32_t pkt_threshold,
                   uint32_t time_threshold_ms);

/**
 * @brief Begin a server-initiated re-key by emitting a fresh KEXINIT.
 *
 * Generates a new ephemeral DH key pair, builds and stores a new server
 * KEXINIT (I_S), and returns the transport to SshPhase::SSH_PHASE_KEXINIT. The session
 * id and authentication state are preserved, so once the re-key completes the
 * connection resumes in its prior (authenticated) phase.
 *
 * @param[in]  i        Connection slot index.
 * @param[out] out      KEXINIT payload to send.
 * @param[out] out_len  Bytes written.
 * @param[in]  cap      Capacity of @p out.
 * @return 0 on success, -1 on error.
 */
int ssh_transport_begin_rekey(uint8_t i, uint8_t *out, size_t *out_len, size_t cap);

#endif // PROTOCORE_SSH_TRANSPORT_H
