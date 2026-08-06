// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file base64.c
 * @brief Base64 encoder/decoder implementation.
 *
 * **Encode** is a single portable software codec (RFC 4648) on every target: it is byte-identical to
 * mbedTLS's but ~20x faster on the ESP32-S3 (docs/FEATURE_PERFORMANCE.md section 2), and it only ever
 * encodes public data here (the SHA-1 in the WebSocket accept), so a data-dependent table lookup is fine.
 *
 * **Decode** is one portable **constant-time** codec on every target (no mbedTLS delegation, no separate
 * native path). It is the only base64 path that touches a secret - the Basic-auth credential (RFC 7617) and
 * the JWT / JWS segments (RFC 7515, via base64url) - so the character -> value mapping is evaluated with
 * branchless arithmetic range masks: no data-dependent branch and no data-indexed table, so neither the
 * timing nor the cache footprint depends on the secret bytes. Padding / length handling may branch (the
 * plaintext length mod 3 is public, not a secret). This both removes the mbedTLS base64 dependency and
 * closes the timing gap on the base64url (JWT) path, which was previously a plain branchy software decoder.
 */

#include "base64.h"

#include "mmgr/swar.h"        // the lane math; the classification below is base64's own
#include "protocore_config.h" // PC_BASE64_SWAR (scalar vs SWAR constant-time decode; default SWAR)
                              // strnlen

static const char B64_TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void pc_base64_encode(const uint8_t *src, size_t src_len, char *dst)
{
    size_t i = 0;
    size_t j = 0;

    // Full groups: pack 3 input bytes into a 24-bit value, emit as 4 x 6-bit
    // characters (most-significant 6 bits first).
    while (i + 2 < src_len)
    {
        uint32_t v = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1] << 8) | (uint32_t)src[i + 2];
        dst[j++] = B64_TABLE[(v >> 18) & 0x3F];
        dst[j++] = B64_TABLE[(v >> 12) & 0x3F];
        dst[j++] = B64_TABLE[(v >> 6) & 0x3F];
        dst[j++] = B64_TABLE[(v) & 0x3F];
        i += 3;
    }

    // Tail of 1 or 2 leftover bytes: emit 2 or 3 characters, '='-padded to 4.
    if (i < src_len)
    {
        uint32_t v = (uint32_t)src[i] << 16;
        if (i + 1 < src_len)
        {
            v |= (uint32_t)src[i + 1] << 8;
        }

        dst[j++] = B64_TABLE[(v >> 18) & 0x3F];
        dst[j++] = B64_TABLE[(v >> 12) & 0x3F];
        dst[j++] = (i + 1 < src_len) ? B64_TABLE[(v >> 6) & 0x3F] : '='; // 3rd char only if 2 input bytes
        dst[j++] = '=';
    }

    dst[j] = '\0';
}

// ---------------------------------------------------------------------------
// Constant-time decode (RFC 4648). Decode is the one base64 path that touches a secret - the Basic-auth
// credential (RFC 7617) and, via base64url below, the JWT / JWS segments (RFC 7515) - so the
// character -> value mapping must not leak the bytes through a data-dependent branch or a data-indexed
// table lookup (either makes the timing / cache footprint depend on the secret). Every alphabet range is
// evaluated with branchless arithmetic masks, so a given input length always costs the same regardless of
// the byte values. One portable codec on every target (no mbedTLS delegation, no separate native path).
// Padding / length handling may branch: the plaintext length mod 3 is public (visible on the wire), not a
// secret byte value. Verified byte-exact vs RFC 4648 vectors + the reference decoder over a random corpus;
// timing-invariance measured on the ESP32-S3 (CCOUNT does not vary with the input bytes).
// ---------------------------------------------------------------------------

// x, lo, hi are byte values (< 256), so a byte-domain subtraction's bit 31 is the borrow flag. Each helper
// returns an all-ones (0xFFFFFFFF) or all-zero mask with no branch and no memory access.
static inline uint32_t ct_ge(uint32_t x, uint32_t lo)
{
    return ((x - lo) >> 31) - 1u; // x >= lo  <=>  no borrow  <=>  bit 31 clear
}
static inline uint32_t ct_le(uint32_t x, uint32_t hi)
{
    return ((hi - x) >> 31) - 1u;
}
static inline uint32_t ct_eq(uint32_t x, uint32_t y)
{
    return ct_ge(x, y) & ct_le(x, y);
}
static inline uint32_t ct_is_zero(uint32_t x)
{
    return 0u - ((x - 1u) >> 31); // x==0 -> (0-1)>>31 == 1 -> all-ones; x>=1 -> 0
}

// Map one character to (6-bit value + 1), or 0 if it is not in the alphabet. The +1 bias distinguishes 'A'
// (value 0) from "no match" (0) without a branch. Exactly one range matches, so the ORed masked terms
// collapse to that term. `urlsafe` picks the '-' '_' alphabet (RFC 4648 sec 5) over '+' '/'; it is a public
// per-call constant, never a secret, so selecting on it leaks nothing.
static inline uint32_t ct_b64_val_plus1(uint32_t c, int urlsafe)
{
    uint32_t plus = urlsafe ? (uint32_t)'-' : (uint32_t)'+';
    uint32_t slash = urlsafe ? (uint32_t)'_' : (uint32_t)'/';
    uint32_t v = 0;
    v |= ct_ge(c, 'A') & ct_le(c, 'Z') & (c - 'A' + 1u);  // 1..26
    v |= ct_ge(c, 'a') & ct_le(c, 'z') & (c - 'a' + 27u); // 27..52
    v |= ct_ge(c, '0') & ct_le(c, '9') & (c - '0' + 53u); // 53..62
    v |= ct_eq(c, plus) & 63u;                            // value 62 -> +1 = 63
    v |= ct_eq(c, slash) & 64u;                           // value 63 -> +1 = 64
    return v;
}

#if PC_BASE64_SWAR
// ---------------------------------------------------------------------------
// SWAR variant: classify 4 characters per 32-bit word instead of one at a time (opt-in, PC_BASE64_SWAR).
// The lane math itself is mmgr/swar.h; what is base64's own is the classification below.
// Every base64 character is < 0x80, so a byte lane never sets its own high bit, which is what lets the
// guard-bit subtraction keep borrows inside their lane and the range masks stay data-independent -
// same constant-time property as the scalar path, four lanes at once. Whether this actually wins is a HW
// measurement, not an assumption (decode is once-per-request); see docs/FEATURE_PERFORMANCE.md.
// ---------------------------------------------------------------------------

// A base64 quad is four characters by definition of the encoding, so this path wants exactly four
// lanes - it is the one caller of swar.h that cannot follow a retyped word width.
static_assert(PC_SWAR_BYTES == 4, "base64 SWAR decodes one four-character quad per word");

// Decode 4 packed characters (c0 in the low byte) to 4 packed 6-bit values; *ok gets 0xFF in each valid lane.
static inline uint32_t swar_quad(uint32_t a, uint32_t *ok)
{
    uint32_t mAZ = pc_swar_spread(pc_swar_ge(a, 'A') & pc_swar_le(a, 'Z'));
    uint32_t maz = pc_swar_spread(pc_swar_ge(a, 'a') & pc_swar_le(a, 'z'));
    uint32_t m09 = pc_swar_spread(pc_swar_ge(a, '0') & pc_swar_le(a, '9'));
    uint32_t mpl = pc_swar_spread(pc_swar_ge(a, '+') & pc_swar_le(a, '+'));
    uint32_t msl = pc_swar_spread(pc_swar_ge(a, '/') & pc_swar_le(a, '/'));
    uint32_t val = (mAZ & (pc_swar_sub7(a, 'A') + 0u * PC_SWAR_ONES)) |
                   (maz & (pc_swar_sub7(a, 'a') + 26u * PC_SWAR_ONES)) |
                   (m09 & (pc_swar_sub7(a, '0') + 52u * PC_SWAR_ONES)) | (mpl & (62u * PC_SWAR_ONES)) |
                   (msl & (63u * PC_SWAR_ONES));
    *ok = mAZ | maz | m09 | mpl | msl;
    return val;
}

static size_t pc_base64_decode(const char *src, uint8_t *dst, size_t dst_cap)
{
    size_t src_len = strnlen(src, ((dst_cap + 2) / 3) * 4 + 4);
    if (src_len == 0 || (src_len & 3u) != 0)
    {
        return 0;
    }
    size_t out = 0;
    uint32_t bad = 0;
    size_t nquads = src_len / 4;
    for (size_t q = 0; q < nquads; q++)
    {
        size_t i = q * 4;
        uint32_t c0 = (uint8_t)src[i];
        uint32_t c1 = (uint8_t)src[i + 1];
        uint32_t c2 = (uint8_t)src[i + 2];
        uint32_t c3 = (uint8_t)src[i + 3];
        int is_last = (q + 1 == nquads);
        int p2 = is_last && (c2 == '=');
        int p3 = is_last && (c3 == '=');
        if (p2 && !p3)
        {
            return 0;
        }
        // A padded final quad: swap each '=' for 'A' (value 0, always valid) so the lane classifies, then
        // drop the matching output byte. Position is public length info, so this branch leaks no secret.
        uint32_t word = c0 | (c1 << 8) | ((p2 ? (uint32_t)'A' : c2) << 16) | ((p3 ? (uint32_t)'A' : c3) << 24);
        uint32_t ok;
        uint32_t val = swar_quad(word, &ok);
        bad |= ~ok; // any invalid lane sets high bits here
        uint32_t a = val & 0xFF;
        uint32_t b = (val >> 8) & 0xFF;
        uint32_t c = (val >> 16) & 0xFF;
        uint32_t d = (val >> 24) & 0xFF;
        if (out >= dst_cap)
        {
            return 0;
        }
        dst[out++] = (uint8_t)((a << 2) | (b >> 4));
        if (!p2)
        {
            if (out >= dst_cap)
            {
                return 0;
            }
            dst[out++] = (uint8_t)((b << 4) | (c >> 2));
        }
        if (!p3)
        {
            if (out >= dst_cap)
            {
                return 0;
            }
            dst[out++] = (uint8_t)((c << 6) | d);
        }
    }
    if (bad)
    {
        return 0;
    }
    return out;
}
#else
static size_t pc_base64_decode(const char *src, uint8_t *dst, size_t dst_cap)
{
    // Bounded length (a missing NUL cannot run past what dst_cap could ever hold). Canonical base64 is
    // whole 4-character quads.
    size_t src_len = strnlen(src, ((dst_cap + 2) / 3) * 4 + 4);
    if (src_len == 0 || (src_len & 3u) != 0)
    {
        return 0;
    }

    size_t out = 0;
    uint32_t bad = 0; // OR of per-character invalidity masks; a single check at the end (no early leak)

    for (size_t i = 0; i < src_len; i += 4)
    {
        uint32_t c0 = (uint8_t)src[i + 0];
        uint32_t c1 = (uint8_t)src[i + 1];
        uint32_t c2 = (uint8_t)src[i + 2];
        uint32_t c3 = (uint8_t)src[i + 3];

        // '=' padding is legal only as the last 1-2 characters of the final quad. Position is public length
        // information, so this branch reveals nothing about the secret bytes.
        int is_last = (i + 4 == src_len);
        int p2 = is_last && (c2 == '=');
        int p3 = is_last && (c3 == '=');
        if (p2 && !p3)
        {
            return 0; // "xx=y" - a lone pad must sit in the 4th position, not the 3rd
        }

        uint32_t v0 = ct_b64_val_plus1(c0, 0);
        uint32_t v1 = ct_b64_val_plus1(c1, 0);
        uint32_t v2 = ct_b64_val_plus1(c2, 0);
        uint32_t v3 = ct_b64_val_plus1(c3, 0);

        // c0/c1 are always alphabet chars; c2/c3 may be padding (only in the final quad).
        bad |= ct_is_zero(v0);
        bad |= ct_is_zero(v1);
        bad |= p2 ? 0u : ct_is_zero(v2);
        bad |= p3 ? 0u : ct_is_zero(v3);

        uint32_t a = (v0 - 1u) & 0x3Fu;
        uint32_t b = (v1 - 1u) & 0x3Fu;
        uint32_t c = p2 ? 0u : ((v2 - 1u) & 0x3Fu);
        uint32_t d = p3 ? 0u : ((v3 - 1u) & 0x3Fu);

        // Reassemble the 24-bit group; padding trims the tail. Every write is bounded by dst_cap.
        if (out >= dst_cap)
        {
            return 0;
        }
        dst[out++] = (uint8_t)((a << 2) | (b >> 4));
        if (!p2)
        {
            if (out >= dst_cap)
            {
                return 0;
            }
            dst[out++] = (uint8_t)((b << 4) | (c >> 2));
        }
        if (!p3)
        {
            if (out >= dst_cap)
            {
                return 0;
            }
            dst[out++] = (uint8_t)((c << 6) | d);
        }
    }

    if (bad)
    {
        return 0;
    }
    return out;
}
#endif // PC_BASE64_SWAR

// ---------------------------------------------------------------------------
// Base64url (RFC 4648 section 5): '-' / '_' replace '+' / '/', no '=' padding.
// Shared by JWT (HS256) and OIDC (RS256) so the alphabet lives in one place.
// Platform-independent: encode builds on pc_base64_encode then rewrites the two
// differing characters in place; decode is a direct streaming decoder that
// accepts the URL and standard alphabets and stops at '=', so it needs no temp
// buffer or re-padding.
// ---------------------------------------------------------------------------

static size_t pc_base64url_encode(const uint8_t *src, size_t src_len, char *dst)
{
    pc_base64_encode(src, src_len, dst); // standard base64, '='-padded, NUL-terminated
    size_t n = 0;
    for (size_t i = 0; dst[i]; i++)
    {
        char c = dst[i];
        if (c == '=')
        {
            break; // base64url carries no padding
        }
        if (c == '+')
        {
            dst[i] = '-';
        }
        else if (c == '/')
        {
            dst[i] = '_';
        }
        n = i + 1;
    }
    dst[n] = '\0';
    return n;
}

// Constant-time base64url decode (RFC 4648 sec 5, '-'/'_'), used by JWT / JWS (RFC 7515) and OIDC - a
// secret path, so it shares the branchless classifier above (the standard '+'/'/' are rejected here). The
// only difference from pc_base64_decode is framing: base64url carries no padding, and the final group may
// be 2 or 3 characters (an unbounded streaming decode rather than whole quads).
size_t pc_base64url_decode(const char *src, size_t src_len, uint8_t *dst, size_t dst_cap)
{
    size_t o = 0;
    uint32_t acc = 0;
    int bits = 0;
    uint32_t bad = 0; // branchless invalidity accumulator; one check at the end (no per-character leak)
    for (size_t i = 0; i < src_len; i++)
    {
        uint32_t ch = (uint8_t)src[i];
        if (ch == '=')
        {
            break; // optional padding ends the input (public length information)
        }
        uint32_t vp = ct_b64_val_plus1(ch, 1);
        bad |= ct_is_zero(vp);
        acc = (acc << 6) | ((vp - 1u) & 0x3Fu);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            if (o >= dst_cap)
            {
                return 0;
            }
            dst[o++] = (uint8_t)((acc >> bits) & 0xFF);
        }
    }
    if (bad)
    {
        return 0;
    }
    return o;
}

const Base64Ns Base64 = {pc_base64_encode, pc_base64_decode, pc_base64url_encode, pc_base64url_decode};
