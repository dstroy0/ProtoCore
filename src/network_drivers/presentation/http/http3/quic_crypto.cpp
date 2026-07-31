// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_quic_crypto.cpp
 * @brief QUIC packet protection and Initial secrets (see pc_quic_crypto.h).
 */

#include "network_drivers/presentation/http/http3/quic_crypto.h"

#if PC_ENABLE_HTTP3

#include "crypto/aead/aes128gcm.h"
#include "crypto/kdf/hkdf.h"
#include "network_drivers/presentation/http/http3/quic_packet.h"
#include "server/mmgr/secure.h" // the secure pool: header-protection key schedule
#include <string.h>

namespace
{
// RFC 9001 sec 5.2: the version-1 Initial salt.
const uint8_t INITIAL_SALT[20] = {0x38, 0x76, 0x2c, 0xf7, 0xf5, 0x59, 0x34, 0xb3, 0x4d, 0x17,
                                  0x9a, 0xe6, 0xa4, 0xc8, 0x0c, 0xad, 0xcc, 0xbb, 0x7f, 0x0a};

// RFC 9001 sec 5.8: the version-1 Retry integrity key and nonce.
const uint8_t RETRY_KEY[16] = {0xbe, 0x0c, 0x69, 0x0b, 0x9f, 0x66, 0x57, 0x5a,
                               0x1d, 0x76, 0x6b, 0x54, 0xe3, 0x68, 0xc8, 0x4e};
const uint8_t RETRY_NONCE[12] = {0x46, 0x15, 0x99, 0xd3, 0x5d, 0x63, 0x2b, 0xf2, 0x23, 0x98, 0x25, 0xbb};

// Build the AEAD nonce: the packet number, left-padded to the 12-byte IV width, XOR the IV.
void build_nonce(const uint8_t iv[12], uint64_t full_pn, uint8_t nonce[12])
{
    memcpy(nonce, iv, 12);
    for (int i = 0; i < 8; i++)
    {
        nonce[11 - i] ^= (uint8_t)(full_pn >> (8 * i));
    }
}
} // namespace

void pc_quic_keys_from_secret(const uint8_t secret[PC_HKDF_HASH_LEN], QuicPacketKeys *out)
{
    // RFC 9001 sec 5.1: every encryption level's packet keys are these three Expand-Labels of the
    // level's traffic secret (the Initial secrets below, or the TLS handshake / application secrets).
    // The key becomes a context here and the raw bytes are wiped: nothing downstream needs them.
    // The pool cannot come up short here: PC_SECURE_ARENA_SIZE is the SUM of every PC_WORK_*, a
    // strict upper bound rather than a deepest-nest estimate, so a borrow inside that budget always
    // succeeds. The borrow also wipes on release, on every exit path.
    SecureBorrow kb(PC_AES128GCM_KEY_LEN, 8);
    uint8_t *k = kb.span().buf;
    pc_hkdf_expand_label(secret, "quic key", k, PC_AES128GCM_KEY_LEN);
    pc_aes128gcm_key_init(out->gcm, k);
    pc_hkdf_expand_label(secret, "quic iv", out->iv, sizeof(out->iv));
    SecureBorrow hpb(PC_AES128GCM_KEY_LEN, 8);
    uint8_t *hpk = hpb.span().buf;
    pc_hkdf_expand_label(secret, "quic hp", hpk, PC_AES128GCM_KEY_LEN);
    pc_aes128_init(reinterpret_cast<pc_aes128 *>(out->hp), hpk);
}

void pc_quic_derive_initial_secrets(const uint8_t *dcid, size_t dcid_len, QuicInitialSecrets *out)
{
    uint8_t initial_secret[PC_HKDF_HASH_LEN];
    pc_hkdf_extract(INITIAL_SALT, sizeof(INITIAL_SALT), dcid, dcid_len, initial_secret);

    uint8_t client_secret[PC_HKDF_HASH_LEN];
    uint8_t server_secret[PC_HKDF_HASH_LEN];
    pc_hkdf_expand_label(initial_secret, "client in", client_secret, sizeof(client_secret));
    pc_hkdf_expand_label(initial_secret, "server in", server_secret, sizeof(server_secret));

    pc_quic_keys_from_secret(client_secret, &out->client);
    pc_quic_keys_from_secret(server_secret, &out->server);
}

size_t pc_quic_packet_protect(uint8_t *pkt, size_t cap, size_t pn_offset, uint8_t pn_len, uint64_t full_pn,
                              size_t payload_len, QuicPacketKeys &keys, bool is_long)
{
    if (pn_len < 1 || pn_len > 4)
    {
        return 0;
    }
    size_t hdr_len = pn_offset + pn_len;
    size_t total = hdr_len + payload_len + PC_AES128GCM_TAG_LEN;
    if (total > cap)
    {
        return 0;
    }

    // AEAD-seal the payload in place; associated data is the unprotected header.
    uint8_t nonce[12];
    build_nonce(keys.iv, full_pn, nonce);
    pc_aes128gcm_seal(reinterpret_cast<pc_aes128gcm_key *>(keys.gcm), nonce, pkt, hdr_len, pkt + hdr_len, payload_len,
                      pkt + hdr_len, pkt + hdr_len + payload_len);

    // Header protection (RFC 9001 sec 5.4): sample 16 bytes at pn_offset + 4 (always inside the
    // ciphertext because pn_len <= 4), AES-ECB it under hp, mask the low first-byte bits and the PN.
    // The header-protection context is already keyed and lives in the key material; rebuilding it here
    // costs ~556 cycles per packet plus a pool borrow and wipe. (The ECB block itself is ~7,842 - a
    // single HW-AES operation is expensive on this die, and that is the bigger target.)
    uint8_t mask[16];
    pc_aes128_encrypt_block(reinterpret_cast<pc_aes128 *>(keys.hp), pkt + pn_offset + 4, mask);

    pkt[0] ^= mask[0] & (is_long ? 0x0f : 0x1f);
    for (uint8_t i = 0; i < pn_len; i++)
    {
        pkt[pn_offset + i] ^= mask[1 + i];
    }

    return total;
}

size_t pc_quic_packet_unprotect(uint8_t *pkt, size_t pn_offset, size_t length, uint64_t largest_pn,
                                QuicPacketKeys &keys, bool is_long, uint8_t *out, uint64_t *out_pn)
{
    // Header protection needs a full 16-byte sample starting at pn_offset + 4, and the AEAD region
    // must carry at least the 16-byte tag once the (<=4-byte) packet number is removed.
    if (length < 4 + PC_AES128GCM_TAG_LEN)
    {
        return (size_t)-1;
    }

    // The header-protection context is already keyed and lives in the key material; building one here
    // would cost ~8,400 cycles to encrypt sixteen bytes.
    uint8_t mask[16];
    pc_aes128_encrypt_block(reinterpret_cast<pc_aes128 *>(keys.hp), pkt + pn_offset + 4, mask);

    pkt[0] ^= mask[0] & (is_long ? 0x0f : 0x1f);
    uint8_t pn_len = (uint8_t)((pkt[0] & 0x03) + 1);

    uint64_t truncated_pn = 0;
    for (uint8_t i = 0; i < pn_len; i++)
    {
        pkt[pn_offset + i] ^= mask[1 + i];
        truncated_pn = (truncated_pn << 8) | pkt[pn_offset + i];
    }
    uint64_t full_pn = pc_quic_pn_decode(largest_pn, truncated_pn, (uint8_t)(pn_len * 8));
    if (out_pn)
    {
        *out_pn = full_pn;
    }

    size_t hdr_len = pn_offset + pn_len;
    size_t ct_len = length - pn_len; // ciphertext + tag
    // A record too short to hold a tag is rejected HERE. The detached-tag api takes the ciphertext
    // length and the tag pointer separately, so it no longer has a combined length to range-check on
    // the caller's behalf - and ct_len comes off the wire, so the subtraction below would wrap to a
    // huge size_t rather than fail. This guard used to live inside pc_aes128gcm_open().
    if (ct_len < PC_AES128GCM_TAG_LEN)
    {
        return (size_t)-1;
    }
    uint8_t nonce[12];
    build_nonce(keys.iv, full_pn, nonce);
    const size_t pt_len = ct_len - PC_AES128GCM_TAG_LEN;
    if (!pc_aes128gcm_open(reinterpret_cast<pc_aes128gcm_key *>(keys.gcm), nonce, pkt, hdr_len, pkt + hdr_len, pt_len,
                           pkt + hdr_len + pt_len, out))
    {
        return (size_t)-1;
    }

    return ct_len - PC_AES128GCM_TAG_LEN;
}

void pc_quic_retry_integrity_tag(const uint8_t *odcid, size_t odcid_len, const uint8_t *retry, size_t retry_len,
                                 uint8_t tag[16])
{
    // AAD = Retry Pseudo-Packet: ODCID Length (1 byte) || ODCID || Retry packet (sans tag).
    // Assemble it into a scratch buffer; a Retry is small (short token), so a fixed cap suffices.
    uint8_t aad[1 + QUIC_MAX_CID_LEN + 256];
    size_t p = 0;
    aad[p++] = (uint8_t)odcid_len;
    if (odcid_len > QUIC_MAX_CID_LEN || 1 + odcid_len + retry_len > sizeof(aad))
    {
        memset(tag, 0, 16);
        return;
    }
    memcpy(aad + p, odcid, odcid_len);
    p += odcid_len;
    memcpy(aad + p, retry, retry_len);
    p += retry_len;

    // Empty plaintext: seal writes only the 16-byte tag.
    { // fixed RFC 9001 key, once per Retry packet - not worth a resident context
        SecureBorrow rk_b(PC_WORK_AES128GCM, 8);
        pc_aes128gcm_key *rk = pc_aes128gcm_key_init(rk_b.span().buf, RETRY_KEY);
        pc_aes128gcm_seal(rk, RETRY_NONCE, aad, p, nullptr, 0, tag, tag);
        pc_aes128gcm_key_wipe(rk);
    }
}

#endif // PC_ENABLE_HTTP3
