// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file totp.c
 * @brief HOTP (RFC 4226) and TOTP (RFC 6238) over HMAC-SHA-1, and the base32 secret (RFC 4648).
 *
 * HMAC-SHA-1 is RFC 2104 sec 2 H(K XOR opad, H(K XOR ipad, text)) built on the platform's one-shot
 * SHA-1. The text is always the 8-byte counter of RFC 4226 sec 5.2, so every working buffer is a
 * fixed size and stack-local. RFC 4226 sec 5.3 truncates the MAC and reduces it mod 10^Digit;
 * RFC 6238 sec 4.2 supplies the counter as T = (Current Unix time - T0) / X.
 */

#include "services/security/totp/totp.h"
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_TOTP

#include "crypto/hash/sha1.h"

/** @brief B: the block length HMAC pads K out to for SHA-1 (RFC 2104 sec 2). */
#define PROTOCORE_TOTP_HMAC_B 64

/** @brief The counter C occupies 8 bytes, high-order byte first (RFC 4226 sec 5.2). */
#define PROTOCORE_TOTP_C_LEN 8

/**
 * @brief The module's handle onto its own calls - what TotpNs points at.
 *
 * No store member: nothing in this file has static lifetime past the handle itself.
 *
 * @var TotpInternal::ns  the handle a caller sets a call's members on
 */
struct TotpInternal
{
    TotpNs *ns;
};

static struct TotpInternal s_totp = {.ns = &Totp};

// 10^n.
static uint32_t pow10u(uint8_t n)
{
    uint32_t v = 1;
    while (n--)
    {
        v *= 10;
    }
    return v;
}

// RFC 2104 sec 2 H(K XOR opad, H(K XOR ipad, text)) with text the 8-byte counter C: K is zero-padded
// to B bytes, or replaced by H(K) when it is longer than B.
static void hmac_sha1_counter(const uint8_t *key, size_t keylen, const uint8_t c[PROTOCORE_TOTP_C_LEN],
                              uint8_t out[PROTOCORE_SHA1_DIGEST_LEN])
{
    uint8_t k[PROTOCORE_TOTP_HMAC_B] = {0};
    if (keylen > PROTOCORE_TOTP_HMAC_B)
    {
        uint8_t kh[PROTOCORE_SHA1_DIGEST_LEN];
        protocore_sha1(key, keylen, kh);
        mem.cpy(k, kh, PROTOCORE_SHA1_DIGEST_LEN);
    }
    else
    {
        mem.cpy(k, key, keylen);
    }

    uint8_t inner_in[PROTOCORE_TOTP_HMAC_B + PROTOCORE_TOTP_C_LEN];
    for (int i = 0; i < PROTOCORE_TOTP_HMAC_B; i++)
    {
        inner_in[i] = k[i] ^ 0x36; // ipad
    }
    mem.cpy(inner_in + PROTOCORE_TOTP_HMAC_B, c, PROTOCORE_TOTP_C_LEN);
    uint8_t inner[PROTOCORE_SHA1_DIGEST_LEN];
    protocore_sha1(inner_in, sizeof(inner_in), inner);

    uint8_t outer_in[PROTOCORE_TOTP_HMAC_B + PROTOCORE_SHA1_DIGEST_LEN];
    for (int i = 0; i < PROTOCORE_TOTP_HMAC_B; i++)
    {
        outer_in[i] = k[i] ^ 0x5c; // opad
    }
    mem.cpy(outer_in + PROTOCORE_TOTP_HMAC_B, inner, PROTOCORE_SHA1_DIGEST_LEN);
    protocore_sha1(outer_in, sizeof(outer_in), out);
}

// RFC 4226 sec 5.2 HOTP(K,C) = Truncate(HMAC-SHA-1(K,C)): C goes in high-order byte first, DT takes
// the low 4 bits of the last MAC byte as an offset, reads 4 bytes there and masks the top bit, and
// the resulting 31-bit number reduces mod 10^Digit (RFC 4226 sec 5.3).
static uint32_t hotp_value(const uint8_t *key, size_t keylen, uint64_t counter, uint8_t digit)
{
    uint8_t c[PROTOCORE_TOTP_C_LEN];
    for (int i = PROTOCORE_TOTP_C_LEN - 1; i >= 0; i--)
    {
        c[i] = (uint8_t)(counter & 0xFF);
        counter >>= 8;
    }
    uint8_t hs[PROTOCORE_SHA1_DIGEST_LEN];
    hmac_sha1_counter(key, keylen, c, hs);

    int offset = hs[PROTOCORE_SHA1_DIGEST_LEN - 1] & 0x0F;
    uint32_t sbits = ((uint32_t)(hs[offset] & 0x7F) << 24) | ((uint32_t)hs[offset + 1] << 16) |
                     ((uint32_t)hs[offset + 2] << 8) | (uint32_t)hs[offset + 3];
    return sbits % pow10u(digit);
}

// Digit, with 0 taking the RFC 4226 sec 5.3 minimum.
static uint8_t otp_digit(struct TotpInternal *restrict ctx)
{
    return ctx->ns->digit ? ctx->ns->digit : (uint8_t)PROTOCORE_TOTP_DIGIT_MIN;
}

// RFC 6238 sec 4.2 T = (Current Unix time - T0) / X, floored. X of 0 takes the default; a clock
// behind T0 gives step 0.
static uint64_t time_step(struct TotpInternal *restrict ctx)
{
    uint32_t x = ctx->ns->step.x;
    if (x == 0)
    {
        x = PROTOCORE_TOTP_X_DEFAULT;
    }
    if (ctx->ns->step.unix_time < ctx->ns->step.t0)
    {
        return 0;
    }
    return (ctx->ns->step.unix_time - ctx->ns->step.t0) / x;
}

// HOTP(K,C) for the counter the caller set (RFC 4226 sec 5.3).
static void hotp(struct TotpInternal *restrict ctx)
{
    ctx->ns->u32 = hotp_value(ctx->ns->k, ctx->ns->keylen, ctx->ns->step.counter, otp_digit(ctx));
}

// RFC 6238 sec 4.2 TOTP = HOTP(K, T).
static void totp(struct TotpInternal *restrict ctx)
{
    ctx->ns->u32 = hotp_value(ctx->ns->k, ctx->ns->keylen, time_step(ctx), otp_digit(ctx));
}

// RFC 6238 sec 6: match the submitted OTP against T and every step within the drift limit forward
// and backward of it. Steps below the epoch are skipped; a negative drift matches nothing.
static void verify(struct TotpInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    const int32_t drift = ctx->ns->check.drift;
    if (drift < 0)
    {
        return;
    }
    const uint8_t digit = otp_digit(ctx);
    const int64_t t = (int64_t)time_step(ctx);
    for (int32_t w = -drift; w <= drift; w++)
    {
        const int64_t c = t + w;
        if (c < 0)
        {
            continue;
        }
        if (hotp_value(ctx->ns->k, ctx->ns->keylen, (uint64_t)c, digit) == ctx->ns->check.otp)
        {
            ctx->ns->ok = PROTO_TRUE;
            return;
        }
    }
}

// RFC 4648 sec 6 base32 to bytes, 5 bits per character, most significant bit first. A-Z carry 0..25
// and 2-7 carry 26..31; lowercase carries the same values as uppercase, and '=', ' ' and '-' are
// skipped rather than rejected under RFC 4648 sec 3.3. Any other character, or a byte past cap,
// ends the decode at -1.
static void base32_decode(struct TotpInternal *restrict ctx)
{
    const char *b32 = ctx->ns->secret.b32;
    uint8_t *out = ctx->ns->secret.out;
    const size_t cap = ctx->ns->secret.cap;

    ctx->ns->i32 = -1;
    if (!b32 || !out)
    {
        return;
    }
    uint32_t buffer = 0;
    int bits = 0;
    size_t n = 0;
    for (const char *p = b32; *p; p++)
    {
        char ch = *p;
        int val;
        if (ch >= 'A' && ch <= 'Z')
        {
            val = ch - 'A';
        }
        else if (ch >= 'a' && ch <= 'z')
        {
            val = ch - 'a';
        }
        else if (ch >= '2' && ch <= '7')
        {
            val = ch - '2' + 26;
        }
        else if (ch == '=' || ch == ' ' || ch == '-')
        {
            continue;
        }
        else
        {
            return;
        }
        buffer = (buffer << 5) | (uint32_t)val;
        bits += 5;
        if (bits >= 8)
        {
            bits -= 8;
            if (n >= cap)
            {
                return;
            }
            out[n++] = (uint8_t)((buffer >> bits) & 0xFF);
        }
    }
    ctx->ns->i32 = (int32_t)n;
}

// Designated, so a member's position in the struct does not decide what it binds to.
TotpNs Totp = {.hotp = hotp, .totp = totp, .verify = verify, .base32_decode = base32_decode, .internal = &s_totp};

#endif // PROTOCORE_ENABLE_TOTP
