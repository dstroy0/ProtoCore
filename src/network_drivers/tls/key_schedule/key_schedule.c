// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file key_schedule.c
 * @brief TLS 1.3 key schedule (RFC 8446 sec 7.1). See key_schedule.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if (PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_TLS_SOFTWARE)

#include "network_drivers/tls/key_schedule/key_schedule.h"

#include "crypto/hash/sha256/sha256.h"
#include "crypto/hash/sha384/sha384.h"
#include "crypto/kdf/hkdf/hkdf.h"
#include "crypto/kdf/hkdf_sha384/hkdf_sha384.h"
#include "crypto/mac/hmac_sha256/hmac_sha256.h"
#include "crypto/mac/hmac_sha384/hmac_sha384.h"

// RFC 8446 sec 7.1 ("tls13 ") and RFC 9147 sec 5.9 ("dtls13") HKDF-Expand-Label prefixes.
const Tls13Kdf TLS13_KDF = {"tls13 "};
const Tls13Kdf DTLS13_KDF = {"dtls13"};

/**
 * @brief The schedule's calls - what Tls13KsNs points at.
 *
 * @var Tls13KsInternal::ns  the handle a caller sets a call's members on
 */

// The four places the suite's hash reaches: the two HKDF forms, the empty-transcript digest, and the
// Finished MAC. Every step below runs through these, so the hash is chosen once per primitive and the
// schedule's own shape does not branch on it.

// Hash.length for the bound suite (RFC 8446 sec 7.1).
static size_t hash_len(proto_bool is384)
{
    return is384 ? (size_t)TLS13_SECRET_SHA384 : (size_t)TLS13_SECRET_SHA256;
}

// HKDF-Expand-Label(secret, label, "", out_len) under kdf's prefix and the bound hash.
static void expand(const Tls13Kdf *kdf, proto_bool is384, uint8_t *work, const uint8_t *secret, const char *label,
                   uint8_t *out, size_t out_len)
{
    if (is384)
    {
        HkdfSha384.expand_label_args.secret = secret;
        HkdfSha384.expand_label_args.label = label;
        HkdfSha384.expand_label_args.out = out;
        HkdfSha384.expand_label_args.out_len = out_len;
        HkdfSha384.expand_label_args.label_prefix = kdf->label_prefix;
        HkdfSha384.expand_label(work);
        return;
    }
    Hkdf.expand_label_args.secret = secret;
    Hkdf.expand_label_args.label = label;
    Hkdf.expand_label_args.out = out;
    Hkdf.expand_label_args.out_len = out_len;
    Hkdf.expand_label_args.label_prefix = kdf->label_prefix;
    Hkdf.expand_label(work);
}

// Derive-Secret(secret, label, Messages) = HKDF-Expand-Label(secret, label, Hash(Messages), L).
static void derive(const Tls13Kdf *kdf, proto_bool is384, uint8_t *work, const uint8_t *secret, const char *label,
                   const uint8_t *transcript_hash, uint8_t *out)
{
    const size_t len = hash_len(is384);
    if (is384)
    {
        HkdfSha384.expand_label_ctx_args.secret = secret;
        HkdfSha384.expand_label_ctx_args.label = label;
        HkdfSha384.expand_label_ctx_args.context = transcript_hash;
        HkdfSha384.expand_label_ctx_args.context_len = len;
        HkdfSha384.expand_label_ctx_args.out = out;
        HkdfSha384.expand_label_ctx_args.out_len = len;
        HkdfSha384.expand_label_ctx_args.label_prefix = kdf->label_prefix;
        HkdfSha384.expand_label_ctx(work);
        return;
    }
    Hkdf.expand_label_ctx_args.secret = secret;
    Hkdf.expand_label_ctx_args.label = label;
    Hkdf.expand_label_ctx_args.context = transcript_hash;
    Hkdf.expand_label_ctx_args.context_len = len;
    Hkdf.expand_label_ctx_args.out = out;
    Hkdf.expand_label_ctx_args.out_len = len;
    Hkdf.expand_label_ctx_args.label_prefix = kdf->label_prefix;
    Hkdf.expand_label_ctx(work);
}

// HKDF-Extract(salt, ikm) under the bound hash.
static void extract(proto_bool is384, uint8_t *work, const uint8_t *salt, size_t salt_len, const uint8_t *ikm,
                    size_t ikm_len, uint8_t *prk)
{
    if (is384)
    {
        HkdfSha384.extract_args.salt = salt;
        HkdfSha384.extract_args.salt_len = salt_len;
        HkdfSha384.extract_args.ikm = ikm;
        HkdfSha384.extract_args.ikm_len = ikm_len;
        HkdfSha384.extract_args.prk = prk;
        HkdfSha384.extract(work);
        return;
    }
    Hkdf.extract_args.salt = salt;
    Hkdf.extract_args.salt_len = salt_len;
    Hkdf.extract_args.ikm = ikm;
    Hkdf.extract_args.ikm_len = ikm_len;
    Hkdf.extract_args.prk = prk;
    Hkdf.extract(work);
}

// Transcript-Hash("") under the bound hash, the context the "derived" steps take.
static void empty_hash(proto_bool is384, uint8_t *work, uint8_t *out)
{
    if (is384)
    {
        Sha384V.hash_args.data = NULL;
        Sha384V.hash_args.len = 0;
        Sha384V.hash_args.out = out;
        Sha384.hash(work);
        return;
    }
    Sha256V.hash_args.data = NULL;
    Sha256V.hash_args.len = 0;
    Sha256V.hash_args.out = out;
    Sha256.hash(work);
}

// HMAC(finished_key, transcript_hash) under the bound hash (RFC 8446 sec 4.4.4).
static void finished_hmac(proto_bool is384, uint8_t *work, const uint8_t *key, const uint8_t *data, uint8_t *out)
{
    const size_t len = hash_len(is384);
    if (is384)
    {
        HmacSha384.mac_args.key = key;
        HmacSha384.mac_args.key_len = len;
        HmacSha384.mac_args.data = data;
        HmacSha384.mac_args.len = len;
        HmacSha384.mac_args.out = out;
        HmacSha384.mac(work);
        return;
    }
    HmacSha256.mac_args.key = key;
    HmacSha256.mac_args.key_len = len;
    HmacSha256.mac_args.data = data;
    HmacSha256.mac_args.len = len;
    HmacSha256.mac_args.out = out;
    HmacSha256.mac(work);
}

void protocore_tls13_ks_expand_label(uint8_t *restrict work)
{
    expand(Tls13KsV.bind.kdf, Tls13KsV.bind.is384, Tls13KsV.derive_args.work, Tls13KsV.derive_args.secret,
           Tls13KsV.derive_args.label, Tls13KsV.derive_args.out, Tls13KsV.derive_args.out_len);
}

void protocore_tls13_ks_derive_secret(uint8_t *restrict work)
{
    derive(Tls13KsV.bind.kdf, Tls13KsV.bind.is384, Tls13KsV.derive_args.work, Tls13KsV.derive_args.secret,
           Tls13KsV.derive_args.label, Tls13KsV.derive_args.transcript_hash, Tls13KsV.derive_args.out);
}

void protocore_tls13_ks_early(uint8_t *restrict work)
{
    Tls13KeySchedule *ks = Tls13KsV.bind.ks;
    ks->kdf = Tls13KsV.bind.kdf;
    ks->s = Tls13KsV.bind.s;
    ks->is384 = Tls13KsV.bind.is384;
    ks->len = hash_len(ks->is384);
    Tls13KsV.len = ks->len;
    if (ks->s == NULL)
    {
        Tls13KsV.ok = PROTO_FALSE;
        return;
    }
    // No PSK: Early Secret = HKDF-Extract(salt=0, IKM=0^Hash.length). HMAC zero-pads a short/absent
    // key, so an empty salt and a run of Hash.length zero bytes reproduce the RFC 8448 early secret
    // exactly.
    extract(ks->is384, ks->s + TLS13_KS_WORK, NULL, 0, ks->s + TLS13_KS_ZEROS, ks->len, ks->s + TLS13_KS_EARLY);
    Tls13KsV.ok = PROTO_TRUE;
}

void protocore_tls13_ks_handshake(uint8_t *restrict work)
{
    Tls13KeySchedule *ks = Tls13KsV.bind.ks;
    if (ks->s == NULL)
    {
        return;
    }
    const uint8_t *ch_sh_hash = Tls13KsV.step.ch_sh_hash;
    // Handshake Secret = HKDF-Extract(Derive-Secret(Early, "derived", ""), (EC)DHE).
    empty_hash(ks->is384, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_EMPTY_HASH);
    derive(ks->kdf, ks->is384, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_EARLY, "derived", ks->s + TLS13_KS_EMPTY_HASH,
           ks->s + TLS13_KS_DERIVED);
    extract(ks->is384, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_DERIVED, ks->len, Tls13KsV.step.ecdhe,
            Tls13KsV.step.ecdhe_len, ks->s + TLS13_KS_HANDSHAKE);

    derive(ks->kdf, ks->is384, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_HANDSHAKE, "c hs traffic", ch_sh_hash,
           ks->s + TLS13_KS_CLIENT_HS);
    derive(ks->kdf, ks->is384, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_HANDSHAKE, "s hs traffic", ch_sh_hash,
           ks->s + TLS13_KS_SERVER_HS);
}

void protocore_tls13_ks_master(uint8_t *restrict work)
{
    Tls13KeySchedule *ks = Tls13KsV.bind.ks;
    if (ks->s == NULL)
    {
        return;
    }
    const uint8_t *ch_sfin_hash = Tls13KsV.step.ch_sfin_hash;
    // Master Secret = HKDF-Extract(Derive-Secret(Handshake, "derived", ""), 0^Hash.length).
    empty_hash(ks->is384, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_EMPTY_HASH);
    derive(ks->kdf, ks->is384, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_HANDSHAKE, "derived",
           ks->s + TLS13_KS_EMPTY_HASH, ks->s + TLS13_KS_DERIVED);
    extract(ks->is384, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_DERIVED, ks->len, ks->s + TLS13_KS_ZEROS, ks->len,
            ks->s + TLS13_KS_MASTER);

    derive(ks->kdf, ks->is384, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_MASTER, "c ap traffic", ch_sfin_hash,
           ks->s + TLS13_KS_CLIENT_AP);
    derive(ks->kdf, ks->is384, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_MASTER, "s ap traffic", ch_sfin_hash,
           ks->s + TLS13_KS_SERVER_AP);
}

void protocore_tls13_ks_finished_mac(uint8_t *restrict work)
{
    Tls13KeySchedule *ks = Tls13KsV.bind.ks;
    if (ks->s == NULL)
    {
        return;
    }
    // finished_key = HKDF-Expand-Label(base_secret, "finished", "", L); verify_data = HMAC(fk, Hash).
    uint8_t *fk = ks->s + TLS13_KS_FINISHED_KEY;
    expand(ks->kdf, ks->is384, ks->s + TLS13_KS_WORK, Tls13KsV.finished_args.base_secret, "finished", fk, ks->len);
    finished_hmac(ks->is384, ks->s + TLS13_KS_WORK, fk, Tls13KsV.finished_args.transcript_hash,
                  Tls13KsV.finished_args.out);
}

// RFC 8446 sec 4.4.1: the Transcript-Hash runs under the same suite hash as the schedule, so it
// dispatches on the bound flag rather than on a hash the caller names.

void protocore_tls13_ks_transcript_init(uint8_t *restrict work)
{
    if (Tls13KsV.bind.ks->is384)
    {
        Sha384.init(work);
        return;
    }
    Sha256.init(work);
}

void protocore_tls13_ks_transcript_update(uint8_t *restrict work)
{
    if (Tls13KsV.bind.ks->is384)
    {
        Sha384V.update_args.data = Tls13KsV.transcript_args.data;
        Sha384V.update_args.len = Tls13KsV.transcript_args.len;
        Sha384.update(work);
        return;
    }
    Sha256V.update_args.data = Tls13KsV.transcript_args.data;
    Sha256V.update_args.len = Tls13KsV.transcript_args.len;
    Sha256.update(work);
}

// Finalizing compresses the padded blocks into a copy of the state, so the running context is
// untouched and keeps taking messages.
void protocore_tls13_ks_transcript_peek(uint8_t *restrict work)
{
    if (Tls13KsV.bind.ks->is384)
    {
        Sha384V.final_args.out = Tls13KsV.transcript_args.out;
        Sha384.final(work);
        return;
    }
    Sha256V.final_args.out = Tls13KsV.transcript_args.out;
    Sha256.final(work);
}

/** @brief The operands and the outcome. */
Tls13KsVars Tls13KsV;

#endif // PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_TLS_SOFTWARE
