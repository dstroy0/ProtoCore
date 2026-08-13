// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file totp.c
 * @brief HMAC-SHA1 HOTP/TOTP (RFC 4226 / 6238) + base32 decode (pure).
 *
 * HMAC-SHA1 is built on the existing one-shot software SHA-1; the TOTP message is
 * the 8-byte counter, so all working buffers are fixed and stack-local.
 */

#include "services/security/totp/totp.h"
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_TOTP

#include "crypto/hash/sha1.h"

#define PROTOCORE_BLOCK 64 // SHA-1 block size

// HMAC-SHA1 over an 8-byte message (the HOTP/TOTP counter).
static void protocore_hmac_sha1_8(const uint8_t *key, size_t keylen, const uint8_t msg[8],
                                  uint8_t out[PROTOCORE_SHA1_DIGEST_LEN])
{
    uint8_t k[PROTOCORE_BLOCK] = {0};
    if (keylen > PROTOCORE_BLOCK)
    {
        uint8_t kh[PROTOCORE_SHA1_DIGEST_LEN];
        protocore_sha1(key, keylen, kh);
        mem.cpy(k, kh, PROTOCORE_SHA1_DIGEST_LEN);
    }
    else
    {
        mem.cpy(k, key, keylen);
    }

    uint8_t inner_in[PROTOCORE_BLOCK + 8];
    for (int i = 0; i < PROTOCORE_BLOCK; i++)
    {
        inner_in[i] = k[i] ^ 0x36; // ipad
    }
    mem.cpy(inner_in + PROTOCORE_BLOCK, msg, 8);
    uint8_t inner[PROTOCORE_SHA1_DIGEST_LEN];
    protocore_sha1(inner_in, sizeof(inner_in), inner);

    uint8_t outer_in[PROTOCORE_BLOCK + PROTOCORE_SHA1_DIGEST_LEN];
    for (int i = 0; i < PROTOCORE_BLOCK; i++)
    {
        outer_in[i] = k[i] ^ 0x5c; // opad
    }
    mem.cpy(outer_in + PROTOCORE_BLOCK, inner, PROTOCORE_SHA1_DIGEST_LEN);
    protocore_sha1(outer_in, sizeof(outer_in), out);
}

static uint32_t pow10u(uint8_t n)
{
    uint32_t v = 1;
    while (n--)
    {
        v *= 10;
    }
    return v;
}

uint32_t protocore_hotp(const uint8_t *key, size_t keylen, uint64_t counter, uint8_t digits)
{
    uint8_t msg[8];
    for (int i = 7; i >= 0; i--)
    {
        msg[i] = (uint8_t)(counter & 0xFF);
        counter >>= 8;
    }
    uint8_t mac[PROTOCORE_SHA1_DIGEST_LEN];
    protocore_hmac_sha1_8(key, keylen, msg, mac);

    int off = mac[PROTOCORE_SHA1_DIGEST_LEN - 1] & 0x0F; // dynamic truncation (RFC 4226 §5.3)
    uint32_t bin = ((uint32_t)(mac[off] & 0x7F) << 24) | ((uint32_t)mac[off + 1] << 16) |
                   ((uint32_t)mac[off + 2] << 8) | (uint32_t)mac[off + 3];
    return bin % pow10u(digits);
}

uint32_t protocore_totp(const uint8_t *key, size_t keylen, uint64_t unix_time, uint32_t period, uint8_t digits)
{
    if (period == 0)
    {
        period = 30;
    }
    return protocore_hotp(key, keylen, unix_time / period, digits);
}

proto_bool protocore_totp_verify(const uint8_t *key, size_t keylen, uint64_t unix_time, uint32_t code, uint32_t period,
                                 uint8_t digits, int window)
{
    if (period == 0)
    {
        period = 30;
    }
    int64_t step = (int64_t)(unix_time / period);
    for (int w = -window; w <= window; w++)
    {
        int64_t c = step + w;
        if (c < 0)
        {
            continue;
        }
        if (protocore_hotp(key, keylen, (uint64_t)c, digits) == code)
        {
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

int protocore_base32_decode(const char *b32, uint8_t *out, size_t cap)
{
    if (!b32 || !out)
    {
        return -1;
    }
    uint32_t buffer = 0;
    int bits = 0;
    size_t n = 0;
    for (const char *p = b32; *p; p++)
    {
        char c = *p;
        int val;
        if (c >= 'A' && c <= 'Z')
        {
            val = c - 'A';
        }
        else if (c >= 'a' && c <= 'z')
        {
            val = c - 'a';
        }
        else if (c >= '2' && c <= '7')
        {
            val = c - '2' + 26;
        }
        else if (c == '=' || c == ' ' || c == '-')
        {
            continue; // padding / cosmetic separators
        }
        else
        {
            return -1; // invalid base32 character
        }
        buffer = (buffer << 5) | (uint32_t)val;
        bits += 5;
        if (bits >= 8)
        {
            bits -= 8;
            if (n >= cap)
            {
                return -1;
            }
            out[n++] = (uint8_t)((buffer >> bits) & 0xFF);
        }
    }
    return (int)n;
}

#endif // PROTOCORE_ENABLE_TOTP
