// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file key_schedule.c
 * @brief TLS 1.3 key schedule (RFC 8446 sec 7.1). See key_schedule.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if (PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_TLS_SOFTWARE)

#include "network_drivers/tls/key_schedule/key_schedule.h"

#include "crypto/hash/sha256.h"
#include "crypto/kdf/hkdf.h"
#include "crypto/mac/hmac_sha256.h"

// RFC 8446 sec 7.1 ("tls13 ") and RFC 9147 sec 5.9 ("dtls13") HKDF-Expand-Label prefixes.
const Tls13Kdf TLS13_KDF = {"tls13 "};
const Tls13Kdf DTLS13_KDF = {"dtls13"};

/**
 * @brief The schedule's calls - what Tls13KsNs points at.
 *
 * @var Tls13KsInternal::ns  the handle a caller sets a call's members on
 */

// HKDF-Expand-Label(secret, label, "", out_len) under kdf's prefix.
static void expand(const Tls13Kdf *kdf, uint8_t *work, const uint8_t *secret, const char *label, uint8_t *out,
                   size_t out_len)
{
    Hkdf.expand_label_args.secret = secret;
    Hkdf.expand_label_args.label = label;
    Hkdf.expand_label_args.out = out;
    Hkdf.expand_label_args.out_len = out_len;
    Hkdf.expand_label_args.label_prefix = kdf->label_prefix;
    Hkdf.expand_label(work);
}

// Derive-Secret(secret, label, Messages) = HKDF-Expand-Label(secret, label, Hash(Messages), L).
static void derive(const Tls13Kdf *kdf, uint8_t *work, const uint8_t *secret, const char *label,
                   const uint8_t *transcript_hash, uint8_t *out)
{
    Hkdf.expand_label_ctx_args.secret = secret;
    Hkdf.expand_label_ctx_args.label = label;
    Hkdf.expand_label_ctx_args.context = transcript_hash;
    Hkdf.expand_label_ctx_args.context_len = TLS13_SECRET_LEN;
    Hkdf.expand_label_ctx_args.out = out;
    Hkdf.expand_label_ctx_args.out_len = TLS13_SECRET_LEN;
    Hkdf.expand_label_ctx_args.label_prefix = kdf->label_prefix;
    Hkdf.expand_label_ctx(work);
}

static void ks_expand_label(uint8_t *restrict work)
{
    expand(Tls13Ks.bind.kdf, Tls13Ks.derive_args.work, Tls13Ks.derive_args.secret, Tls13Ks.derive_args.label,
           Tls13Ks.derive_args.out, Tls13Ks.derive_args.out_len);
}

static void ks_derive_secret(uint8_t *restrict work)
{
    derive(Tls13Ks.bind.kdf, Tls13Ks.derive_args.work, Tls13Ks.derive_args.secret, Tls13Ks.derive_args.label,
           Tls13Ks.derive_args.transcript_hash, Tls13Ks.derive_args.out);
}

static void ks_early(uint8_t *restrict work)
{
    Tls13KeySchedule *ks = Tls13Ks.bind.ks;
    ks->kdf = Tls13Ks.bind.kdf;
    ks->s = Tls13Ks.bind.s;
    if (ks->s == NULL)
    {
        Tls13Ks.ok = PROTO_FALSE;
        return;
    }
    // No PSK: Early Secret = HKDF-Extract(salt=0, IKM=0^Hash.length). HMAC zero-pads a short/absent
    // key, so an empty salt and a 32-zero-byte IKM reproduce the RFC 8448 early secret exactly.
    Hkdf.extract_args.salt = NULL;
    Hkdf.extract_args.salt_len = 0;
    Hkdf.extract_args.ikm = ks->s + TLS13_KS_ZEROS;
    Hkdf.extract_args.ikm_len = TLS13_SECRET_LEN;
    Hkdf.extract_args.prk = ks->s + TLS13_KS_EARLY;
    Hkdf.extract(ks->s + TLS13_KS_WORK);
    Tls13Ks.ok = PROTO_TRUE;
}

static void ks_handshake(uint8_t *restrict work)
{
    Tls13KeySchedule *ks = Tls13Ks.bind.ks;
    if (ks->s == NULL)
    {
        return;
    }
    const uint8_t *ch_sh_hash = Tls13Ks.step.ch_sh_hash;
    // Handshake Secret = HKDF-Extract(Derive-Secret(Early, "derived", ""), (EC)DHE).
    Sha256.hash_args.data = NULL;
    Sha256.hash_args.len = 0;
    Sha256.hash_args.out = ks->s + TLS13_KS_EMPTY_HASH;
    Sha256.hash(ks->s + TLS13_KS_WORK);
    derive(ks->kdf, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_EARLY, "derived", ks->s + TLS13_KS_EMPTY_HASH,
           ks->s + TLS13_KS_DERIVED);
    Hkdf.extract_args.salt = ks->s + TLS13_KS_DERIVED;
    Hkdf.extract_args.salt_len = TLS13_SECRET_LEN;
    Hkdf.extract_args.ikm = Tls13Ks.step.ecdhe;
    Hkdf.extract_args.ikm_len = Tls13Ks.step.ecdhe_len;
    Hkdf.extract_args.prk = ks->s + TLS13_KS_HANDSHAKE;
    Hkdf.extract(ks->s + TLS13_KS_WORK);

    derive(ks->kdf, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_HANDSHAKE, "c hs traffic", ch_sh_hash,
           ks->s + TLS13_KS_CLIENT_HS);
    derive(ks->kdf, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_HANDSHAKE, "s hs traffic", ch_sh_hash,
           ks->s + TLS13_KS_SERVER_HS);
}

static void ks_master(uint8_t *restrict work)
{
    Tls13KeySchedule *ks = Tls13Ks.bind.ks;
    if (ks->s == NULL)
    {
        return;
    }
    const uint8_t *ch_sfin_hash = Tls13Ks.step.ch_sfin_hash;
    // Master Secret = HKDF-Extract(Derive-Secret(Handshake, "derived", ""), 0^Hash.length).
    Sha256.hash_args.data = NULL;
    Sha256.hash_args.len = 0;
    Sha256.hash_args.out = ks->s + TLS13_KS_EMPTY_HASH;
    Sha256.hash(ks->s + TLS13_KS_WORK);
    derive(ks->kdf, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_HANDSHAKE, "derived", ks->s + TLS13_KS_EMPTY_HASH,
           ks->s + TLS13_KS_DERIVED);
    Hkdf.extract_args.salt = ks->s + TLS13_KS_DERIVED;
    Hkdf.extract_args.salt_len = TLS13_SECRET_LEN;
    Hkdf.extract_args.ikm = ks->s + TLS13_KS_ZEROS;
    Hkdf.extract_args.ikm_len = TLS13_SECRET_LEN;
    Hkdf.extract_args.prk = ks->s + TLS13_KS_MASTER;
    Hkdf.extract(ks->s + TLS13_KS_WORK);

    derive(ks->kdf, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_MASTER, "c ap traffic", ch_sfin_hash,
           ks->s + TLS13_KS_CLIENT_AP);
    derive(ks->kdf, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_MASTER, "s ap traffic", ch_sfin_hash,
           ks->s + TLS13_KS_SERVER_AP);
}

static void ks_finished_mac(uint8_t *restrict work)
{
    Tls13KeySchedule *ks = Tls13Ks.bind.ks;
    if (ks->s == NULL)
    {
        return;
    }
    // finished_key = HKDF-Expand-Label(base_secret, "finished", "", L); verify_data = HMAC(fk, Hash).
    uint8_t *fk = ks->s + TLS13_KS_FINISHED_KEY;
    expand(ks->kdf, ks->s + TLS13_KS_WORK, Tls13Ks.finished_args.base_secret, "finished", fk, TLS13_SECRET_LEN);
    HmacSha256.mac_args.key = fk;
    HmacSha256.mac_args.key_len = TLS13_SECRET_LEN;
    HmacSha256.mac_args.data = Tls13Ks.finished_args.transcript_hash;
    HmacSha256.mac_args.len = TLS13_SECRET_LEN;
    HmacSha256.mac_args.out = Tls13Ks.finished_args.out;
    HmacSha256.mac(ks->s + TLS13_KS_WORK);
}

Tls13KsNs Tls13Ks = {.expand_label = ks_expand_label,
                     .derive_secret = ks_derive_secret,
                     .early = ks_early,
                     .handshake = ks_handshake,
                     .master = ks_master,
                     .finished_mac = ks_finished_mac};

#endif // PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_TLS_SOFTWARE
