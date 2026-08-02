// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_snmp_crypto.cpp
 * @brief USM key localization (SHA-256) + AES-128-CFB implementation.
 */

#include "services/net/snmp/snmp_crypto.h"
#include "mmgr/secure.h"

#if PC_ENABLE_SNMP_V3

#include "crypto/cipher/aes_sbox.h"
#include "crypto/hash/sha256.h"
#include <string.h>

// ---------------------------------------------------------------------------
// RFC 3414 §2.6 key localization (SHA-256)
// ---------------------------------------------------------------------------

// RFC 3414 passphrases are >= 8 chars with no formal upper bound; cap the localization input
// defensively (well above any real passphrase) so a non-terminated password cannot over-read.
#define PC_SNMP_USM_PASS_MAX 256

void pc_snmp_usm_localize_key(const char *password, const uint8_t *engine_id, size_t engine_id_len,
                              uint8_t key_out[SNMP_USM_KEY_LEN])
{
    size_t pwlen = password ? strnlen(password, PC_SNMP_USM_PASS_MAX) : 0;
    if (pwlen == 0)
    {
        memset(key_out, 0, SNMP_USM_KEY_LEN);
        return;
    }

    // Ku = SHA-256( password repeated to exactly 1 048 576 bytes ).
    pc_sha256_ctx ctx;
    pc_sha256_init(&ctx);
    uint8_t block[64];
    size_t pw_index = 0;
    uint32_t count = 0;
    while (count < 1048576u)
    {
        for (int i = 0; i < 64; i++)
        {
            block[i] = (uint8_t)password[pw_index];
            pw_index = (pw_index + 1) % pwlen;
        }
        pc_sha256_update(&ctx, block, 64);
        count += 64;
    }
    uint8_t ku[SNMP_USM_KEY_LEN];
    pc_sha256_final(&ctx, ku);

    // Kul = SHA-256( Ku || engineID || Ku ).
    pc_sha256_init(&ctx);
    pc_sha256_update(&ctx, ku, SNMP_USM_KEY_LEN);
    pc_sha256_update(&ctx, engine_id, engine_id_len);
    pc_sha256_update(&ctx, ku, SNMP_USM_KEY_LEN);
    pc_sha256_final(&ctx, key_out);

    pc_secure_wipe(ku, sizeof(ku));
    pc_secure_wipe(block, sizeof(block));
}

// ---------------------------------------------------------------------------
// AES-128 (FIPS-197) block encrypt + CFB-128 mode
// ---------------------------------------------------------------------------

static const uint8_t kRcon[10] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36};

// Expand a 128-bit key into 11 round keys (44 words).
static void aes128_key_schedule(const uint8_t key[16], uint8_t rk[176])
{
    memcpy(rk, key, 16);
    for (int i = 4; i < 44; i++)
    {
        uint8_t t[4];
        memcpy(t, rk + (i - 1) * 4, 4);
        if (i % 4 == 0)
        {
            uint8_t tmp = t[0]; // RotWord
            t[0] = PC_AES_SBOX[t[1]];
            t[1] = PC_AES_SBOX[t[2]];
            t[2] = PC_AES_SBOX[t[3]];
            t[3] = PC_AES_SBOX[tmp];
            t[0] ^= kRcon[i / 4 - 1];
        }
        for (int j = 0; j < 4; j++)
        {
            rk[i * 4 + j] = rk[(i - 4) * 4 + j] ^ t[j];
        }
    }
}

static uint8_t xtime(uint8_t x)
{
    return (uint8_t)((x << 1) ^ ((x >> 7) * 0x1b));
}

static void aes128_encrypt_block(const uint8_t rk[176], const uint8_t in[16], uint8_t out[16])
{
    uint8_t s[16];
    for (int i = 0; i < 16; i++)
    {
        s[i] = in[i] ^ rk[i];
    }

    for (int round = 1; round <= 10; round++)
    {
        // SubBytes
        for (int i = 0; i < 16; i++)
        {
            s[i] = PC_AES_SBOX[s[i]];
        }

        // ShiftRows (state is column-major: s[r + 4c])
        uint8_t t[16];
        for (int r = 0; r < 4; r++)
        {
            for (int c = 0; c < 4; c++)
            {
                t[r + 4 * c] = s[r + 4 * ((c + r) % 4)];
            }
        }
        memcpy(s, t, 16);

        // MixColumns (skip in the final round)
        if (round != 10)
        {
            for (int c = 0; c < 4; c++)
            {
                uint8_t *col = s + 4 * c;
                uint8_t a0 = col[0];
                uint8_t a1 = col[1];
                uint8_t a2 = col[2];
                uint8_t a3 = col[3];
                col[0] = (uint8_t)(xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3);
                col[1] = (uint8_t)(a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3);
                col[2] = (uint8_t)(a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3));
                col[3] = (uint8_t)((xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3));
            }
        }

        // AddRoundKey
        for (int i = 0; i < 16; i++)
        {
            s[i] ^= rk[round * 16 + i];
        }
    }
    memcpy(out, s, 16);
}

void pc_snmp_aes128_cfb(const uint8_t key[16], const uint8_t iv[16], const uint8_t *in, uint8_t *out, size_t len,
                        proto_bool encrypt)
{
    uint8_t rk[176];
    aes128_key_schedule(key, rk);
    uint8_t fb[16];
    memcpy(fb, iv, 16);
    uint8_t ks[16];

    size_t off = 0;
    while (off < len)
    {
        aes128_encrypt_block(rk, fb, ks);
        size_t bl = len - off;
        if (bl > 16)
        {
            bl = 16;
        }
        // Ciphertext feedback must be captured before an in-place XOR overwrites
        // the input (matters when out == in).
        uint8_t cipher[16];
        if (encrypt)
        {
            for (size_t j = 0; j < bl; j++)
            {
                out[off + j] = in[off + j] ^ ks[j];
                cipher[j] = out[off + j];
            }
        }
        else
        {
            for (size_t j = 0; j < bl; j++)
            {
                cipher[j] = in[off + j];
                out[off + j] = in[off + j] ^ ks[j];
            }
        }
        if (bl == 16)
        {
            memcpy(fb, cipher, 16);
        }
        off += bl;
    }

    pc_secure_wipe(rk, sizeof(rk));
    pc_secure_wipe(ks, sizeof(ks));
    pc_secure_wipe(fb, sizeof(fb));
}

#endif // PC_ENABLE_SNMP_V3
