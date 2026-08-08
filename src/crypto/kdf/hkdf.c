// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hkdf.c
 * @brief HKDF-SHA256 and TLS 1.3 HKDF-Expand-Label (see pc_hkdf.h).
 */

#include "crypto/kdf/hkdf.h"

#if (PC_ENABLE_HTTP3 || PC_ENABLE_DTLS || PC_TLS_SOFTWARE)

#include "crypto/mac/hmac_sha256.h"

void pc_hkdf_extract(const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len,
                     uint8_t prk[PC_HKDF_HASH_LEN])
{
    // RFC 5869 sec 2.2: PRK = HMAC-Hash(salt, IKM). pc_hmac_sha256 pre-hashes keys > 64 bytes and
    // zero-pads shorter ones, which is exactly HMAC's own key handling, so the salt goes in as-is.
    pc_hmac_sha256(salt, salt_len, ikm, ikm_len, prk);
}

// RFC 5869 sec 2.3 HKDF-Expand for the QUIC case: the info block is small and fixed and the
// requested length never exceeds one hash block, but the general N-block loop is written out so a
// future >32-byte caller stays correct. T(i) = HMAC(PRK, T(i-1) || info || i), i counts from 1.
static void hkdf_expand(const uint8_t prk[PC_HKDF_HASH_LEN], const uint8_t *info, size_t info_len, uint8_t *out,
                        size_t out_len)
{
    uint8_t t[PC_HKDF_HASH_LEN];
    size_t t_len = 0; // 0 for T(0) (empty), PC_HKDF_HASH_LEN afterwards
    size_t done = 0;
    uint8_t counter = 0;
    while (done < out_len)
    {
        counter++;
        pc_hmac_sha256_ctx ctx;
        pc_hmac_sha256_init(&ctx, prk, PC_HKDF_HASH_LEN);
        pc_hmac_sha256_update(&ctx, t, t_len);
        pc_hmac_sha256_update(&ctx, info, info_len);
        pc_hmac_sha256_update(&ctx, &counter, 1);
        pc_hmac_sha256_final(&ctx, t);
        t_len = PC_HKDF_HASH_LEN;

        size_t take = out_len - done;
        if (take > PC_HKDF_HASH_LEN)
        {
            take = PC_HKDF_HASH_LEN;
        }
        memcpy(out + done, t, take);
        done += take;
    }
}

void pc_hkdf_expand_label_ctx(const uint8_t secret[PC_HKDF_HASH_LEN], const char *label, const uint8_t *context,
                              size_t context_len, uint8_t *out, size_t out_len, const char *label_prefix)
{
    // HkdfLabel (RFC 8446 sec 7.1): uint16 length | opaque label<..> = label_prefix + label | opaque context.
    // The prefix is "tls13 " for TLS/QUIC (RFC 8446) or "dtls13" for DTLS 1.3 (RFC 9147 sec 5.9); the
    // caller supplies whichever applies, so this primitive stays protocol-agnostic. Label length maxes
    // out well under 255 (longest is "tls13 client in" = 15); the context is a Transcript-Hash (<= 32)
    // for Derive-Secret and empty for packet-protection keys. A fixed 2 + 1 + 255 + 1 + 255 scratch
    // buffer covers every caller.
    // Bound the scans: the combined label is opaque<7..255> (RFC 8446 sec 7.1), so cap prefix+label at
    // 255 - this both fits the reserved region below and keeps the single length byte from wrapping,
    // even if a caller ever passed a non-NUL-terminated string.
    size_t prefix_len = strnlen(label_prefix, 255);
    size_t label_len = strnlen(label, 255 - prefix_len);
    uint8_t info[2 + 1 + 255 + 1 + 255];
    size_t p = 0;
    info[p++] = (uint8_t)(out_len >> 8);
    info[p++] = (uint8_t)(out_len & 0xff);
    info[p++] = (uint8_t)(prefix_len + label_len); // full label length, prefix included
    memcpy(info + p, label_prefix, prefix_len);
    p += prefix_len;
    memcpy(info + p, label, label_len);
    p += label_len;
    info[p++] = (uint8_t)context_len;
    if (context_len)
    {
        memcpy(info + p, context, context_len);
        p += context_len;
    }

    hkdf_expand(secret, info, p, out, out_len);
}

void pc_hkdf_expand_label(const uint8_t secret[PC_HKDF_HASH_LEN], const char *label, uint8_t *out, size_t out_len,
                          const char *label_prefix)
{
    pc_hkdf_expand_label_ctx(secret, label, NULL, 0, out, out_len, label_prefix);
}

#endif // PC_ENABLE_HTTP3 || PC_ENABLE_DTLS || PC_TLS_SOFTWARE
