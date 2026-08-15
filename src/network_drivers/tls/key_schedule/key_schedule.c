// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file key_schedule.c
 * @brief TLS 1.3 key schedule (RFC 8446 sec 7.1). See key_schedule.h.
 */

#include "network_drivers/tls/key_schedule/key_schedule.h"

#if (PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_TLS_SOFTWARE)

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
struct Tls13KsInternal
{
    Tls13KsNs *ns;
};

static struct Tls13KsInternal s_ks = {.ns = &Tls13Ks};

// HKDF-Expand-Label(secret, label, "", out_len) under kdf's prefix.
static void expand(const Tls13Kdf *kdf, uint8_t *work, const uint8_t *secret, const char *label, uint8_t *out,
                   size_t out_len)
{
    protocore_hkdf_expand_label(work, secret, label, out, out_len, kdf->label_prefix);
}

// Derive-Secret(secret, label, Messages) = HKDF-Expand-Label(secret, label, Hash(Messages), L).
static void derive(const Tls13Kdf *kdf, uint8_t *work, const uint8_t *secret, const char *label,
                   const uint8_t *transcript_hash, uint8_t *out)
{
    protocore_hkdf_expand_label_ctx(work, secret, label, transcript_hash, TLS13_SECRET_LEN, out, TLS13_SECRET_LEN,
                                    kdf->label_prefix);
}

static void ks_expand_label(struct Tls13KsInternal *restrict ctx)
{
    expand(ctx->ns->bind.kdf, ctx->ns->derive_args.work, ctx->ns->derive_args.secret, ctx->ns->derive_args.label,
           ctx->ns->derive_args.out, ctx->ns->derive_args.out_len);
}

static void ks_derive_secret(struct Tls13KsInternal *restrict ctx)
{
    derive(ctx->ns->bind.kdf, ctx->ns->derive_args.work, ctx->ns->derive_args.secret, ctx->ns->derive_args.label,
           ctx->ns->derive_args.transcript_hash, ctx->ns->derive_args.out);
}

static void ks_early(struct Tls13KsInternal *restrict ctx)
{
    Tls13KeySchedule *ks = ctx->ns->bind.ks;
    ks->kdf = ctx->ns->bind.kdf;
    ks->s = ctx->ns->bind.s;
    if (ks->s == NULL)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    // No PSK: Early Secret = HKDF-Extract(salt=0, IKM=0^Hash.length). HMAC zero-pads a short/absent
    // key, so an empty salt and a 32-zero-byte IKM reproduce the RFC 8448 early secret exactly.
    protocore_hkdf_extract(ks->s + TLS13_KS_WORK, NULL, 0, ks->s + TLS13_KS_ZEROS, TLS13_SECRET_LEN,
                           ks->s + TLS13_KS_EARLY);
    ctx->ns->ok = PROTO_TRUE;
}

static void ks_handshake(struct Tls13KsInternal *restrict ctx)
{
    Tls13KeySchedule *ks = ctx->ns->bind.ks;
    if (ks->s == NULL)
    {
        return;
    }
    const uint8_t *ch_sh_hash = ctx->ns->step.ch_sh_hash;
    // Handshake Secret = HKDF-Extract(Derive-Secret(Early, "derived", ""), (EC)DHE).
    protocore_sha256(ks->s + TLS13_KS_WORK, NULL, 0, ks->s + TLS13_KS_EMPTY_HASH);
    derive(ks->kdf, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_EARLY, "derived", ks->s + TLS13_KS_EMPTY_HASH,
           ks->s + TLS13_KS_DERIVED);
    protocore_hkdf_extract(ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_DERIVED, TLS13_SECRET_LEN, ctx->ns->step.ecdhe,
                           ctx->ns->step.ecdhe_len, ks->s + TLS13_KS_HANDSHAKE);

    derive(ks->kdf, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_HANDSHAKE, "c hs traffic", ch_sh_hash,
           ks->s + TLS13_KS_CLIENT_HS);
    derive(ks->kdf, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_HANDSHAKE, "s hs traffic", ch_sh_hash,
           ks->s + TLS13_KS_SERVER_HS);
}

static void ks_master(struct Tls13KsInternal *restrict ctx)
{
    Tls13KeySchedule *ks = ctx->ns->bind.ks;
    if (ks->s == NULL)
    {
        return;
    }
    const uint8_t *ch_sfin_hash = ctx->ns->step.ch_sfin_hash;
    // Master Secret = HKDF-Extract(Derive-Secret(Handshake, "derived", ""), 0^Hash.length).
    protocore_sha256(ks->s + TLS13_KS_WORK, NULL, 0, ks->s + TLS13_KS_EMPTY_HASH);
    derive(ks->kdf, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_HANDSHAKE, "derived", ks->s + TLS13_KS_EMPTY_HASH,
           ks->s + TLS13_KS_DERIVED);
    protocore_hkdf_extract(ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_DERIVED, TLS13_SECRET_LEN, ks->s + TLS13_KS_ZEROS,
                           TLS13_SECRET_LEN, ks->s + TLS13_KS_MASTER);

    derive(ks->kdf, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_MASTER, "c ap traffic", ch_sfin_hash,
           ks->s + TLS13_KS_CLIENT_AP);
    derive(ks->kdf, ks->s + TLS13_KS_WORK, ks->s + TLS13_KS_MASTER, "s ap traffic", ch_sfin_hash,
           ks->s + TLS13_KS_SERVER_AP);
}

static void ks_finished_mac(struct Tls13KsInternal *restrict ctx)
{
    Tls13KeySchedule *ks = ctx->ns->bind.ks;
    if (ks->s == NULL)
    {
        return;
    }
    // finished_key = HKDF-Expand-Label(base_secret, "finished", "", L); verify_data = HMAC(fk, Hash).
    uint8_t *fk = ks->s + TLS13_KS_FINISHED_KEY;
    expand(ks->kdf, ks->s + TLS13_KS_WORK, ctx->ns->finished_args.base_secret, "finished", fk, TLS13_SECRET_LEN);
    protocore_hmac_sha256(ks->s + TLS13_KS_WORK, fk, TLS13_SECRET_LEN, ctx->ns->finished_args.transcript_hash,
                          TLS13_SECRET_LEN, ctx->ns->finished_args.out);
}

Tls13KsNs Tls13Ks = {.expand_label = ks_expand_label,
                     .derive_secret = ks_derive_secret,
                     .early = ks_early,
                     .handshake = ks_handshake,
                     .master = ks_master,
                     .finished_mac = ks_finished_mac,
                     .internal = &s_ks};

#endif // PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_TLS_SOFTWARE
