// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_keymat.h
 * @brief SSH session key material - types, pools, and security model.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * SECURITY MODEL - READ BEFORE MODIFYING ANYTHING IN THIS FILE
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * The fundamental threat this layout defends against is **buffer-overflow
 * key extraction**: an attacker who can cause an out-of-bounds read or write
 * in the packet receive path (ssh_pool[].pkt_buf) should not be able to
 * reach AES session keys or HMAC keys in the same memory operation.
 *
 * DEFENSE 1 - Physical BSS separation
 * ─────────────────────────────────────
 * All three pools are SEPARATE static symbols:
 *
 *   ssh_pool[MAX_SSH_CONNS]   - packet assembly buffers, protocol state
 *   ssh_keys[MAX_SSH_CONNS]   - AES-256 key schedules + HMAC keys
 *   ssh_dh[MAX_SSH_CONNS]     - ephemeral DH scalar (y), server public (f), K
 *
 * The linker places these as independent objects.  An overflow inside
 * pkt_buf must cross the entire ssh_pool[] symbol, then any objects the
 * linker placed between it and ssh_keys[], before reaching key material.
 * On ESP32 with the default linker script this is a different RAM region
 * than a linear overflow from pkt_buf would reach.
 *
 * An attacker relying on a single linear write cannot bridge both gaps in
 * one step.  This is not mitigation-by-obscurity - it is the same "separate
 * key store" principle used by HSMs, but implemented in software via linker
 * symbol separation.
 *
 * DEFENSE 2 - RSA host private key is NEVER stored in static memory
 * ──────────────────────────────────────────────────────────────────
 * The RSA-2048 private key (d, p, q, dp, dq, qInv) is loaded from NVS
 * (encrypted flash on ESP32) into a LOCAL STACK FRAME inside
 * ssh_rsa_sign().  It is explicitly zeroed (via volatile memset, which
 * the compiler cannot elide) before ssh_rsa_sign() returns.
 *
 * Consequences:
 *   - A static memory scan never finds the private key.
 *   - Cold-boot attacks recover only the public key from BSS.
 *   - The exposure window is the duration of a single mbedtls_rsa_pkcs1_sign
 *     call, typically < 1 ms.
 *
 * DEFENSE 3 - DH ephemeral scalar zeroing
 * ─────────────────────────────────────────
 * y (2048-bit private DH scalar) lives in ssh_dh[slot].y.  After
 * ssh_dh_finish() derives K it calls pc_secure_wipe() on the entire SshDhState
 * struct, which uses a volatile loop to zero all 801 bytes including y,
 * f, and K.  K must be zeroed after session keys are derived from it
 * (RFC 4253 §7.2 makes no requirement, but it reduces long-term exposure).
 *
 * DEFENSE 4 - crypto_work scratch buffer zeroing
 * ────────────────────────────────────────────────
 * The Montgomery multiplication working set is borrowed from the secure pool
 * temporaries (up to 516 bytes of intermediate products that contain
 * combinations of y, K, and d fragments).  It is zeroed via pc_secure_wipe()
 * immediately after every call to bn_expmod_group14() or ssh_rsa_sign().
 *
 * DEFENSE 5 - MAC-verify-before-use (all packet input)
 * ──────────────────────────────────────────────────────
 * After key exchange, every inbound SSH binary packet is:
 *   1. Received into pkt_buf.
 *   2. Decrypted in place (AES-256-CTR).
 *   3. HMAC-SHA2-256 verified over (seq_num_be32 || plaintext_packet).
 *   4. ONLY THEN forwarded to the protocol handler.
 *
 * If HMAC verification fails the connection is closed immediately.  No
 * plaintext bytes are acted upon before the MAC is confirmed valid.
 * This prevents padding oracle and chosen-ciphertext attacks.
 *
 * DEFENSE 6 - Sequence number overflow guard
 * ────────────────────────────────────────────
 * RFC 4253 §9.3.4 requires rekeying before the sequence number wraps.
 * If seq_c2s or seq_s2c reaches 0xFFFFFFFF the connection is closed.
 * (Rekeying is not yet implemented; close-on-wrap is the safe fallback.)
 *
 * WHAT THIS DOES NOT PROTECT AGAINST
 * ────────────────────────────────────
 *   - An attacker with arbitrary-read in the process can still read
 *     ssh_keys[] - physical separation only raises the bar, not a hard wall.
 *   - Timing side-channels in the software Montgomery/AES paths are not
 *     addressed.  On ESP32 the hardware AES path (mbedtls) has
 *     implementation-level timing properties that are mbedtls's concern.
 *   - Cold-boot attacks on SRAM after power-loss are partially mitigated by
 *     the zeroing policies above, but ESP32 SRAM may retain data briefly.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 */

#ifndef PROTOCORE_SSH_KEYMAT_H
#define PROTOCORE_SSH_KEYMAT_H

#include "crypto/aead/aesgcm.h"
#include "crypto/aead/chachapoly.h"
#include "crypto/asymmetric/bignum.h"
#include "crypto/cipher/aes256ctr.h"
#include "mmgr/secure.h" // pc_secure_wipe (the canonical secure wipe)
#include "protocore_config.h"
#include <string.h>

// These two are anonymous enums: the constants are what the code names, and the negotiated value
// travels as a uint8_t through ssh_dh_derive_keys_sid, ssh_mac_is_etm, ssh_mac_len, and the
// cipher_alg / mac_mode session fields.

/** @brief Negotiated bulk cipher for a session. */
enum
{
    SSH_CIPHER_AES256CTR = 0,        ///< aes256-ctr + a separate HMAC (the fallback)
    SSH_CIPHER_CHACHA20POLY1305 = 1, ///< chacha20-poly1305@openssh.com (AEAD; no separate MAC)
    SSH_CIPHER_AES256GCM = 2,        ///< aes256-gcm@openssh.com (AEAD, RFC 5647; no separate MAC)
};

/** @brief Negotiated MAC for the aes256-ctr cipher (unused with the chacha AEAD). */
enum
{
    SSH_MAC_HMAC_SHA256 = 0,     ///< hmac-sha2-256 (encrypt-and-MAC, RFC 4253)
    SSH_MAC_HMAC_SHA512 = 1,     ///< hmac-sha2-512 (encrypt-and-MAC)
    SSH_MAC_HMAC_SHA256_ETM = 2, ///< hmac-sha2-256-etm@openssh.com (encrypt-then-MAC)
    SSH_MAC_HMAC_SHA512_ETM = 3, ///< hmac-sha2-512-etm@openssh.com (encrypt-then-MAC)
};

/** @brief True if @p mac_mode is an encrypt-then-MAC variant (length in the clear, MAC over ciphertext). */
static inline proto_bool ssh_mac_is_etm(uint8_t mac_mode)
{
    return mac_mode == SSH_MAC_HMAC_SHA256_ETM || mac_mode == SSH_MAC_HMAC_SHA512_ETM;
}
/** @brief MAC tag / key length in bytes for @p mac_mode (32 for SHA-256, 64 for SHA-512). */
static inline uint8_t ssh_mac_len(uint8_t mac_mode)
{
    return (mac_mode == SSH_MAC_HMAC_SHA512 || mac_mode == SSH_MAC_HMAC_SHA512_ETM) ? 64 : 32;
}

// Secure wipe: the canonical pc_secure_wipe() lives in mmgr/secure.h (included above). Use it for
// any buffer that held key material - a volatile store the compiler may not elide, unlike a dead memset.

// ---------------------------------------------------------------------------
// Session key material  (one entry per SSH connection)
// ---------------------------------------------------------------------------

/**
 * @brief AES-256-CTR + HMAC-SHA2-256 session keys for one SSH connection.
 *
 * This struct occupies a separate BSS symbol (ssh_keys[]) from the packet
 * receive buffer (ssh_pool[].pkt_buf).  See the security model at the top
 * of this file for why that separation matters.
 *
 * Key derivation follows RFC 4253 §7.2.  After the DH exchange hash H is
 * known and K is available, six values are derived:
 *
 *   IV_c2s  = SHA256(K || H || "A" || session_id)   [16 bytes]
 *   IV_s2c  = SHA256(K || H || "B" || session_id)   [16 bytes]
 *   key_c2s = SHA256(K || H || "C" || session_id)   [32 bytes]
 *   key_s2c = SHA256(K || H || "D" || session_id)   [32 bytes]
 *   mac_c2s = SHA256(K || H || "E" || session_id)   [32 bytes]
 *   mac_s2c = SHA256(K || H || "F" || session_id)   [32 bytes]
 *
 * aes_key/aes_ctr C→S are the client-to-server key + counter (server decrypts inbound); S→C are the
 * reverse (server encrypts outbound).
 */
typedef struct
{
    // aes256-ctr stores the raw 32-byte key and the 16-byte IV per direction and rebuilds its key schedule
    // per packet in the shared crypto scratch, so no expanded CTR key lingers here. The IV is the running
    // 128-bit counter (see aes256ctr.h).
    //
    // aes256-gcm@openssh.com shares aes_iv_* (the modes are mutually exclusive) but NOT aes_key_* - see the
    // keyed contexts below.
    uint8_t aes_key_c2s[PC_AES256CTR_KEY_LEN]; ///< AES key C→S (server decrypts inbound).
    uint8_t aes_key_s2c[PC_AES256CTR_KEY_LEN]; ///< AES key S→C (server encrypts outbound).
    uint8_t aes_iv_c2s[PC_AES256CTR_CTR_LEN];  ///< AES IV C→S (CTR counter / GCM nonce); advances per packet.
    uint8_t aes_iv_s2c[PC_AES256CTR_CTR_LEN];  ///< AES IV S→C (CTR counter / GCM nonce); advances per packet.

    uint8_t mac_key_c2s[64]; ///< HMAC key, client-to-server (aes mode); 32 bytes for SHA-256, 64 for SHA-512.
    uint8_t mac_key_s2c[64]; ///< HMAC key, server-to-client (aes mode).
    uint8_t mac_mode;        ///< SSH_MAC_* selected for the aes256-ctr cipher (0 = hmac-sha2-256 E&M).

    uint8_t cipher_mode; ///< SSH_CIPHER_* selected for this session (0 = aes256-ctr).
    // chacha20-poly1305@openssh.com: 512-bit key per direction (K_main || K_header); no IV, no MAC key.
    uint8_t chacha_key_c2s[PC_CHACHAPOLY_KEY_LEN]; ///< client-to-server, used only in chacha mode.
    uint8_t chacha_key_s2c[PC_CHACHAPOLY_KEY_LEN]; ///< server-to-client, used only in chacha mode.

    // aes256-gcm@openssh.com (RFC 5647) reuses aes_iv_* above (mode-exclusive with CTR): the low 12 bytes
    // are the nonce, advanced per packet by pc_aesgcm_iv_increment. No separate MAC key.
    //
    // It does NOT use aes_key_*. A GCM key becomes a keyed context at install time and the raw key is
    // wiped there, so in this mode the expanded schedule is the only key material resident - strictly
    // less than CTR mode keeps. The context stays for the life of the key because standing one up costs
    // ~9,200 cycles on an ESP32-S3, a fixed price per packet that dominates small interactive traffic
    // (see aesgcm.h). Wiped on rekey and by ssh_keymat_wipe() on close.
    _Alignas(8) uint8_t gcm_ctx_c2s[PC_WORK_AESGCM]; ///< keyed GCM context C→S (server opens inbound).
    _Alignas(8) uint8_t gcm_ctx_s2c[PC_WORK_AESGCM]; ///< keyed GCM context S→C (server seals outbound).

    proto_bool active; ///< True once keys are installed after successful KEX.
} SshKeyMat;

/**
 * @brief Pool of session key material, one entry per MAX_SSH_CONNS.
 *
 * Separate BSS symbol from ssh_pool[] - see security model.
 * Zeroed on connection close by ssh_keymat_wipe(slot).
 */
extern SshKeyMat ssh_keys[MAX_SSH_CONNS];

// ---------------------------------------------------------------------------
// DH ephemeral state  (one entry per SSH connection, zeroed after KEX)
// ---------------------------------------------------------------------------

/**
 * @brief Ephemeral Diffie-Hellman state for one SSH connection.
 *
 * The three pc_bignum fields (y, f, K) together hold 768 bytes of sensitive
 * material.  The entire struct is wiped by ssh_dh_wipe() immediately after
 * session keys are derived from K.
 *
 * FIELD LIFETIME:
 *   y  - generated by ssh_dh_generate(); zeroed in ssh_dh_wipe().
 *   f  - computed in ssh_dh_generate() as g^y mod p; sent in KEXDH_REPLY;
 *         zeroed in ssh_dh_wipe().
 *   K  - computed in ssh_dh_finish() as e^y mod p; used for key derivation;
 *         zeroed in ssh_dh_wipe() AFTER keys are installed.
 *   H  - the exchange hash; becomes the session_id for the connection's
 *         lifetime (RFC 4253 §7.2); stored in H[], NOT zeroed (it is a
 *         commitment to the handshake, and is not secret).
 */
typedef struct
{
    pc_bignum y; ///< Server ephemeral private DH scalar (SENSITIVE - wiped after KEX).
    pc_bignum f; ///< Server DH public value = g^y mod p (sent to client).
    pc_bignum K; ///< Shared DH secret = e^y mod p (SENSITIVE - wiped after key derivation).

    uint8_t H[32];       ///< SHA-256 exchange hash; doubles as session_id after first KEX.
    proto_bool kex_done; ///< True once NEWKEYS has been sent and received.
} SshDhState;

/** @brief Pool of ephemeral DH state, one entry per MAX_SSH_CONNS. */
extern SshDhState ssh_dh[MAX_SSH_CONNS];

// ---------------------------------------------------------------------------
// Wipe helpers
// ---------------------------------------------------------------------------

/** @brief Zero all key material for slot @p i on disconnect or KEX failure. */
static inline void ssh_keymat_wipe(uint8_t i)
{
    if (i < MAX_SSH_CONNS)
    {
        // A keyed GCM context owns a vendor allocation (mbedtls_gcm_setkey sets up a cipher context), so
        // zeroing the bytes would leak it once per closed connection. Release first, then wipe.
        if (ssh_keys[i].active && ssh_keys[i].cipher_mode == SSH_CIPHER_AES256GCM)
        {
            pc_aesgcm_key_wipe((struct pc_aesgcm_key *)(ssh_keys[i].gcm_ctx_c2s));
            pc_aesgcm_key_wipe((struct pc_aesgcm_key *)(ssh_keys[i].gcm_ctx_s2c));
        }
        pc_secure_wipe(&ssh_keys[i], sizeof(SshKeyMat));
    }
}

/** @brief Zero the ephemeral DH state for slot @p i after keys are derived. */
static inline void ssh_dh_wipe(uint8_t i)
{
    if (i < MAX_SSH_CONNS)
    {
        pc_secure_wipe(&ssh_dh[i], sizeof(SshDhState));
    }
}

#endif // PROTOCORE_SSH_KEYMAT_H
