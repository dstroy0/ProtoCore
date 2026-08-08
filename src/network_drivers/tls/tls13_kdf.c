// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tls13_kdf.c
 * @brief TLS 1.3 key schedule (see pc_tls13_kdf.h).
 */

#include "network_drivers/tls/tls13_kdf.h"

#if (PC_ENABLE_HTTP3 || PC_ENABLE_DTLS || PC_TLS_SOFTWARE)

#include "crypto/hash/sha256.h"
#include "crypto/kdf/hkdf.h"
#include "crypto/mac/hmac_sha256.h"
#include "mmgr/secure.h"

// The secure-pool term this file declares against PC_SECURE_ARENA_SIZE: one schedule's terms per
// handshake in flight, from the persistent end for the life of the connection.
static_assert(PC_WORK_TLS13_KDF >= (size_t)PC_TLS13_KS_SLOTS * PC_TLS13_KS_CAP,
              "PC_WORK_TLS13_KDF must cover PC_TLS13_KS_SLOTS schedules of PC_TLS13_KS_CAP: raise it in "
              "protocore_config.h");

// RFC 8446 sec 7.1 ("tls13 ") and RFC 9147 sec 5.9 ("dtls13") HKDF-Expand-Label prefixes.
const Tls13Kdf TLS13_KDF = {"tls13 "};
const Tls13Kdf DTLS13_KDF = {"dtls13"};


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

proto_bool pc_tls13_ks_early(const Tls13Kdf *kdf, Tls13KeySchedule *ks)
{
    ks->kdf = kdf;
    pc_span b = secure.persist_span(PC_TLS13_KS_CAP);
    if (!pc_span_ok(b))
    {
        ks->s = NULL;
        return PROTO_FALSE;
    }
    ks->s = b.buf;
    // No PSK: Early Secret = HKDF-Extract(salt=0, IKM=0^Hash.length). HMAC zero-pads a short/absent
    // key, so an empty salt and a 32-zero-byte IKM reproduce the RFC 8448 early secret exactly.
    pc_hkdf_extract(NULL, 0, ks->s + TLS13_KS_ZEROS, TLS13_SECRET_LEN, ks->s + TLS13_KS_EARLY);
    return PROTO_TRUE;
}

void pc_tls13_ks_handshake(Tls13KeySchedule *ks, const uint8_t *ecdhe, const uint8_t ch_sh_hash[TLS13_SECRET_LEN],
                           size_t ecdhe_len)
{
    if (ks->s == NULL)
    {
        return;
    }
    // Handshake Secret = HKDF-Extract(Derive-Secret(Early, "derived", ""), (EC)DHE).
    pc_sha256(NULL, 0, ks->s + TLS13_KS_EMPTY_HASH);
    pc_tls13_derive_secret(ks->kdf, ks->s + TLS13_KS_EARLY, "derived", ks->s + TLS13_KS_EMPTY_HASH,
                           ks->s + TLS13_KS_DERIVED);
    pc_hkdf_extract(ks->s + TLS13_KS_DERIVED, TLS13_SECRET_LEN, ecdhe, ecdhe_len, ks->s + TLS13_KS_HANDSHAKE);

    pc_tls13_derive_secret(ks->kdf, ks->s + TLS13_KS_HANDSHAKE, "c hs traffic", ch_sh_hash,
                           ks->s + TLS13_KS_CLIENT_HS);
    pc_tls13_derive_secret(ks->kdf, ks->s + TLS13_KS_HANDSHAKE, "s hs traffic", ch_sh_hash,
                           ks->s + TLS13_KS_SERVER_HS);
}

void pc_tls13_ks_master(Tls13KeySchedule *ks, const uint8_t ch_sfin_hash[TLS13_SECRET_LEN])
{
    if (ks->s == NULL)
    {
        return;
    }
    // Master Secret = HKDF-Extract(Derive-Secret(Handshake, "derived", ""), 0^Hash.length).
    pc_sha256(NULL, 0, ks->s + TLS13_KS_EMPTY_HASH);
    pc_tls13_derive_secret(ks->kdf, ks->s + TLS13_KS_HANDSHAKE, "derived", ks->s + TLS13_KS_EMPTY_HASH,
                           ks->s + TLS13_KS_DERIVED);
    pc_hkdf_extract(ks->s + TLS13_KS_DERIVED, TLS13_SECRET_LEN, ks->s + TLS13_KS_ZEROS, TLS13_SECRET_LEN,
                    ks->s + TLS13_KS_MASTER);

    pc_tls13_derive_secret(ks->kdf, ks->s + TLS13_KS_MASTER, "c ap traffic", ch_sfin_hash, ks->s + TLS13_KS_CLIENT_AP);
    pc_tls13_derive_secret(ks->kdf, ks->s + TLS13_KS_MASTER, "s ap traffic", ch_sfin_hash, ks->s + TLS13_KS_SERVER_AP);
}

void pc_tls13_finished_mac(Tls13KeySchedule *ks, const uint8_t base_secret[TLS13_SECRET_LEN],
                           const uint8_t transcript_hash[TLS13_SECRET_LEN], uint8_t out[TLS13_SECRET_LEN])
{
    if (ks->s == NULL)
    {
        return;
    }
    // finished_key = HKDF-Expand-Label(base_secret, "finished", "", L); verify_data = HMAC(fk, Hash).
    uint8_t *fk = ks->s + TLS13_KS_FINISHED_KEY;
    pc_hkdf_expand_label(base_secret, "finished", fk, TLS13_SECRET_LEN, ks->kdf->label_prefix);
    pc_hmac_sha256(fk, TLS13_SECRET_LEN, transcript_hash, TLS13_SECRET_LEN, out);
}

#endif // PC_ENABLE_HTTP3 || PC_ENABLE_DTLS || PC_TLS_SOFTWARE
