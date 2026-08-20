// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hkdf_sha384.c
 * @brief HKDF-SHA384 and TLS 1.3 HKDF-Expand-Label implementation (see hkdf_sha384.h).
 *
 * One arm: every entry is written in terms of the @ref HmacSha384Ns entries, so which arm compresses
 * the underlying SHA-384 is not visible here and this file is the same on every target.
 *
 * Nothing here owns storage or touches the pool. The caller hands over PROTOCORE_HKDF_SHA384_BORROW bytes and
 * this file splits them by offset: the HMAC's own bytes, the T(i) block the expand loop chains through,
 * and the HkdfLabel the labelled forms compose. No region outlives the call that writes it, so there is
 * no context to carry and none is declared.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HKDF_SHA384

#include "crypto/crypto_opt.h"
#include "crypto/kdf/hkdf_sha384/hkdf_sha384.h"
#include "crypto/mac/hmac_sha384/hmac_sha384.h"
#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h"

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// The caller's borrow, split: the HMAC's own, the T(i) block, then the HkdfLabel.
#define HKDF_SHA384_OFF_HMAC 0u
#define HKDF_SHA384_OFF_T (HKDF_SHA384_OFF_HMAC + PROTOCORE_HMAC_SHA384_BORROW)
#define HKDF_SHA384_OFF_INFO (HKDF_SHA384_OFF_T + PROTOCORE_HKDF_SHA384_HASH_LEN)
// uint16 length, the label's own length byte and its 255 bytes, then the context's.
#define HKDF_SHA384_INFO_CAP (2 + 1 + 255 + 1 + 255)
static_assert(HKDF_SHA384_OFF_INFO + HKDF_SHA384_INFO_CAP <= PROTOCORE_HKDF_SHA384_BORROW,
              "PROTOCORE_HKDF_SHA384_BORROW is short of the split - raise it in protocore_config.h, which every "
              "consumer sizes its own borrow from");

// The regions, at their offsets in the caller's borrow.
#define HKDF_SHA384_HMAC(w) ((w) + HKDF_SHA384_OFF_HMAC)
#define HKDF_SHA384_T(w) ((w) + HKDF_SHA384_OFF_T)
#define HKDF_SHA384_INFO(w) ((w) + HKDF_SHA384_OFF_INFO)

// --- the derivations -------------------------------------------------------

// RFC 5869 sec 2.3 HKDF-Expand. The TLS 1.3 secrets are one hash block each, but the general N-block
// loop is written out so a caller asking for more than 48 bytes stays correct.
// T(i) = HMAC(PRK, T(i-1) || info || i), i counts from 1.
static proto_bool hkdf_sha384_derive(uint8_t *restrict work, const uint8_t *prk, const uint8_t *info, size_t info_len,
                                     uint8_t *out, size_t out_len)
{
    // RFC 5869 sec 2.3 bounds L at 255*HashLen because the block counter is a single octet. Past
    // that the counter wraps and T(256) repeats T(1), so the output would silently reuse earlier
    // key material. There is no defined answer, so give none.
    if (out_len > (size_t)255 * PROTOCORE_HKDF_SHA384_HASH_LEN)
    {
        mem.set(out, 0, out_len);
        return PROTO_FALSE;
    }
    uint8_t *t = HKDF_SHA384_T(work);
    size_t t_len = 0; // 0 for T(0) (empty), PROTOCORE_HKDF_SHA384_HASH_LEN afterwards
    size_t done = 0;
    uint8_t counter = 0;
    while (done < out_len)
    {
        counter++;
        HmacSha384.key_args.key = prk;
        HmacSha384.key_args.key_len = PROTOCORE_HKDF_SHA384_HASH_LEN;
        HmacSha384.init(HKDF_SHA384_HMAC(work));
        HmacSha384.update_args.data = t;
        HmacSha384.update_args.len = t_len;
        HmacSha384.update(HKDF_SHA384_HMAC(work));
        HmacSha384.update_args.data = info;
        HmacSha384.update_args.len = info_len;
        HmacSha384.update(HKDF_SHA384_HMAC(work));
        HmacSha384.update_args.data = &counter;
        HmacSha384.update_args.len = 1;
        HmacSha384.update(HKDF_SHA384_HMAC(work));
        HmacSha384.final_args.out = t;
        HmacSha384.final(HKDF_SHA384_HMAC(work));
        t_len = PROTOCORE_HKDF_SHA384_HASH_LEN;

        size_t take = out_len - done;
        if (take > PROTOCORE_HKDF_SHA384_HASH_LEN)
        {
            take = PROTOCORE_HKDF_SHA384_HASH_LEN;
        }
        mem.cpy(out + done, t, take);
        done += take;
    }
    return PROTO_TRUE;
}

// Compose the HkdfLabel in the borrow's info region and expand under it.
static proto_bool hkdf_sha384_label_derive(uint8_t *restrict work, const uint8_t *secret, const char *label,
                                           const uint8_t *context, size_t context_len, uint8_t *out, size_t out_len,
                                           const char *label_prefix)
{
    // HkdfLabel (RFC 8446 sec 7.1): uint16 length | opaque label<..> = label_prefix + label | opaque context.
    // The prefix is "tls13 " for TLS/QUIC (RFC 8446) or "dtls13" for DTLS 1.3 (RFC 9147 sec 5.9); the
    // caller supplies whichever applies, so this primitive stays protocol-agnostic. Label length maxes
    // out well under 255 (longest is "tls13 client in" = 15); the context is a Transcript-Hash (<= 48)
    // for Derive-Secret and empty for the record keys. The caller's HKDF_SHA384_INFO_CAP region covers
    // every caller.
    // Bound the scans: the combined label is opaque<7..255> (RFC 8446 sec 7.1), so cap prefix+label at
    // 255 - this both fits the reserved region below and keeps the single length byte from wrapping,
    // even if a caller ever passed a non-NUL-terminated string.
    size_t prefix_len = str.len(label_prefix, 255);
    size_t label_len = str.len(label, 255 - prefix_len);
    uint8_t *info = HKDF_SHA384_INFO(work);
    size_t p = 0;
    info[p++] = (uint8_t)(out_len >> 8);
    info[p++] = (uint8_t)(out_len & 0xff);
    info[p++] = (uint8_t)(prefix_len + label_len); // full label length, prefix included
    mem.cpy(info + p, label_prefix, prefix_len);
    p += prefix_len;
    mem.cpy(info + p, label, label_len);
    p += label_len;
    info[p++] = (uint8_t)context_len;
    if (context_len)
    {
        mem.cpy(info + p, context, context_len);
        p += context_len;
    }

    return hkdf_sha384_derive(work, secret, info, p, out, out_len);
}

// --- the entries -----------------------------------------------------------

// RFC 5869 sec 2.2: PRK = HMAC-Hash(salt, IKM). HmacSha384 pre-hashes keys > 128 bytes and zero-pads
// shorter ones, which is exactly HMAC's own key handling, so the salt goes in as-is.
static void hkdf_sha384_extract(uint8_t *restrict work)
{
    HkdfSha384.ok = PROTO_FALSE;
    if (!HkdfSha384.extract_args.prk)
    {
        return;
    }
    HmacSha384.mac_args.key = HkdfSha384.extract_args.salt;
    HmacSha384.mac_args.key_len = HkdfSha384.extract_args.salt_len;
    HmacSha384.mac_args.data = HkdfSha384.extract_args.ikm;
    HmacSha384.mac_args.len = HkdfSha384.extract_args.ikm_len;
    HmacSha384.mac_args.out = HkdfSha384.extract_args.prk;
    HmacSha384.mac(HKDF_SHA384_HMAC(work));
    HkdfSha384.ok = HmacSha384.ok;
}

static void hkdf_sha384_expand(uint8_t *restrict work)
{
    HkdfSha384.ok = PROTO_FALSE;
    if (!HkdfSha384.expand_args.prk || !HkdfSha384.expand_args.out)
    {
        return;
    }
    HkdfSha384.ok =
        hkdf_sha384_derive(work, HkdfSha384.expand_args.prk, HkdfSha384.expand_args.info,
                           HkdfSha384.expand_args.info_len, HkdfSha384.expand_args.out, HkdfSha384.expand_args.out_len);
}

static void hkdf_sha384_expand_label(uint8_t *restrict work)
{
    HkdfSha384.ok = PROTO_FALSE;
    if (!HkdfSha384.expand_label_args.secret || !HkdfSha384.expand_label_args.label ||
        !HkdfSha384.expand_label_args.label_prefix || !HkdfSha384.expand_label_args.out)
    {
        return;
    }
    HkdfSha384.ok =
        hkdf_sha384_label_derive(work, HkdfSha384.expand_label_args.secret, HkdfSha384.expand_label_args.label, NULL, 0,
                                 HkdfSha384.expand_label_args.out, HkdfSha384.expand_label_args.out_len,
                                 HkdfSha384.expand_label_args.label_prefix);
}

static void hkdf_sha384_expand_label_ctx(uint8_t *restrict work)
{
    HkdfSha384.ok = PROTO_FALSE;
    if (!HkdfSha384.expand_label_ctx_args.secret || !HkdfSha384.expand_label_ctx_args.label ||
        !HkdfSha384.expand_label_ctx_args.label_prefix || !HkdfSha384.expand_label_ctx_args.out)
    {
        return;
    }
    HkdfSha384.ok =
        hkdf_sha384_label_derive(work, HkdfSha384.expand_label_ctx_args.secret, HkdfSha384.expand_label_ctx_args.label,
                                 HkdfSha384.expand_label_ctx_args.context, HkdfSha384.expand_label_ctx_args.context_len,
                                 HkdfSha384.expand_label_ctx_args.out, HkdfSha384.expand_label_ctx_args.out_len,
                                 HkdfSha384.expand_label_ctx_args.label_prefix);
}

HkdfSha384Ns HkdfSha384 = {.extract = hkdf_sha384_extract,
                           .expand = hkdf_sha384_expand,
                           .expand_label = hkdf_sha384_expand_label,
                           .expand_label_ctx = hkdf_sha384_expand_label_ctx};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HKDF_SHA384
