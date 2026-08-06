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
 * SshKex.set_prefer_rsa() order (default: RSA/DH, hardware-accelerated on ESP32) and
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
#include "network_drivers/tls/ssh_kexhash.h"
#include "protocore_config.h"

PROTO_BEGIN_DECLS

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
    proto_bool have_session_id;              ///< True once the first KEX completes.

    proto_bool ext_info_c; ///< Client advertised ext-info-c (RFC 8308): send EXT_INFO.
    proto_bool authed;     ///< True after successful user authentication.
    uint8_t auth_failures; ///< Failed USERAUTH_REQUESTs (brute-force limit, RFC 4252 §4).
    uint32_t last_kex_ms;  ///< pc_millis() when the last KEX completed (server-initiated re-key timer).
} SshSession;

/** @brief Static pool of SSH session state (BSS), one per SSH slot. */
extern SshSession ssh_sess[MAX_SSH_CONNS];

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

#ifdef PC_SSH_KEX_BENCH
// Wall-clock KEX bench (perf / FEATURE_PERFORMANCE): one owned context holding the two device-side compute
// spans of a key exchange, in microseconds. SshKex.generate records the ephemeral-keygen span (one X25519
// base multiply for a curve25519 KEX) into last_kexgen_us; SshKex.dh_handle records the reply span
// (shared-secret X25519 + host-key sign + exchange hash + KDF + reply assembly) into last_kexreply_us and
// bumps kex_count. The rig firmware watches kex_count and prints both over its own serial - src writes no
// output. Compiled out entirely unless PC_SSH_KEX_BENCH is defined (a rig-only measurement build).
typedef struct
{
    volatile long long last_kexgen_us;   ///< SshKex.generate: ephemeral X25519 base-multiply span.
    volatile long long last_kexreply_us; ///< SshKex.dh_handle: reply span (shared secret + sign + hash + KDF).
    volatile unsigned kex_count;         ///< bumped after each completed KEX; the rig prints on change.
} SshKexBenchCtx;
extern SshKexBenchCtx pc_ssh_kex_bench;
#endif

/**
 * @brief The server's host keys: install one, and ask whether it is installed.
 *
 * @var SshHostkeyNs::ed25519_set        Install an ssh-ed25519 host key from its 32-byte seed (RFC 8032 private
 *                                       key)
 * @var SshHostkeyNs::ed25519_available  True if an ssh-ed25519 host key has been installed
 * @var SshHostkeyNs::ecdsa_set          Install an ecdsa-sha2-nistp256 host key from its 32-byte P-256 private
 *                                       scalar
 * @var SshHostkeyNs::ecdsa_available    True if an ecdsa-sha2-nistp256 host key has been installed
 */
typedef struct
{
    void (*ed25519_set)(const uint8_t seed[32]);
    proto_bool (*ed25519_available)(void);
    void (*ecdsa_set)(const uint8_t priv[32]);
    proto_bool (*ecdsa_available)(void);
} SshHostkeyNs;

/** @brief The one symbol this module exports. */
extern const SshHostkeyNs SshHostkey;

/**
 * @brief Key exchange (RFC 4253 sec 7): KEXINIT, the algorithm preference, the exchange hash, and the
 * Diffie-Hellman reply. The arithmetic underneath is @ref SshDh.
 *
 * @var SshKexNs::init_build      Build the server KEXINIT payload for slot @p i (RFC 4253 §7.1)
 * @var SshKexNs::init_parse      Parse and negotiate the client KEXINIT payload (RFC 4253 §7.1)
 * @var SshKexNs::set_prefer_rsa  Steer KEX / host-key negotiation toward RSA + DH-group14 (default) or toward
 *                                the modern curve25519 + ed25519 suite
 * @var SshKexNs::prefer_rsa      Current negotiation preference (true = prefer RSA/DH, the ESP32-accelerated
 *                                path)
 * @var SshKexNs::generate        Generate the server ephemeral for the negotiated KEX method (call after parse)
 * @var SshKexNs::exchange_hash   Compute the SSH exchange hash H (RFC 4253 §8)
 * @var SshKexNs::dh_parse_init   Parse SSH_MSG_KEXDH_INIT, extracting the client DH value e
 * @var SshKexNs::dh_build_reply  Build SSH_MSG_KEXDH_REPLY (RFC 4253 §8, RFC 8332 §3)
 * @var SshKexNs::dh_handle       Handle KEXDH/ECDH_INIT (msg 30) end-to-end and produce the reply payload
 * @var SshKexNs::dh                   @ref SshDh
 */
typedef struct
{
    int (*init_build)(uint8_t i, uint8_t *payload, size_t *len, size_t cap);
    int (*init_parse)(uint8_t i, const uint8_t *payload, size_t len);
    void (*set_prefer_rsa)(proto_bool prefer);
    proto_bool (*prefer_rsa)(void);
    int (*generate)(uint8_t i);
    int (*exchange_hash)(uint8_t i, const uint8_t *e_be, const uint8_t *f_be, const uint8_t *k_be, const uint8_t *ks,
                         size_t ks_len, uint8_t out[PC_SHA256_DIGEST_LEN]);
    int (*dh_parse_init)(const uint8_t *payload, size_t len, uint8_t e_be[256]);
    int (*dh_build_reply)(const uint8_t *ks, size_t ks_len, const uint8_t *f_be, const uint8_t *sig, size_t sig_len,
                          uint8_t *out, size_t *out_len, size_t cap);
    int (*dh_handle)(uint8_t i, const uint8_t *payload, size_t len, uint8_t *reply_out, size_t *reply_len, size_t cap);
    const SshDhNs *dh;
} SshKexNs;

/** @brief The one symbol this module exports. */
extern const SshKexNs SshKex;

/**
 * @brief The transport layer (RFC 4253): the banner exchange, EXT_INFO, NEWKEYS, and the rekey trigger.
 * Key exchange, the host keys, the packet protocol and compression hang off it.
 *
 * @var SshTransportNs::init              Reset transport state for slot @p i to the start of a handshake
 * @var SshTransportNs::server_banner     Write the server identification string ("SSH-2.0-…\r\n") to @p out
 * @var SshTransportNs::recv_banner       Feed raw bytes while awaiting the client identification string
 * @var SshTransportNs::extinfo_build     Build SSH_MSG_EXT_INFO advertising server-sig-algs (RFC 8308)
 * @var SshTransportNs::newkeys_sent      Activate the outbound direction after emitting our SSH_MSG_NEWKEYS
 * @var SshTransportNs::newkeys_complete  Complete the NEWKEYS exchange: activate the inbound direction and
 *                                        advance phase
 * @var SshTransportNs::rekey_needed      True if slot @p i has reached the re-key threshold (RFC 4253 §9)
 * @var SshTransportNs::rekey_due         Pure re-key decision (RFC 4253 §9: "after each gigabyte ... or after
 *                                        each hour")
 * @var SshTransportNs::begin_rekey       Begin a server-initiated re-key by emitting a fresh KEXINIT
 * @var SshTransportNs::kex                  @ref SshKex
 * @var SshTransportNs::hostkey              @ref SshHostkey
 * @var SshTransportNs::packet               @ref SshPacket
 * @var SshTransportNs::comp                 @ref SshComp, when PC_ENABLE_SSH_ZLIB is set
 */
typedef struct
{
    void (*init)(uint8_t i);
    int (*server_banner)(uint8_t *out, size_t *out_len, size_t cap);
    int (*recv_banner)(uint8_t i, const uint8_t *data, size_t len, size_t *consumed);
    int (*extinfo_build)(uint8_t *out, size_t *len, size_t cap);
    void (*newkeys_sent)(uint8_t i);
    void (*newkeys_complete)(uint8_t i);
    proto_bool (*rekey_needed)(uint8_t i);
    proto_bool (*rekey_due)(uint32_t seq_send, uint32_t seq_recv, uint32_t elapsed_ms, uint32_t pkt_threshold,
                            uint32_t time_threshold_ms);
    int (*begin_rekey)(uint8_t i, uint8_t *out, size_t *out_len, size_t cap);
    const SshKexNs *kex;
    const SshHostkeyNs *hostkey;
    const SshPacketNs *packet;
#if PC_ENABLE_SSH_ZLIB
    const SshCompNs *comp;
#endif
} SshTransportNs;

/** @brief The one symbol this module exports. */
extern const SshTransportNs SshTransport;

PROTO_END_DECLS

#endif // PROTOCORE_SSH_TRANSPORT_H
