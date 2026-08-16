// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hkdf.c
 * @brief HKDF-SHA256 and TLS 1.3 HKDF-Expand-Label implementation (see hkdf.h).
 *
 * One arm: every entry is written in terms of the @ref HmacSha256Ns entries, so which arm compresses
 * the underlying SHA-256 is not visible here and this file is the same on every target.
 *
 * Nothing here owns storage or touches the pool. The caller hands over PROTOCORE_HKDF_BORROW bytes and
 * this file splits them by offset: the HMAC's own bytes, the T(i) block the expand loop chains through,
 * and the HkdfLabel the labelled forms compose. No region outlives the call that writes it, so there is
 * no context to carry and none is declared.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HKDF

#include "crypto/kdf/hkdf.h"
#include "crypto/crypto_opt.h"
#include "crypto/mac/hmac_sha256.h"
#include "mmgr/protomem.h"
#include "mmgr/protostr.h"

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// The caller's borrow, split: the HMAC's own, the T(i) block, then the HkdfLabel.
#define HKDF_OFF_HMAC 0u
#define HKDF_OFF_T (HKDF_OFF_HMAC + PROTOCORE_HMAC_SHA256_BORROW)
#define HKDF_OFF_INFO (HKDF_OFF_T + PROTOCORE_HKDF_HASH_LEN)
// uint16 length, the label's own length byte and its 255 bytes, then the context's.
#define HKDF_INFO_CAP (2 + 1 + 255 + 1 + 255)
static_assert(HKDF_OFF_INFO + HKDF_INFO_CAP <= PROTOCORE_HKDF_BORROW,
              "PROTOCORE_HKDF_BORROW is short of the split - raise it in protocore_config.h, which every "
              "consumer sizes its own borrow from");

// The regions, at their offsets in the caller's borrow.
#define HKDF_HMAC(w) ((w) + HKDF_OFF_HMAC)
#define HKDF_T(w) ((w) + HKDF_OFF_T)
#define HKDF_INFO(w) ((w) + HKDF_OFF_INFO)

// --- the derivations -------------------------------------------------------

// RFC 5869 sec 2.3 HKDF-Expand for the QUIC case: the info block is small and fixed and the
// requested length never exceeds one hash block, but the general N-block loop is written out so a
// future >32-byte caller stays correct. T(i) = HMAC(PRK, T(i-1) || info || i), i counts from 1.
static proto_bool hkdf_derive(uint8_t *restrict work, const uint8_t *prk, const uint8_t *info, size_t info_len,
                              uint8_t *out, size_t out_len)
{
    // RFC 5869 sec 2.3 bounds L at 255*HashLen because the block counter is a single octet. Past
    // that the counter wraps and T(256) repeats T(1), so the output would silently reuse earlier
    // key material. There is no defined answer, so give none.
    if (out_len > (size_t)255 * PROTOCORE_HKDF_HASH_LEN)
    {
        mem.set(out, 0, out_len);
        return PROTO_FALSE;
    }
    uint8_t *t = HKDF_T(work);
    size_t t_len = 0; // 0 for T(0) (empty), PROTOCORE_HKDF_HASH_LEN afterwards
    size_t done = 0;
    uint8_t counter = 0;
    while (done < out_len)
    {
        counter++;
        HmacSha256.key_args.key = prk;
        HmacSha256.key_args.key_len = PROTOCORE_HKDF_HASH_LEN;
        HmacSha256.init(HKDF_HMAC(work));
        HmacSha256.update_args.data = t;
        HmacSha256.update_args.len = t_len;
        HmacSha256.update(HKDF_HMAC(work));
        HmacSha256.update_args.data = info;
        HmacSha256.update_args.len = info_len;
        HmacSha256.update(HKDF_HMAC(work));
        HmacSha256.update_args.data = &counter;
        HmacSha256.update_args.len = 1;
        HmacSha256.update(HKDF_HMAC(work));
        HmacSha256.final_args.out = t;
        HmacSha256.final(HKDF_HMAC(work));
        t_len = PROTOCORE_HKDF_HASH_LEN;

        size_t take = out_len - done;
        if (take > PROTOCORE_HKDF_HASH_LEN)
        {
            take = PROTOCORE_HKDF_HASH_LEN;
        }
        mem.cpy(out + done, t, take);
        done += take;
    }
    return PROTO_TRUE;
}

// Compose the HkdfLabel in the borrow's info region and expand under it.
static proto_bool hkdf_label_derive(uint8_t *restrict work, const uint8_t *secret, const char *label,
                                    const uint8_t *context, size_t context_len, uint8_t *out, size_t out_len,
                                    const char *label_prefix)
{
    // HkdfLabel (RFC 8446 sec 7.1): uint16 length | opaque label<..> = label_prefix + label | opaque context.
    // The prefix is "tls13 " for TLS/QUIC (RFC 8446) or "dtls13" for DTLS 1.3 (RFC 9147 sec 5.9); the
    // caller supplies whichever applies, so this primitive stays protocol-agnostic. Label length maxes
    // out well under 255 (longest is "tls13 client in" = 15); the context is a Transcript-Hash (<= 32)
    // for Derive-Secret and empty for packet-protection keys. The caller's HKDF_INFO_CAP region covers
    // every caller.
    // Bound the scans: the combined label is opaque<7..255> (RFC 8446 sec 7.1), so cap prefix+label at
    // 255 - this both fits the reserved region below and keeps the single length byte from wrapping,
    // even if a caller ever passed a non-NUL-terminated string.
    size_t prefix_len = str.len(label_prefix, 255);
    size_t label_len = str.len(label, 255 - prefix_len);
    uint8_t *info = HKDF_INFO(work);
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

    return hkdf_derive(work, secret, info, p, out, out_len);
}

// --- the entries -----------------------------------------------------------

// RFC 5869 sec 2.2: PRK = HMAC-Hash(salt, IKM). HmacSha256 pre-hashes keys > 64 bytes and zero-pads
// shorter ones, which is exactly HMAC's own key handling, so the salt goes in as-is.
static void hkdf_extract(uint8_t *restrict work)
{
    Hkdf.ok = PROTO_FALSE;
    if (!work || !Hkdf.extract_args.prk)
    {
        return;
    }
    HmacSha256.mac_args.key = Hkdf.extract_args.salt;
    HmacSha256.mac_args.key_len = Hkdf.extract_args.salt_len;
    HmacSha256.mac_args.data = Hkdf.extract_args.ikm;
    HmacSha256.mac_args.len = Hkdf.extract_args.ikm_len;
    HmacSha256.mac_args.out = Hkdf.extract_args.prk;
    HmacSha256.mac(HKDF_HMAC(work));
    Hkdf.ok = HmacSha256.ok;
}

static void hkdf_expand(uint8_t *restrict work)
{
    Hkdf.ok = PROTO_FALSE;
    if (!work || !Hkdf.expand_args.prk || !Hkdf.expand_args.out)
    {
        return;
    }
    Hkdf.ok = hkdf_derive(work, Hkdf.expand_args.prk, Hkdf.expand_args.info, Hkdf.expand_args.info_len,
                          Hkdf.expand_args.out, Hkdf.expand_args.out_len);
}

static void hkdf_expand_label(uint8_t *restrict work)
{
    Hkdf.ok = PROTO_FALSE;
    if (!work || !Hkdf.expand_label_args.secret || !Hkdf.expand_label_args.label ||
        !Hkdf.expand_label_args.label_prefix || !Hkdf.expand_label_args.out)
    {
        return;
    }
    Hkdf.ok = hkdf_label_derive(work, Hkdf.expand_label_args.secret, Hkdf.expand_label_args.label, NULL, 0,
                                Hkdf.expand_label_args.out, Hkdf.expand_label_args.out_len,
                                Hkdf.expand_label_args.label_prefix);
}

static void hkdf_expand_label_ctx(uint8_t *restrict work)
{
    Hkdf.ok = PROTO_FALSE;
    if (!work || !Hkdf.expand_label_ctx_args.secret || !Hkdf.expand_label_ctx_args.label ||
        !Hkdf.expand_label_ctx_args.label_prefix || !Hkdf.expand_label_ctx_args.out)
    {
        return;
    }
    Hkdf.ok = hkdf_label_derive(work, Hkdf.expand_label_ctx_args.secret, Hkdf.expand_label_ctx_args.label,
                                Hkdf.expand_label_ctx_args.context, Hkdf.expand_label_ctx_args.context_len,
                                Hkdf.expand_label_ctx_args.out, Hkdf.expand_label_ctx_args.out_len,
                                Hkdf.expand_label_ctx_args.label_prefix);
}

HkdfNs Hkdf = {.extract = hkdf_extract,
               .expand = hkdf_expand,
               .expand_label = hkdf_expand_label,
               .expand_label_ctx = hkdf_expand_label_ctx};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HKDF
