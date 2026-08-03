// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_dh.c
 * @brief DH-group14-SHA256 key exchange implementation.
 */

#include "network_drivers/presentation/ssh/transport/ssh_dh.h"
#include "crypto/mac/hmac_sha256.h"
#include "mmgr/secure.h"
#include "network_drivers/tls/ssh_kexhash.h"
#include <Arduino.h> // for pc_platform_rand_u32() / pc_platform_rand_fill() (real or mock)
#include <string.h>

// ---------------------------------------------------------------------------
// RNG
// ---------------------------------------------------------------------------

void ssh_rng_fill(uint8_t *buf, size_t len)
{
    pc_platform_rand_fill(buf, len);
}

// ---------------------------------------------------------------------------
// DH key generation
// ---------------------------------------------------------------------------

int ssh_dh_generate(uint8_t i)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
    SshDhState *dh = &ssh_dh[i];

    // Generate a random 2048-bit private scalar y.
    // RFC 4253 §8 does not specify a minimum bit-length for y beyond requiring
    // it to be in [1, p-1].  Common practice is a full 2048-bit random value,
    // which ensures the discrete-log is as hard as the group order.
    ssh_rng_fill((uint8_t *)dh->y.d, sizeof(pc_bignum));

    // Ensure y < p by clearing the two MSBs (conservative; not strictly
    // required since rejection sampling would also work, but a single mask
    // is sufficient because p ≈ 2^2048 and clearing 2 bits keeps y in range).
    dh->y.d[PC_BN_LIMBS - 1] &= 0x3FFFFFFFu;
    // Also ensure y > 1 (set bit 1 of the LSB limb to avoid pathological y=0,1).
    dh->y.d[0] |= 0x00000002u;

    // f = g^y mod p  (g = 2 for group-14)
    bn_expmod_group14(&dh->f, &group14_g, &dh->y);

    dh->kex_done = PROTO_FALSE;
    return 0;
}

// ---------------------------------------------------------------------------
// Session key derivation (RFC 4253 §7.2)
// ---------------------------------------------------------------------------

// Hash the shared secret K as an SSH mpint into @p ctx (RFC 4251 §5 / RFC 4253
// §7.2): big-endian, 4-byte length prefix, a leading 0x00 only if the MSB is set,
// and all UNNECESSARY leading 0x00 bytes stripped (canonical form). The exchange
// hash encodes K the same way (hash_mpint), so the KDF must too: if K has any
// high-order zero bytes (~1/256 of handshakes) a spec-compliant peer strips them
// and would otherwise derive different keys.
static void hash_mpint_K(SshKexHash *h, const uint8_t K_be[256])
{
    size_t off = 0;
    while (off < 256 && K_be[off] == 0x00u)
    {
        off++;
    }
    if (off == 256) // K == 0: empty mpint (not reachable for a real DH secret)
    {
        uint8_t len_be[4] = {0, 0, 0, 0};
        ssh_kexhash_update(h, len_be, 4);
        return;
    }
    proto_bool pad = (K_be[off] & 0x80u) != 0;
    uint32_t mlen = (uint32_t)(256 - off) + (pad ? 1u : 0u);
    uint8_t len_be[4] = {(uint8_t)(mlen >> 24), (uint8_t)(mlen >> 16), (uint8_t)(mlen >> 8), (uint8_t)mlen};
    ssh_kexhash_update(h, len_be, 4);
    if (pad)
    {
        uint8_t zero = 0x00u;
        ssh_kexhash_update(h, &zero, 1);
    }
    ssh_kexhash_update(h, K_be + off, 256 - off);
}

// Hybrid KEX: K is a fixed HASH output (32 for mlkem-sha256, 64 for sntrup761-sha512), hashed as a
// plain SSH string (RFC 4251 §5) - length prefix then the bytes verbatim, NO mpint sign/strip. It
// lives in the last @p klen octets of the right-aligned K_be buffer. H and this KDF encode K the same.
static void hash_string_K(SshKexHash *h, const uint8_t K_be[256], size_t klen)
{
    uint8_t len_be[4] = {(uint8_t)(klen >> 24), (uint8_t)(klen >> 16), (uint8_t)(klen >> 8), (uint8_t)klen};
    ssh_kexhash_update(h, len_be, 4);
    ssh_kexhash_update(h, K_be + (256 - klen), klen);
}

static inline void hash_K(SshKexHash *h, const uint8_t K_be[256], proto_bool k_is_string, size_t k_str_len)
{
    if (k_is_string)
    {
        hash_string_K(h, K_be, k_str_len);
    }
    else
    {
        hash_mpint_K(h, K_be);
    }
}

// RFC 4253 §7.2 key derivation extended to any length, over the KEX method's hash (SHA-256 or
// SHA-512 via SshKexHash / @p is512):
//   K1 = HASH(K || H || X || session_id)   (X = label byte); Ki+1 = HASH(K || H || K1..Ki)
//   key = K1 || K2 || ...   For the first KEX session_id == H; on a re-key it is the first KEX's H.
// @p h_len / @p sid_len are the exchange-hash / session-id lengths. When K is a hybrid string it is
// @p k_str_len octets (the KEX hash length). @p out_len up to SSH_KDF_MAX.
void ssh_kdf_derive(const uint8_t K_be[256], const uint8_t *H, const uint8_t *session_id, char label, uint8_t *out,
                    size_t out_len, proto_bool k_is_string, size_t h_len, size_t sid_len, proto_bool is512)
{
    const size_t blk = ssh_kexhash_len(is512); // 32 or 64
    const size_t k_str_len = ssh_kexhash_len(is512);
    if (out_len > SSH_KDF_MAX)
    {
        out_len = SSH_KDF_MAX; // bounded: every negotiated algorithm needs <= 64 B today
    }
    uint8_t acc[SSH_KDF_MAX]; // K1 || K2 || ... accumulated for the chain hash
    size_t have = 0;

    SshKexHash h;
    ssh_kexhash_init(&h, is512);
    hash_K(&h, K_be, k_is_string, k_str_len);
    ssh_kexhash_update(&h, H, h_len);
    uint8_t lbl = (uint8_t)label;
    ssh_kexhash_update(&h, &lbl, 1);
    ssh_kexhash_update(&h, session_id, sid_len);
    ssh_kexhash_final(&h, acc); // acc[0..blk-1] = K1
    have = blk;

    // have + blk > SSH_KDF_MAX (loop exit via the right operand) is unreachable: blk is only ever 32 or
    // 64 (ssh_kexhash_len(is512)) and SSH_KDF_MAX is 128, an exact multiple of both; out_len is already
    // clamped to <= SSH_KDF_MAX above, and have only grows in whole increments of blk starting at blk -
    // so whenever have < out_len (<= SSH_KDF_MAX) it is at most SSH_KDF_MAX - blk, and this half is
    // always true.
    while (have < out_len &&
           have + blk <= SSH_KDF_MAX) // GCOVR_EXCL_BR_LINE  have+blk never exceeds SSH_KDF_MAX, see above
    {
        ssh_kexhash_init(&h, is512);
        hash_K(&h, K_be, k_is_string, k_str_len);
        ssh_kexhash_update(&h, H, h_len);
        ssh_kexhash_update(&h, acc, have); // all prior blocks
        ssh_kexhash_final(&h, acc + have);
        have += blk;
    }
    memcpy(out, acc, out_len);
}

// One 32-byte derived value (the only size any negotiated cipher key/IV needs today).
static void derive_key(const uint8_t K_be[256], const uint8_t *H, const uint8_t *session_id, char label,
                       uint8_t out[PC_SHA256_DIGEST_LEN], proto_bool k_is_string, size_t h_len, size_t sid_len,
                       proto_bool is512)
{
    ssh_kdf_derive(K_be, H, session_id, label, out, PC_SHA256_DIGEST_LEN, k_is_string, h_len, sid_len, is512);
}

void ssh_dh_derive_keys_sid(uint8_t i, const uint8_t K_be[256], const uint8_t *H, const uint8_t *session_id,
                            uint8_t cipher_alg, uint8_t mac_alg, proto_bool k_is_string, size_t h_len, size_t sid_len,
                            proto_bool is512)
{
    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    SshKeyMat *km = &ssh_keys[i];
    // Rekey lands here with live contexts still in the slot. Release them before cipher_mode is
    // overwritten, after which the outgoing mode is no longer knowable.
    if (km->active && km->cipher_mode == SSH_CIPHER_AES256GCM)
    {
        pc_aesgcm_key_wipe((struct pc_aesgcm_key *)(km->gcm_ctx_c2s));
        pc_aesgcm_key_wipe((struct pc_aesgcm_key *)(km->gcm_ctx_s2c));
    }
    km->cipher_mode = cipher_alg;
    km->mac_mode = mac_alg;

    if (cipher_alg == SSH_CIPHER_CHACHA20POLY1305)
    {
        // chacha20-poly1305@openssh.com: a 512-bit key per direction (labels 'C'/'D'), no IV and
        // no separate MAC key (the AEAD authenticates). The 64 bytes come from the RFC 4253 §7.2
        // extension chain (K1 || K2).
        ssh_kdf_derive(K_be, H, session_id, 'C', km->chacha_key_c2s, PC_CHACHAPOLY_KEY_LEN, k_is_string, h_len, sid_len,
                       is512);
        ssh_kdf_derive(K_be, H, session_id, 'D', km->chacha_key_s2c, PC_CHACHAPOLY_KEY_LEN, k_is_string, h_len, sid_len,
                       is512);
        km->active = PROTO_TRUE;
        return;
    }

    if (cipher_alg == SSH_CIPHER_AES256GCM)
    {
        // aes256-gcm@openssh.com (RFC 5647): a 256-bit key (labels 'C'/'D') and a 96-bit initial IV
        // (the first 12 bytes of the 'A'/'B' IV material) per direction; no separate MAC key (AEAD).
        uint8_t iv_c2s[PC_SHA256_DIGEST_LEN];
        uint8_t iv_s2c[PC_SHA256_DIGEST_LEN];
        uint8_t key_c2s[32];
        uint8_t key_s2c[32];
        derive_key(K_be, H, session_id, 'A', iv_c2s, k_is_string, h_len, sid_len, is512);  // IV  C→S (first 12 used)
        derive_key(K_be, H, session_id, 'B', iv_s2c, k_is_string, h_len, sid_len, is512);  // IV  S→C
        derive_key(K_be, H, session_id, 'C', key_c2s, k_is_string, h_len, sid_len, is512); // key C→S
        derive_key(K_be, H, session_id, 'D', key_s2c, k_is_string, h_len, sid_len, is512); // key S→C
        // Build the keyed contexts now and keep the nonce (GCM nonce = low 12 bytes of the 16-byte IV
        // field). The raw keys are wiped below and never stored: the context holds the schedule, so unlike
        // CTR mode this slot ends up with no raw GCM key in it at all.
        pc_aesgcm_key_init(km->gcm_ctx_c2s, key_c2s);
        pc_aesgcm_key_init(km->gcm_ctx_s2c, key_s2c);
        memcpy(km->aes_iv_c2s, iv_c2s, sizeof(km->aes_iv_c2s));
        memcpy(km->aes_iv_s2c, iv_s2c, sizeof(km->aes_iv_s2c));
        pc_secure_wipe(key_c2s, sizeof(key_c2s));
        pc_secure_wipe(key_s2c, sizeof(key_s2c));
        pc_secure_wipe(iv_c2s, sizeof(iv_c2s));
        pc_secure_wipe(iv_s2c, sizeof(iv_s2c));
        km->active = PROTO_TRUE;
        return;
    }

    // aes256-ctr + HMAC-SHA2-256: RFC 4253 §7.2 derives six values, each keyed by a label 'A'..'F'.
    // The AES contexts need both key and IV at init time, so derive all six values first.
    uint8_t iv_c2s[PC_SHA256_DIGEST_LEN];
    uint8_t iv_s2c[PC_SHA256_DIGEST_LEN];
    uint8_t key_c2s[32];
    uint8_t key_s2c[32];

    derive_key(K_be, H, session_id, 'A', iv_c2s, k_is_string, h_len, sid_len, is512);  // IV  C→S (first 16 used)
    derive_key(K_be, H, session_id, 'B', iv_s2c, k_is_string, h_len, sid_len, is512);  // IV  S→C
    derive_key(K_be, H, session_id, 'C', key_c2s, k_is_string, h_len, sid_len, is512); // cipher key C→S
    derive_key(K_be, H, session_id, 'D', key_s2c, k_is_string, h_len, sid_len, is512); // cipher key S→C
    uint8_t mlen = ssh_mac_len(mac_alg);                                               // 32 (SHA-256) or 64 (SHA-512)
    ssh_kdf_derive(K_be, H, session_id, 'E', km->mac_key_c2s, mlen, k_is_string, h_len, sid_len, is512); // MAC C→S
    ssh_kdf_derive(K_be, H, session_id, 'F', km->mac_key_s2c, mlen, k_is_string, h_len, sid_len, is512); // MAC S→C

    // Store only the raw key + the initial counter (first 16 bytes of the derived IV); the key schedule is
    // rebuilt per packet in the shared crypto scratch, so no expanded key ever lands in the keymat pool.
    memcpy(km->aes_key_c2s, key_c2s, sizeof(km->aes_key_c2s));
    memcpy(km->aes_key_s2c, key_s2c, sizeof(km->aes_key_s2c));
    memcpy(km->aes_iv_c2s, iv_c2s, sizeof(km->aes_iv_c2s));
    memcpy(km->aes_iv_s2c, iv_s2c, sizeof(km->aes_iv_s2c));

    // Wipe stack temporaries (key material).
    pc_secure_wipe(key_c2s, sizeof(key_c2s));
    pc_secure_wipe(key_s2c, sizeof(key_s2c));
    pc_secure_wipe(iv_c2s, sizeof(iv_c2s));
    pc_secure_wipe(iv_s2c, sizeof(iv_s2c));

    km->active = PROTO_TRUE;
}

void ssh_dh_derive_keys(uint8_t i, const uint8_t K_be[256], const uint8_t H[PC_SHA256_DIGEST_LEN])
{
    // First-KEX convenience: session id equals H; aes256-ctr + hmac-sha2-256 (pre-negotiation defaults),
    // SHA-256 exchange hash (h_len / sid_len / is512 default).
    ssh_dh_derive_keys_sid(i, K_be, H, H, SSH_CIPHER_AES256CTR, SSH_MAC_HMAC_SHA256, PROTO_FALSE, PC_SHA256_DIGEST_LEN,
                           PC_SHA256_DIGEST_LEN, PROTO_FALSE);
}
