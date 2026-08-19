// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file quic_crypto.c
 * @brief QUIC packet protection and Initial secrets (see quic_crypto.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HTTP3

static uint8_t quic_packet_work[16]; // the borrow an entry takes; QuicPacket never reads it

#include "mmgr/protomem/protomem.h"
#include "network_drivers/presentation/http/http3/quic_crypto/quic_crypto.h"

#include "crypto/aead/aes128gcm/aes128gcm.h"
#include "crypto/kdf/hkdf/hkdf.h"
#include "mmgr/secure/secure.h" // the secure pool: header-protection key schedule
#include "network_drivers/presentation/http/http3/quic_packet/quic_packet.h"

PROTOCORE_BEGIN_DECLS

// RFC 9001 sec 5.2: the version-1 Initial salt.
static const uint8_t INITIAL_SALT[20] = {0x38, 0x76, 0x2c, 0xf7, 0xf5, 0x59, 0x34, 0xb3, 0x4d, 0x17,
                                         0x9a, 0xe6, 0xa4, 0xc8, 0x0c, 0xad, 0xcc, 0xbb, 0x7f, 0x0a};

// RFC 9001 sec 5.8: the version-1 Retry integrity key and nonce.
static const uint8_t RETRY_KEY[16] = {0xbe, 0x0c, 0x69, 0x0b, 0x9f, 0x66, 0x57, 0x5a,
                                      0x1d, 0x76, 0x6b, 0x54, 0xe3, 0x68, 0xc8, 0x4e};
static const uint8_t RETRY_NONCE[12] = {0x46, 0x15, 0x99, 0xd3, 0x5d, 0x63, 0x2b, 0xf2, 0x23, 0x98, 0x25, 0xbb};

// Build the AEAD nonce: the packet number, left-padded to the 12-byte IV width, XOR the IV.
static void build_nonce(const uint8_t iv[12], uint64_t full_pn, uint8_t nonce[12])
{
    mem.cpy(nonce, iv, 12);
    for (int i = 0; i < 8; i++)
    {
        nonce[11 - i] ^= (uint8_t)(full_pn >> (8 * i));
    }
}

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void quic_crypto_keys_from_secret(uint8_t *restrict work);

static void quic_crypto_keys_from_secret(uint8_t *restrict work)
{
    (void)work;
    uint8_t *keys_work = QuicCrypto.keys_from_secret_args.keys_work;
    const uint8_t *secret = QuicCrypto.keys_from_secret_args.secret;
    QuicPacketKeys *out = QuicCrypto.keys_from_secret_args.out;

    // RFC 9001 sec 5.1: every encryption level's packet keys are these three Expand-Labels of the
    // level's traffic secret (the Initial secrets below, or the TLS handshake / application secrets).
    // The key becomes a context here and the raw bytes are wiped: nothing downstream needs them.
    uint8_t *k = keys_work + PROTOCORE_HKDF_BORROW;
    uint8_t *hpk = k + PROTOCORE_AES128GCM_KEY_LEN;
    Hkdf.expand_label_args.secret = secret;
    Hkdf.expand_label_args.label = "quic key";
    Hkdf.expand_label_args.out = k;
    Hkdf.expand_label_args.out_len = PROTOCORE_AES128GCM_KEY_LEN;
    Hkdf.expand_label_args.label_prefix = PROTOCORE_HKDF_LABEL_PREFIX;
    Hkdf.expand_label(keys_work);
    Aes128Gcm.key_args.key = k;
    Aes128Gcm.key_init(out->gcm);
    Hkdf.expand_label_args.secret = secret;
    Hkdf.expand_label_args.label = "quic iv";
    Hkdf.expand_label_args.out = out->iv;
    Hkdf.expand_label_args.out_len = sizeof(out->iv);
    Hkdf.expand_label_args.label_prefix = PROTOCORE_HKDF_LABEL_PREFIX;
    Hkdf.expand_label(keys_work);
    Hkdf.expand_label_args.secret = secret;
    Hkdf.expand_label_args.label = "quic hp";
    Hkdf.expand_label_args.out = hpk;
    Hkdf.expand_label_args.out_len = PROTOCORE_AES128GCM_KEY_LEN;
    Hkdf.expand_label_args.label_prefix = PROTOCORE_HKDF_LABEL_PREFIX;
    Hkdf.expand_label(keys_work);
    Aes128Gcm.block_key_args.key = hpk;
    Aes128Gcm.block_init(out->gcm);
    protocore_secure_wipe(k, 2 * PROTOCORE_AES128GCM_KEY_LEN);
}

static void quic_crypto_derive_initial_secrets(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    uint8_t *keys_work = QuicCrypto.derive_initial_secrets_args.keys_work;
    const uint8_t *dcid = QuicCrypto.derive_initial_secrets_args.dcid;
    size_t dcid_len = QuicCrypto.derive_initial_secrets_args.dcid_len;
    QuicInitialSecrets *out = QuicCrypto.derive_initial_secrets_args.out;

    uint8_t initial_secret[PROTOCORE_HKDF_HASH_LEN];
    Hkdf.extract_args.salt = INITIAL_SALT;
    Hkdf.extract_args.salt_len = sizeof(INITIAL_SALT);
    Hkdf.extract_args.ikm = dcid;
    Hkdf.extract_args.ikm_len = dcid_len;
    Hkdf.extract_args.prk = initial_secret;
    Hkdf.extract(keys_work);

    uint8_t client_secret[PROTOCORE_HKDF_HASH_LEN];
    uint8_t server_secret[PROTOCORE_HKDF_HASH_LEN];
    Hkdf.expand_label_args.secret = initial_secret;
    Hkdf.expand_label_args.label = "client in";
    Hkdf.expand_label_args.out = client_secret;
    Hkdf.expand_label_args.out_len = sizeof(client_secret);
    Hkdf.expand_label_args.label_prefix = PROTOCORE_HKDF_LABEL_PREFIX;
    Hkdf.expand_label(keys_work);
    Hkdf.expand_label_args.secret = initial_secret;
    Hkdf.expand_label_args.label = "server in";
    Hkdf.expand_label_args.out = server_secret;
    Hkdf.expand_label_args.out_len = sizeof(server_secret);
    Hkdf.expand_label_args.label_prefix = PROTOCORE_HKDF_LABEL_PREFIX;
    Hkdf.expand_label(keys_work);

    QuicCrypto.keys_from_secret_args.keys_work = keys_work;
    QuicCrypto.keys_from_secret_args.secret = client_secret;
    QuicCrypto.keys_from_secret_args.out = &out->client;
    quic_crypto_keys_from_secret(work);
    QuicCrypto.keys_from_secret_args.keys_work = keys_work;
    QuicCrypto.keys_from_secret_args.secret = server_secret;
    QuicCrypto.keys_from_secret_args.out = &out->server;
    quic_crypto_keys_from_secret(work);
}

static void quic_crypto_packet_protect(uint8_t *restrict work)
{
    (void)work;
    uint8_t *pkt = QuicCrypto.packet_protect_args.pkt;
    size_t cap = QuicCrypto.packet_protect_args.cap;
    size_t pn_offset = QuicCrypto.packet_protect_args.pn_offset;
    uint8_t pn_len = QuicCrypto.packet_protect_args.pn_len;
    uint64_t full_pn = QuicCrypto.packet_protect_args.full_pn;
    size_t payload_len = QuicCrypto.packet_protect_args.payload_len;
    QuicPacketKeys *keys = QuicCrypto.packet_protect_args.keys;
    proto_bool is_long = QuicCrypto.packet_protect_args.is_long;

    if (pn_len < 1 || pn_len > 4)
    {
        QuicCrypto.n = 0;
        return;
    }
    size_t hdr_len = pn_offset + pn_len;
    size_t total = hdr_len + payload_len + PROTOCORE_AES128GCM_TAG_LEN;
    if (total > cap)
    {
        QuicCrypto.n = 0;
        return;
    }

    // AEAD-seal the payload in place; associated data is the unprotected header.
    uint8_t nonce[12];
    build_nonce(keys->iv, full_pn, nonce);
    Aes128Gcm.seal_args.nonce = nonce;
    Aes128Gcm.seal_args.aad = pkt;
    Aes128Gcm.seal_args.aad_len = hdr_len;
    Aes128Gcm.seal_args.pt = pkt + hdr_len;
    Aes128Gcm.seal_args.pt_len = payload_len;
    Aes128Gcm.seal_args.ct_out = pkt + hdr_len;
    Aes128Gcm.seal_args.tag_out = pkt + hdr_len + payload_len;
    Aes128Gcm.seal(keys->gcm);

    // Header protection (RFC 9001 sec 5.4): sample 16 bytes at pn_offset + 4 (always inside the
    // ciphertext because pn_len <= 4), AES-ECB it under hp, mask the low first-byte bits and the PN.
    // The header-protection context is already keyed and lives in the key material; rebuilding it here
    // costs ~556 cycles per packet plus a pool borrow and wipe. (The ECB block itself is ~7,842 - a
    // single HW-AES operation is expensive on this die, and that is the bigger target.)
    uint8_t mask[16];
    Aes128Gcm.block_args.in = pkt + pn_offset + 4;
    Aes128Gcm.block_args.out = mask;
    Aes128Gcm.block_encrypt(keys->gcm);

    pkt[0] ^= mask[0] & (is_long ? 0x0f : 0x1f);
    for (uint8_t i = 0; i < pn_len; i++)
    {
        pkt[pn_offset + i] ^= mask[1 + i];
    }

    QuicCrypto.n = total;
}

static void quic_crypto_packet_unprotect(uint8_t *restrict work)
{
    (void)work;
    uint8_t *pkt = QuicCrypto.packet_unprotect_args.pkt;
    size_t pn_offset = QuicCrypto.packet_unprotect_args.pn_offset;
    size_t length = QuicCrypto.packet_unprotect_args.length;
    uint64_t largest_pn = QuicCrypto.packet_unprotect_args.largest_pn;
    QuicPacketKeys *keys = QuicCrypto.packet_unprotect_args.keys;
    proto_bool is_long = QuicCrypto.packet_unprotect_args.is_long;
    uint8_t *out = QuicCrypto.packet_unprotect_args.out;
    uint64_t *out_pn = QuicCrypto.packet_unprotect_args.out_pn;

    // Header protection needs a full 16-byte sample starting at pn_offset + 4, and the AEAD region
    // must carry at least the 16-byte tag once the (<=4-byte) packet number is removed.
    if (length < 4 + PROTOCORE_AES128GCM_TAG_LEN)
    {
        QuicCrypto.n = (size_t)-1;
        return;
    }

    // The header-protection context is already keyed and lives in the key material; building one here
    // would cost ~8,400 cycles to encrypt sixteen bytes.
    uint8_t mask[16];
    Aes128Gcm.block_args.in = pkt + pn_offset + 4;
    Aes128Gcm.block_args.out = mask;
    Aes128Gcm.block_encrypt(keys->gcm);

    pkt[0] ^= mask[0] & (is_long ? 0x0f : 0x1f);
    uint8_t pn_len = (uint8_t)((pkt[0] & 0x03) + 1);

    uint64_t truncated_pn = 0;
    for (uint8_t i = 0; i < pn_len; i++)
    {
        pkt[pn_offset + i] ^= mask[1 + i];
        truncated_pn = (truncated_pn << 8) | pkt[pn_offset + i];
    }
    QuicPacket.pn_decode_args.largest_pn = largest_pn;
    QuicPacket.pn_decode_args.truncated_pn = truncated_pn;
    QuicPacket.pn_decode_args.pn_nbits = (uint8_t)(pn_len * 8);
    QuicPacket.pn_decode(quic_packet_work);
    uint64_t full_pn = QuicPacket.u64;
    if (out_pn)
    {
        *out_pn = full_pn;
    }

    size_t hdr_len = pn_offset + pn_len;
    size_t ct_len = length - pn_len; // ciphertext + tag
    // A record too short to hold a tag is rejected HERE. The detached-tag api takes the ciphertext
    // length and the tag pointer separately, so it no longer has a combined length to range-check on
    // the caller's behalf - and ct_len comes off the wire, so the subtraction below would wrap to a
    // huge size_t rather than fail. This guard used to live inside the AEAD open itself.
    if (ct_len < PROTOCORE_AES128GCM_TAG_LEN)
    {
        QuicCrypto.n = (size_t)-1;
        return;
    }
    uint8_t nonce[12];
    build_nonce(keys->iv, full_pn, nonce);
    const size_t pt_len = ct_len - PROTOCORE_AES128GCM_TAG_LEN;
    Aes128Gcm.open_args.nonce = nonce;
    Aes128Gcm.open_args.aad = pkt;
    Aes128Gcm.open_args.aad_len = hdr_len;
    Aes128Gcm.open_args.ct = pkt + hdr_len;
    Aes128Gcm.open_args.ct_len = pt_len;
    Aes128Gcm.open_args.tag = pkt + hdr_len + pt_len;
    Aes128Gcm.open_args.out = out;
    Aes128Gcm.open(keys->gcm);
    if (!Aes128Gcm.ok)
    {
        QuicCrypto.n = (size_t)-1;
        return;
    }

    // On success pkt[0] holds the unprotected first byte, which is where the caller reads the
    // RFC 9000 sec 17.2 / 17.3.1 Reserved Bits: this is the only point at which both protections
    // are off, and only the connection can answer a violation with a CONNECTION_CLOSE.
    QuicCrypto.n = ct_len - PROTOCORE_AES128GCM_TAG_LEN;
}

static void quic_crypto_retry_integrity_tag(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *odcid = QuicCrypto.retry_integrity_tag_args.odcid;
    size_t odcid_len = QuicCrypto.retry_integrity_tag_args.odcid_len;
    const uint8_t *retry = QuicCrypto.retry_integrity_tag_args.retry;
    size_t retry_len = QuicCrypto.retry_integrity_tag_args.retry_len;
    uint8_t *tag = QuicCrypto.retry_integrity_tag_args.tag;

    // AAD = Retry Pseudo-Packet: ODCID Length (1 byte) || ODCID || Retry packet (sans tag).
    // Assemble it into a scratch buffer; a Retry is small (short token), so a fixed cap suffices.
    uint8_t aad[1 + QUIC_MAX_CID_LEN + 256];
    size_t p = 0;
    aad[p++] = (uint8_t)odcid_len;
    if (odcid_len > QUIC_MAX_CID_LEN || 1 + odcid_len + retry_len > sizeof(aad))
    {
        mem.set(tag, 0, 16);
        return;
    }
    mem.cpy(aad + p, odcid, odcid_len);
    p += odcid_len;
    mem.cpy(aad + p, retry, retry_len);
    p += retry_len;

    // Empty plaintext: seal writes only the 16-byte tag.
    { // fixed RFC 9001 key, once per Retry packet - not worth a resident context
        size_t mark = protocore_secure_mark();
        uint8_t *rk = protocore_secure_alloc(PROTOCORE_AES128GCM_BORROW, 8);
        Aes128Gcm.key_args.key = RETRY_KEY;
        Aes128Gcm.key_init(rk);
        Aes128Gcm.seal_args.nonce = RETRY_NONCE;
        Aes128Gcm.seal_args.aad = aad;
        Aes128Gcm.seal_args.aad_len = p;
        Aes128Gcm.seal_args.pt = NULL;
        Aes128Gcm.seal_args.pt_len = 0;
        Aes128Gcm.seal_args.ct_out = tag;
        Aes128Gcm.seal_args.tag_out = tag;
        Aes128Gcm.seal(rk);
        Aes128Gcm.key_wipe(rk);
        protocore_secure_release(mark);
    }
}

QuicCryptoNs QuicCrypto = {
    .derive_initial_secrets = quic_crypto_derive_initial_secrets,
    .keys_from_secret = quic_crypto_keys_from_secret,
    .packet_protect = quic_crypto_packet_protect,
    .packet_unprotect = quic_crypto_packet_unprotect,
    .retry_integrity_tag = quic_crypto_retry_integrity_tag,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3
