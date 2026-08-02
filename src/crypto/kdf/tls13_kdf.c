// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tls13_kdf.c
 * @brief TLS 1.3 key schedule (see pc_tls13_kdf.h).
 */

#include "crypto/kdf/tls13_kdf.h"

#if (PC_ENABLE_HTTP3 || PC_ENABLE_DTLS)

#include "crypto/hash/sha256.h"
#include "crypto/kdf/hkdf.h"
#include "crypto/mac/hmac_sha256.h"
#include <string.h>

// RFC 8446 sec 7.1 ("tls13 ") and RFC 9147 sec 5.9 ("dtls13") HKDF-Expand-Label prefixes.
const Tls13Kdf TLS13_KDF = {"tls13 "};
const Tls13Kdf DTLS13_KDF = {"dtls13"};

// Transcript-Hash of the empty string: SHA-256(""), the context for the two "derived" steps.
static void empty_hash(uint8_t out[TLS13_SECRET_LEN])
{
    pc_sha256(NULL, 0, out);
}

void pc_tls13_kdf_expand_label(const Tls13Kdf *kdf, const uint8_t secret[TLS13_SECRET_LEN], const char *label,
                               uint8_t *out, size_t out_len)
{
    pc_hkdf_expand_label(secret, label, out, out_len, kdf->label_prefix);
}

void pc_tls13_derive_secret(const Tls13Kdf *kdf, const uint8_t secret[TLS13_SECRET_LEN], const char *label,
                            const uint8_t transcript_hash[TLS13_SECRET_LEN], uint8_t out[TLS13_SECRET_LEN])
{
    // Derive-Secret(Secret, Label, Messages) = HKDF-Expand-Label(Secret, Label, Hash(Messages), L).
    pc_hkdf_expand_label_ctx(secret, label, transcript_hash, TLS13_SECRET_LEN, out, TLS13_SECRET_LEN,
                             kdf->label_prefix);
}

void pc_tls13_ks_early(const Tls13Kdf *kdf, Tls13KeySchedule *ks)
{
    ks->kdf = kdf;
    // No PSK: Early Secret = HKDF-Extract(salt=0, IKM=0^Hash.length). HMAC zero-pads a short/absent
    // key, so an empty salt and a 32-zero-byte IKM reproduce the RFC 8448 early secret exactly.
    uint8_t zeros[TLS13_SECRET_LEN];
    memset(zeros, 0, sizeof(zeros));
    pc_hkdf_extract(NULL, 0, zeros, sizeof(zeros), ks->early_secret);
}

void pc_tls13_ks_handshake(Tls13KeySchedule *ks, const uint8_t *ecdhe, const uint8_t ch_sh_hash[TLS13_SECRET_LEN],
                           size_t ecdhe_len)
{
    // Handshake Secret = HKDF-Extract(Derive-Secret(Early, "derived", ""), (EC)DHE).
    uint8_t eh[TLS13_SECRET_LEN];
    empty_hash(eh);
    uint8_t derived[TLS13_SECRET_LEN];
    pc_tls13_derive_secret(ks->kdf, ks->early_secret, "derived", eh, derived);
    pc_hkdf_extract(derived, sizeof(derived), ecdhe, ecdhe_len, ks->handshake_secret);

    pc_tls13_derive_secret(ks->kdf, ks->handshake_secret, "c hs traffic", ch_sh_hash, ks->client_hs_traffic);
    pc_tls13_derive_secret(ks->kdf, ks->handshake_secret, "s hs traffic", ch_sh_hash, ks->server_hs_traffic);
}

void pc_tls13_ks_master(Tls13KeySchedule *ks, const uint8_t ch_sfin_hash[TLS13_SECRET_LEN])
{
    // Master Secret = HKDF-Extract(Derive-Secret(Handshake, "derived", ""), 0^Hash.length).
    uint8_t eh[TLS13_SECRET_LEN];
    empty_hash(eh);
    uint8_t derived[TLS13_SECRET_LEN];
    pc_tls13_derive_secret(ks->kdf, ks->handshake_secret, "derived", eh, derived);
    uint8_t zeros[TLS13_SECRET_LEN];
    memset(zeros, 0, sizeof(zeros));
    pc_hkdf_extract(derived, sizeof(derived), zeros, sizeof(zeros), ks->master_secret);

    pc_tls13_derive_secret(ks->kdf, ks->master_secret, "c ap traffic", ch_sfin_hash, ks->client_ap_traffic);
    pc_tls13_derive_secret(ks->kdf, ks->master_secret, "s ap traffic", ch_sfin_hash, ks->server_ap_traffic);
}

void pc_tls13_finished_mac(const Tls13Kdf *kdf, const uint8_t base_secret[TLS13_SECRET_LEN],
                           const uint8_t transcript_hash[TLS13_SECRET_LEN], uint8_t out[TLS13_SECRET_LEN])
{
    // finished_key = HKDF-Expand-Label(base_secret, "finished", "", L); verify_data = HMAC(fk, Hash).
    uint8_t finished_key[TLS13_SECRET_LEN];
    pc_hkdf_expand_label(base_secret, "finished", finished_key, sizeof(finished_key), kdf->label_prefix);
    pc_hmac_sha256(finished_key, sizeof(finished_key), transcript_hash, TLS13_SECRET_LEN, out);
}

#endif // PC_ENABLE_HTTP3 || PC_ENABLE_DTLS
