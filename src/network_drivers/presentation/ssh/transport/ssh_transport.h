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
#include "network_drivers/presentation/ssh/transport/ssh_keymat.h"
#include "network_drivers/presentation/ssh/transport/ssh_packet.h" // SshDir - what the codec is handed
#include "network_drivers/tls/ssh_kexhash.h"
#include "protocore_config.h"

PROTO_BEGIN_DECLS

// ---------------------------------------------------------------------------
// Sizing
// ---------------------------------------------------------------------------

/** @brief Max stored length of an SSH identification string (RFC 4253 §4.2: 255). */
#define SSH_VERSION_MAX 256

/** @brief Longest identification string RFC 4253 sec 4.2 admits: 255 on the wire, CR and LF counted. */
#define SSH_VERSION_CONTENT_MAX 253

/** @brief Max stored size of our own KEXINIT (I_S). Sized for the full advertised suite: the
 *  kex list (mlkem + dh + ecdh-nistp256 + curve25519 x2 + ext-info-s), all three host-key types,
 *  the cipher (chacha + 2x aes) and MAC (2x etm + 2x plain) lists, and zlib s2c compression
 *  (worst case ~580 bytes; 704 leaves headroom for future algorithm additions). */
#define PC_SSH_KEXINIT_S_MAX 704

/** @brief Server identification string (no CR LF; appended on the wire). */
#define SSH_SERVER_VERSION "SSH-2.0-1.0"

/** @brief Client identification string (no CR LF; appended on the wire). */
#define SSH_CLIENT_VERSION "SSH-2.0-PC_client_1.0"

// ---------------------------------------------------------------------------
// Handshake phase
// ---------------------------------------------------------------------------

/** @brief SSH connection lifecycle phase. */
typedef enum PROTO_ENUM_PACKED
{
    SSH_PHASE_BANNER,  ///< Awaiting the client identification string.
    SSH_PHASE_KEXINIT, ///< Awaiting the client KEXINIT.
    SSH_PHASE_DH_INIT, ///< Awaiting SSH_MSG_KEXDH_INIT.
    SSH_PHASE_NEWKEYS, ///< Awaiting SSH_MSG_NEWKEYS.
    SSH_PHASE_SERVICE, ///< Awaiting SERVICE_REQUEST ("ssh-userauth").
    SSH_PHASE_AUTH,    ///< User authentication in progress (RFC 4252).
    SSH_PHASE_OPEN     ///< Authenticated; connection/channel protocol active.
} SshPhase;

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
typedef enum PROTO_ENUM_PACKED
{
    SSH_KEX_DH_GROUP14 = 0,      ///< diffie-hellman-group14-sha256 (HW-accelerated MPI on ESP32)
    SSH_KEX_CURVE25519 = 1,      ///< curve25519-sha256 (RFC 8731, X25519)
    SSH_KEX_MLKEM768_X25519 = 2, ///< mlkem768x25519-sha256 (PQ/T hybrid, draft-ietf-sshm-mlkem-hybrid-kex)
    SSH_KEX_ECDH_NISTP256 = 3,   ///< ecdh-sha2-nistp256 (NIST P-256 ECDH, RFC 5656 §4)
    SSH_KEX_SNTRUP761_X25519 = 4 ///< sntrup761x25519-sha512@openssh.com (PQ/T hybrid, SHA-512 exchange hash)
} SshKexAlg;

/** @brief Negotiated host-key / signature algorithm. */
typedef enum PROTO_ENUM_PACKED
{
    SSH_HOSTKEY_RSA_SHA256 = 0,    ///< rsa-sha2-256 (HW-accelerated on ESP32)
    SSH_HOSTKEY_ED25519 = 1,       ///< ssh-ed25519 (RFC 8032)
    SSH_HOSTKEY_RSA_SHA512 = 2,    ///< rsa-sha2-512 (same "ssh-rsa" key, SHA-512 signature; RFC 8332)
    SSH_HOSTKEY_ECDSA_NISTP256 = 3 ///< ecdsa-sha2-nistp256 (NIST P-256, RFC 5656)
} SshHostkeyAlg;

typedef struct
{
    SshPhase phase; ///< Current handshake phase.

    SshKexAlg kex_alg;         ///< negotiated in KEXINIT.
    SshHostkeyAlg hostkey_alg; ///< negotiated in KEXINIT.
    // RFC 4253 sec 7.1 negotiates each direction's cipher and MAC from its own name-list, so the two
    // directions may differ and are kept apart from KEXINIT through to key install.
    uint8_t cipher_alg_c2s; ///< SSH_CIPHER_* client-to-server.
    uint8_t cipher_alg_s2c; ///< SSH_CIPHER_* server-to-client.
    uint8_t mac_alg_c2s;    ///< SSH_MAC_* client-to-server (aes cipher only; 0 = hmac-sha2-256).
    uint8_t mac_alg_s2c;    ///< SSH_MAC_* server-to-client (aes cipher only; 0 = hmac-sha2-256).
    // Every buffer below is the constants region of the connection's span, at its own named offset.
    // Null until the connection claims the slot and splits its borrow.
    uint8_t *ecdh_sk; ///< 32B: Server X25519 ephemeral private (curve25519 KEX only; wiped after).
    uint8_t *ecdh_pk; ///< 32B: Server X25519 ephemeral public (curve25519 KEX only).

    char *v_c;        ///< SSH_VERSION_MAX: Client identification string (no CR LF).
    uint16_t v_c_len; ///< Length of v_c.
    char *v_s;        ///< SSH_VERSION_MAX: Server identification string (no CR LF).
    uint16_t v_s_len; ///< Length of v_s.

    uint8_t *banner_buf; ///< SSH_VERSION_MAX: Accumulator for the inbound banner.
    uint16_t banner_len; ///< Bytes buffered in banner_buf.

    uint8_t *i_c;     ///< SSH_KEXINIT_MAX: Client KEXINIT payload (for H).
    uint16_t i_c_len; ///< Length of i_c.
    uint8_t *i_s;     ///< PC_SSH_KEXINIT_S_MAX: Server KEXINIT payload (for H).
    uint16_t i_s_len; ///< Length of i_s.

    uint8_t *session_id;        ///< SSH_KEXHASH_MAX_LEN: H from the first KEX (RFC 4253 §7.2); 32 or 64 bytes.
    uint8_t session_id_len;     ///< session_id length (the first KEX's exchange-hash length).
    proto_bool have_session_id; ///< True once the first KEX completes.

    // RFC 4253 sec 6 is a layer under sec 7, so the codec is handed one of these per call rather
    // than reading them. Each switches on its own SSH_MSG_NEWKEYS (sec 7.3).
    SshDir out; ///< our outbound direction: encrypted once we sent NEWKEYS, and the epoch it reads.
    SshDir in;  ///< our inbound direction: encrypted once the peer's arrived.

    proto_bool kex_active; ///< an exchange is running, from KEXINIT to NEWKEYS (RFC 4253 sec 9).

    ///< The client guessed a KEX that lost negotiation (RFC 4253 sec 7.1): drop its guessed packet.
    proto_bool drop_guessed_kex_pkt;
    proto_bool ext_info_c;    ///< Client advertised ext-info-c (RFC 8308): send EXT_INFO.
    proto_bool ext_info_sent; ///< EXT_INFO already went out; RFC 8308 sec 2.4 allows it once.
    proto_bool authed;        ///< True after successful user authentication.
    uint8_t auth_failures;    ///< Failed USERAUTH_REQUESTs (brute-force limit, RFC 4252 §4).
    uint32_t last_kex_ms;     ///< pc_millis() when the last KEX completed (server-initiated re-key timer).
} SshSession;

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
 * @brief Write our identification string ("SSH-2.0-…\r\n") to @p out and keep it for H.
 *
 * Sent verbatim (not inside a binary packet) at connection start, before any
 * KEXINIT. The CR LF is included on the wire but excluded from the copy kept.
 * Slot @p i's role picks which string is ours and which field holds it: a
 * client sends SSH_CLIENT_VERSION and keeps it as V_C, a server sends
 * SSH_SERVER_VERSION and keeps it as V_S.
 *
 * @return 0 on success, -1 on a bad slot or if @p cap is too small.
 */
int ssh_transport_send_banner(uint8_t i, uint8_t *out, size_t *out_len, size_t cap);

/**
 * @brief Feed raw bytes while awaiting the peer's identification string.
 *
 * Accumulates bytes until a CR LF (or bare LF) terminates a line beginning
 * with "SSH-". Earlier non-SSH lines (allowed by RFC 4253 §4.2) are skipped.
 * On completion the peer's version (without CR LF) is stored in the field slot
 * @p i's role does not send: a server stores V_C, a client stores V_S.
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
 * @brief True if @p want is a complete element of the comma-separated name-list
 *        [@p list, @p list + @p len) (RFC 4253 §7.1 - no spaces).
 */
proto_bool namelist_contains(const uint8_t *list, uint32_t len, const char *want);

/** @brief Hash an SSH string (uint32 length + bytes) into the running exchange hash. */
void hash_string(SshKexHash *h, const uint8_t *data, size_t len);

/**
 * @brief Hash an SSH mpint from a fixed-width big-endian integer: leading zero bytes stripped,
 *        a 0x00 prepended when the top bit is set.
 */
void hash_mpint(SshKexHash *h, const uint8_t *be, size_t len);

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
void ssh_kex_set_prefer_rsa(proto_bool prefer);

/** @brief Current negotiation preference (true = prefer RSA/DH, the ESP32-accelerated path). */
proto_bool ssh_kex_prefer_rsa(void);

/**
 * @brief Install an ssh-ed25519 host key from its 32-byte seed (RFC 8032 private key).
 *
 * Enables the ssh-ed25519 host-key algorithm for negotiation and derives the public key.
 * The RSA host key (loaded via ssh_rsa) and this may both be present; negotiation picks
 * one per ssh_kex_set_prefer_rsa(). If neither is installed the handshake cannot complete.
 */
void pc_ssh_hostkey_ed25519_set(const uint8_t seed[32]);

/** @brief True if an ssh-ed25519 host key has been installed. */
proto_bool pc_ssh_hostkey_ed25519_available(void);

/**
 * @brief Install an ecdsa-sha2-nistp256 host key from its 32-byte P-256 private scalar.
 *
 * Enables the ecdsa-sha2-nistp256 host-key algorithm (RFC 5656) for negotiation and derives
 * the public point. May coexist with the RSA and ssh-ed25519 host keys; negotiation picks one
 * per ssh_kex_set_prefer_rsa(). An invalid scalar (0 or >= the group order) is ignored.
 */
void pc_ssh_hostkey_ecdsa_set(const uint8_t priv[32]);

/** @brief True if an ecdsa-sha2-nistp256 host key has been installed. */
proto_bool pc_ssh_hostkey_ecdsa_available(void);

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
 * @brief Handle KEXDH/ECDH_INIT (msg 30) end-to-end and produce the reply payload.
 *
 * Branches on the negotiated KEX method (ssh_sess[i].kex_alg): computes the shared
 * secret K = e^y mod p (DH-group14) or K = X25519(sk, Q_C) (curve25519), builds the
 * method-correct exchange hash H (e/f as mpints for DH, Q_C/Q_S as strings for curve),
 * signs H with the negotiated host key (rsa-sha2-512/256 or ssh-ed25519), assembles
 * SSH_MSG_KEXDH_REPLY, and derives the six session keys (installed into the epoch of ssh_keys[i]
 * neither direction reads; each direction moves to it on its own NEWKEYS - see ssh_newkeys_sent()
 * and ssh_newkeys_complete()). On the first
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
typedef struct
{
    volatile long long last_kexgen_us;   ///< ssh_kex_generate: ephemeral X25519 base-multiply span.
    volatile long long last_kexreply_us; ///< ssh_kexdh_handle: reply span (shared secret + sign + hash + KDF).
    volatile unsigned kex_count;         ///< bumped after each completed KEX; the rig prints on change.
} SshKexBenchCtx;
extern SshKexBenchCtx pc_ssh_kex_bench;
#endif

/**
 * @brief Activate the outbound direction after emitting our SSH_MSG_NEWKEYS.
 *
 * Call this right after sending the server's NEWKEYS: it moves SshSession::out to the epoch this
 * exchange derived, marks it encrypted, and starts the s2c compression stream. Per RFC 4253 sec 7.3
 * each direction activates independently, so the outbound turns on when we send, not when the peer's
 * NEWKEYS arrives.
 */
void ssh_newkeys_sent(uint8_t i);

/**
 * @brief Complete the NEWKEYS exchange: activate the inbound direction and advance phase.
 *
 * Called once the client's SSH_MSG_NEWKEYS has been received (the server having already sent its own,
 * via ssh_newkeys_sent()). Moves SshSession::in to the epoch this exchange derived and marks it
 * encrypted, releases the epoch both directions have left, clears kex_active, and moves to
 * SSH_PHASE_SERVICE (or back to SSH_PHASE_OPEN on a re-key).
 *
 * @return 0 when the exchange completed, -1 when no exchange was running for this NEWKEYS to end
 *         (RFC 4253 sec 7.3) - the caller drops the connection.
 */
int ssh_newkeys_complete(uint8_t i);

/**
 * @brief True if slot @p i has reached the re-key threshold (RFC 4253 §9).
 *
 * Checks both packet sequence numbers against SSH_REKEY_PACKET_THRESHOLD.
 */
proto_bool ssh_rekey_needed(uint8_t i);

/**
 * @brief Pure re-key decision (RFC 4253 §9: "after each gigabyte ... or after each hour").
 *
 * @param seq_send / @param seq_recv the outbound / inbound packet counters (a data-volume proxy).
 * @param elapsed_ms milliseconds since the last KEX completed.
 * @param pkt_threshold the packet-count trigger (SSH_REKEY_PACKET_THRESHOLD).
 * @param time_threshold_ms the elapsed-time trigger (SSH_REKEY_TIME_MS); 0 disables the time trigger.
 * @return true if either a packet counter or the elapsed time has crossed its threshold.
 */
proto_bool ssh_rekey_due(uint32_t seq_send, uint32_t seq_recv, uint32_t elapsed_ms, uint32_t pkt_threshold,
                         uint32_t time_threshold_ms);

/**
 * @brief Begin a server-initiated re-key by emitting a fresh KEXINIT.
 *
 * Generates a new ephemeral DH key pair, builds and stores a new server
 * KEXINIT (I_S), and returns the transport to SSH_PHASE_KEXINIT. The session
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

/**
 * @brief The RFC 4253 transport state machine, as the operations both roles consume.
 *
 * One member per rule the RFC states, so a section number resolves to exactly one implementation.
 * Role selects which half of a mirrored pair a caller sends - it does not select a second machine.
 *
 * @var SshTransportNs::recv_banner    sec 4.2 receive the peer identification string
 * @var SshTransportNs::send_banner    sec 4.2 send ours
 * @var SshTransportNs::kexinit_build  sec 7.1 build our KEXINIT, keeping a copy for H
 * @var SshTransportNs::kexinit_parse  sec 7.1 parse the peer's and negotiate every list
 * @var SshTransportNs::kex_generate   sec 8 generate this exchange's ephemeral
 * @var SshTransportNs::exchange_hash  sec 8 the exchange hash H over the negotiated method's terms
 * @var SshTransportNs::kexdh          sec 8 the KEXDH half this role sends
 * @var SshTransportNs::newkeys_sent   sec 7.3 our outbound switches when we send NEWKEYS
 * @var SshTransportNs::newkeys_recvd  sec 7.3 our inbound switches when the peer's arrives
 * @var SshTransportNs::rekey_due      sec 9 the volume and time budget since the last exchange
 * @var SshTransportNs::begin_rekey    sec 9 start a re-exchange, when not already doing one
 *
 * Not members: sec 7.2 key derivation is ssh_dh_derive_keys_sid(), already shared by both roles;
 * sec 10's service request belongs to the auth layer.
 */
typedef struct
{
    int (*recv_banner)(uint8_t i, const uint8_t *data, size_t len, size_t *consumed);
    int (*send_banner)(uint8_t i, uint8_t *out, size_t *out_len, size_t cap);
    int (*kexinit_build)(uint8_t i, uint8_t *payload, size_t *len, size_t cap);
    int (*kexinit_parse)(uint8_t i, const uint8_t *payload, size_t len);
    int (*kex_generate)(uint8_t i);
    int (*exchange_hash)(uint8_t i, const uint8_t *e_be, const uint8_t *f_be, const uint8_t *k_be, const uint8_t *ks,
                         size_t ks_len, uint8_t out[PC_SHA256_DIGEST_LEN]);
    int (*kexdh)(uint8_t i, const uint8_t *payload, size_t len, uint8_t *reply_out, size_t *reply_len, size_t cap);
    void (*newkeys_sent)(uint8_t i);
    int (*newkeys_recvd)(uint8_t i);
    proto_bool (*rekey_due)(uint32_t seq_send, uint32_t seq_recv, uint32_t elapsed_ms, uint32_t pkt_threshold,
                            uint32_t time_threshold_ms);
    int (*begin_rekey)(uint8_t i, uint8_t *out, size_t *out_len, size_t cap);
} SshTransportNs;

/** @brief The one instance, defined in ssh_transport.c. */
const SshTransportNs *pc_ssh_transport(void);

/** @brief Reader shorthand: SSH_TRANSPORT->kexinit_parse(...). */
#define SSH_TRANSPORT (pc_ssh_transport())

PROTO_END_DECLS

#endif // PROTOCORE_SSH_TRANSPORT_H
