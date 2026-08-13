// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tls13_kdf.c
 * @brief TLS 1.3 key schedule (see protocore_tls13_kdf.h).
 */

#include "network_drivers/tls/tls13_kdf.h"

#if (PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_TLS_SOFTWARE)

#include "crypto/hash/sha256.h"
#include "crypto/kdf/hkdf.h"
#include "crypto/mac/hmac_sha256.h"

// RFC 8446 sec 7.1 ("tls13 ") and RFC 9147 sec 5.9 ("dtls13") HKDF-Expand-Label prefixes.
const Tls13Kdf TLS13_KDF = {"tls13 "};
const Tls13Kdf DTLS13_KDF = {"dtls13"};

void protocore_tls13_kdf_expand_label(const Tls13Kdf *kdf, uint8_t *work, const uint8_t secret[TLS13_SECRET_LEN],
                               const char *label, uint8_t *out, size_t out_len)
{
    protocore_hkdf_expand_label(work, secret, label, out, out_len, kdf->label_prefix);
}

void protocore_tls13_derive_secret(const Tls13Kdf *kdf, uint8_t *work, const uint8_t secret[TLS13_SECRET_LEN],
                            const char *label, const uint8_t transcript_hash[TLS13_SECRET_LEN],
                            uint8_t out[TLS13_SECRET_LEN])
{
    // Derive-Secret(Secret, Label, Messages) = HKDF-Expand-Label(Secret, Label, Hash(Messages), L).
    protocore_hkdf_expand_label_ctx(work, secret, label, transcript_hash, TLS13_SECRET_LEN, out, TLS13_SECRET_LEN,
                             kdf->label_prefix);
}

proto_bool protocore_tls13_ks_early(const Tls13Kdf *kdf, Tls13KeySchedule *ks, uint8_t *s)
{
    ks->kdf = kdf;
    ks->s = s;
    if (s == NULL)
    {
        return PROTO_FALSE;
    }
    // No PSK: Early Secret = HKDF-Extract(salt=0, IKM=0^Hash.length). HMAC zero-pads a short/absent
    // key, so an empty salt and a 32-zero-byte IKM reproduce the RFC 8448 early secret exactly.
    protocore_hkdf_extract(ks->s + TLS13_KS_WORK, NULL, 0, ks->s + TLS13_KS_ZEROS, TLS13_SECRET_LEN, ks->s + TLS13_KS_EARLY);
    return PROTO_TRUE;
}

void protocore_tls13_ks_handshake(Tls13KeySchedule *ks, const uint8_t *ecdhe, const uint8_t ch_sh_hash[TLS13_SECRET_LEN],
                           size_t ecdhe_len)
{
    if (ks->s == NULL)
    {
        return;
    }
    // Handshake Secret = HKDF-Extract(Derive-Secret(Early, "derived", ""), (EC)DHE).
    protocore_sha256(ks->s + TLS13_KS_WORK, NULL, 0, ks->s + TLS13_KS_EMPTY_HASH);
    protocore_tls13_derive_secret(ks->kdf, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_EARLY, "derived",
                           ks->s + TLS13_KS_EMPTY_HASH, ks->s + TLS13_KS_DERIVED);
    protocore_hkdf_extract(ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_DERIVED, TLS13_SECRET_LEN, ecdhe, ecdhe_len,
                    ks->s + TLS13_KS_HANDSHAKE);

    protocore_tls13_derive_secret(ks->kdf, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_HANDSHAKE, "c hs traffic", ch_sh_hash,
                           ks->s + TLS13_KS_CLIENT_HS);
    protocore_tls13_derive_secret(ks->kdf, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_HANDSHAKE, "s hs traffic", ch_sh_hash,
                           ks->s + TLS13_KS_SERVER_HS);
}

void protocore_tls13_ks_master(Tls13KeySchedule *ks, const uint8_t ch_sfin_hash[TLS13_SECRET_LEN])
{
    if (ks->s == NULL)
    {
        return;
    }
    // Master Secret = HKDF-Extract(Derive-Secret(Handshake, "derived", ""), 0^Hash.length).
    protocore_sha256(ks->s + TLS13_KS_WORK, NULL, 0, ks->s + TLS13_KS_EMPTY_HASH);
    protocore_tls13_derive_secret(ks->kdf, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_HANDSHAKE, "derived",
                           ks->s + TLS13_KS_EMPTY_HASH, ks->s + TLS13_KS_DERIVED);
    protocore_hkdf_extract(ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_DERIVED, TLS13_SECRET_LEN, ks->s + TLS13_KS_ZEROS,
                    TLS13_SECRET_LEN, ks->s + TLS13_KS_MASTER);

    protocore_tls13_derive_secret(ks->kdf, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_MASTER, "c ap traffic", ch_sfin_hash,
                           ks->s + TLS13_KS_CLIENT_AP);
    protocore_tls13_derive_secret(ks->kdf, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_MASTER, "s ap traffic", ch_sfin_hash,
                           ks->s + TLS13_KS_SERVER_AP);
}

void protocore_tls13_finished_mac(Tls13KeySchedule *ks, const uint8_t base_secret[TLS13_SECRET_LEN],
                           const uint8_t transcript_hash[TLS13_SECRET_LEN], uint8_t out[TLS13_SECRET_LEN])
{
    if (ks->s == NULL)
    {
        return;
    }
    // finished_key = HKDF-Expand-Label(base_secret, "finished", "", L); verify_data = HMAC(fk, Hash).
    uint8_t *fk = ks->s + TLS13_KS_FINISHED_KEY;
    protocore_hkdf_expand_label(ks->s + TLS13_KS_WORK, base_secret, "finished", fk, TLS13_SECRET_LEN, ks->kdf->label_prefix);
    protocore_hmac_sha256(ks->s + TLS13_KS_WORK, fk, TLS13_SECRET_LEN, transcript_hash, TLS13_SECRET_LEN, out);
}

#endif // PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_TLS_SOFTWARE
