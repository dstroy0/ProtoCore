// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_dh.c
 * @brief DH-group14-SHA256 key exchange implementation.
 */

#include "network_drivers/presentation/ssh/transport/ssh_dh.h"
#include "crypto/mac/hmac_sha256.h"
#include "crypto/rng/rng.h" // pc_rand_fill: crypto owns the generator, this layer only draws
#include "mmgr/protomem.h"
#include "mmgr/secure.h"
#include "network_drivers/presentation/ssh/transport/ssh_packet.h" // ssh_pkt[]: the slot's KEX bytes
#include "network_drivers/tls/ssh_kexhash.h"

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
    pc_rand_fill((uint8_t *)dh->y.d, sizeof(pc_bignum));

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

// The caller's region, split by offset the way the slot's own borrow is: the exchange hash works out
// of the front, the K1 || K2 chain accumulates behind it. A caller hands in PC_SSH_KDF_BORROW bytes.
#define SSH_KDF_OFF_HASH 0u
#define SSH_KDF_OFF_ACC (SSH_KDF_OFF_HASH + PC_SHA512_BORROW)

// The library's own caller is the connection: every KDF here runs out of slot i's crypto_work.
static_assert(PC_CRYPTO_BORROW_MAX >= PC_SSH_KDF_BORROW,
              "a slot's crypto_work must cover the RFC 4253 sec 7.2 KDF: raise PC_CRYPTO_BORROW_MAX");

// RFC 4253 §7.2 key derivation extended to any length, over the KEX method's hash (SHA-256 or
// SHA-512 via SshKexHash / @p is512):
//   K1 = HASH(K || H || X || session_id)   (X = label byte); Ki+1 = HASH(K || H || K1..Ki)
//   key = K1 || K2 || ...   For the first KEX session_id == H; on a re-key it is the first KEX's H.
// @p h_len / @p sid_len are the exchange-hash / session-id lengths. When K is a hybrid string it is
// @p k_str_len octets (the KEX hash length). @p out_len up to SSH_KDF_MAX.
void ssh_kdf_derive(uint8_t *work, const uint8_t K_be[256], const uint8_t *H, const uint8_t *session_id, char label,
                    uint8_t *out, size_t out_len, proto_bool k_is_string, size_t h_len, size_t sid_len,
                    proto_bool is512)
{
    const size_t blk = ssh_kexhash_len(is512); // 32 or 64
    const size_t k_str_len = ssh_kexhash_len(is512);
    if (out_len > SSH_KDF_MAX)
    {
        out_len = SSH_KDF_MAX; // bounded: every negotiated algorithm needs <= 64 B today
    }
    uint8_t *acc = work + SSH_KDF_OFF_ACC; // K1 || K2 || ... accumulated for the chain hash
    size_t have = 0;

    SshKexHash h;
    ssh_kexhash_init(&h, work, is512);
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
    while (have < out_len && have + blk <= SSH_KDF_MAX)
    {
        ssh_kexhash_init(&h, work, is512);
        hash_K(&h, K_be, k_is_string, k_str_len);
        ssh_kexhash_update(&h, H, h_len);
        ssh_kexhash_update(&h, acc, have); // all prior blocks
        ssh_kexhash_final(&h, acc + have);
        have += blk;
    }
    mem.cpy(out, acc, out_len);
    pc_secure_wipe(acc, SSH_KDF_MAX); // the cipher key, the IV and both MAC keys pass through here
}

// The KEX values every direction's derivation shares, passed by pointer: this is the deepest call
// chain in the library, and these would otherwise ride the stack frame by frame.
typedef struct
{
    const uint8_t *K_be;
    const uint8_t *H;
    const uint8_t *session_id;
    size_t h_len;
    size_t sid_len;
    proto_bool k_is_string;
    proto_bool is512;
} SshKdfInputs;

// One derived value, straight into its destination. Slot @p i owns the working bytes: the KDF runs
// out of that connection's crypto_work, the region ssh_packet.h reserves for the sec 7.2 KDF.
static void kdf_into(uint8_t i, const SshKdfInputs *in, char label, uint8_t *out, size_t out_len)
{
    ssh_kdf_derive(ssh_pkt[i].crypto_work, in->K_be, in->H, in->session_id, label, out, out_len, in->k_is_string,
                   in->h_len, in->sid_len, in->is512);
}

// Install one direction's key material into the connection's own keymat. RFC 4253 sec 7.2 fixes the
// labels by direction - client to server takes 'A' (IV), 'C' (key) and 'E' (MAC), server to client
// takes 'B', 'D' and 'F' - and that direction's negotiated cipher decides which of them it needs.
// Every value is derived into the slot that owns it, so none of it is staged anywhere else.
static void install_direction(uint8_t i, const SshKdfInputs *in, proto_bool c2s, uint8_t cipher_alg, uint8_t mac_alg)
{
    SshKeyMat *km = &ssh_keys[i];
    char iv_label = 'B';
    char key_label = 'D';
    char mac_label = 'F';
    uint8_t *chacha_key = km->chacha_key_s2c;
    uint8_t *gcm_ctx = km->gcm_ctx_s2c;
    uint8_t *aes_key = km->aes_key_s2c;
    uint8_t *aes_iv = km->aes_iv_s2c;
    uint8_t *mac_key = km->mac_key_s2c;
    if (c2s)
    {
        iv_label = 'A';
        key_label = 'C';
        mac_label = 'E';
        chacha_key = km->chacha_key_c2s;
        gcm_ctx = km->gcm_ctx_c2s;
        aes_key = km->aes_key_c2s;
        aes_iv = km->aes_iv_c2s;
        mac_key = km->mac_key_c2s;
    }

    if (cipher_alg == SSH_CIPHER_CHACHA20POLY1305)
    {
        // A 512-bit key (K_main || K_header) from the sec 7.2 extension chain; no IV, no MAC key.
        kdf_into(i, in, key_label, chacha_key, PC_CHACHAPOLY_KEY_LEN);
        return;
    }

    // The IV field takes the leading bytes of the derived stream, which is what the KDF copies.
    kdf_into(i, in, iv_label, aes_iv, PC_AES256CTR_CTR_LEN);

    if (cipher_alg == SSH_CIPHER_AES256GCM)
    {
        // RFC 5647: this mode keeps only the schedule, so the key lands in aes_key - which GCM does not
        // otherwise use - becomes the keyed context, and is wiped. The nonce is the low 12 IV bytes.
        kdf_into(i, in, key_label, aes_key, PC_AES256CTR_KEY_LEN);
        pc_aesgcm_key_init(gcm_ctx, aes_key);
        pc_secure_wipe(aes_key, PC_AES256CTR_KEY_LEN);
        return;
    }

    // aes256-ctr keeps the raw key and the running counter; the schedule is rebuilt per packet in the
    // shared crypto scratch. It is the only cipher that also needs a separate MAC key.
    kdf_into(i, in, key_label, aes_key, PC_AES256CTR_KEY_LEN);
    kdf_into(i, in, mac_label, mac_key, ssh_mac_len(mac_alg));
}

void ssh_dh_derive_keys_sid(uint8_t i, const uint8_t K_be[256], const uint8_t *H, const uint8_t *session_id,
                            uint8_t cipher_alg_c2s, uint8_t cipher_alg_s2c, uint8_t mac_alg_c2s, uint8_t mac_alg_s2c,
                            proto_bool k_is_string, size_t h_len, size_t sid_len, proto_bool is512)
{
    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    // The exchange hash and this KDF work out of the slot's own bytes, the same borrow the wire and
    // the packet MAC come from.
    if (!ssh_pkt_slot_storage(&ssh_pkt[i]))
    {
        return;
    }
    SshKeyMat *km = &ssh_keys[i];
    // Rekey lands here with live contexts still in the slot. Release each before its mode is
    // overwritten, after which the outgoing mode is no longer knowable.
    if (km->active && km->cipher_mode_c2s == SSH_CIPHER_AES256GCM)
    {
        pc_aesgcm_key_wipe((struct pc_aesgcm_key *)(km->gcm_ctx_c2s));
    }
    if (km->active && km->cipher_mode_s2c == SSH_CIPHER_AES256GCM)
    {
        pc_aesgcm_key_wipe((struct pc_aesgcm_key *)(km->gcm_ctx_s2c));
    }
    km->cipher_mode_c2s = cipher_alg_c2s;
    km->cipher_mode_s2c = cipher_alg_s2c;
    km->mac_mode_c2s = mac_alg_c2s;
    km->mac_mode_s2c = mac_alg_s2c;

    const SshKdfInputs in = {.K_be = K_be,
                             .H = H,
                             .session_id = session_id,
                             .h_len = h_len,
                             .sid_len = sid_len,
                             .k_is_string = k_is_string,
                             .is512 = is512};
    install_direction(i, &in, PROTO_TRUE, cipher_alg_c2s, mac_alg_c2s);
    install_direction(i, &in, PROTO_FALSE, cipher_alg_s2c, mac_alg_s2c);
    km->active = PROTO_TRUE;
}


void ssh_dh_derive_keys(uint8_t i, const uint8_t K_be[256], const uint8_t H[PC_SHA256_DIGEST_LEN])
{
    // First-KEX convenience: session id equals H; aes256-ctr + hmac-sha2-256 (pre-negotiation defaults),
    // SHA-256 exchange hash (h_len / sid_len / is512 default).
    ssh_dh_derive_keys_sid(i, K_be, H, H, SSH_CIPHER_AES256CTR, SSH_CIPHER_AES256CTR, SSH_MAC_HMAC_SHA256,
                           SSH_MAC_HMAC_SHA256, PROTO_FALSE, PC_SHA256_DIGEST_LEN, PC_SHA256_DIGEST_LEN, PROTO_FALSE);
}
