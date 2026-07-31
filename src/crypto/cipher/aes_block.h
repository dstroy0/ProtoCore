// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file aes_block.h
 * @brief Compact table-free software AES key schedule + single-block encrypt (FIPS 197) - one source of truth.
 *
 * The SSH AES-256-GCM, SSH AES-256-CTR, and QUIC/TLS AES-128 modules each carried a byte-for-byte copy of the
 * same software AES (key expansion + the SubBytes/ShiftRows/MixColumns/AddRoundKey block cipher). They differ
 * ONLY in the key size: parameterize on @c nk (key words: 4 for AES-128, 8 for AES-256) and @c nr (rounds: 10
 * or 14) and one implementation serves all three.
 *
 * Only the S-box (@c PC_AES_SBOX from aes_sbox.h) and the GF(2^8) @c xtime are used - no large T-tables - so
 * this is the same "constant-time by structure" shape the copies had (a table-indexed S-box; no secret-
 * dependent branching introduced here). Header-only and pure (S-box + @c <string.h>), so it is compiled into
 * only the NATIVE software path of each module; the @c \#ifdef ARDUINO mbedTLS/HW branches are untouched.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_AES_BLOCK_H
#define PROTOCORE_AES_BLOCK_H

#include "crypto/cipher/aes_sbox.h" // PC_AES_SBOX
#include <stdint.h>
#include <string.h>

/** @brief GF(2^8) multiply-by-2 (xtime) for the AES MixColumns step. */
inline uint8_t pc_aes_xtime(uint8_t a)
{
    return (uint8_t)((a << 1) ^ ((a >> 7) ? 0x1bu : 0x00u));
}

/** @brief AES SubWord (FIPS 197 sec 5.2): apply the S-box to each of the four bytes of a 32-bit word. */
inline uint32_t pc_aes_sub_word(uint32_t w)
{
    return ((uint32_t)PC_AES_SBOX[w >> 24] << 24) | ((uint32_t)PC_AES_SBOX[(w >> 16) & 0xff] << 16) |
           ((uint32_t)PC_AES_SBOX[(w >> 8) & 0xff] << 8) | (uint32_t)PC_AES_SBOX[w & 0xff];
}

/** @brief AES RotWord (FIPS 197 sec 5.2): cyclically rotate a 32-bit word one byte left. */
inline uint32_t pc_aes_rot_word(uint32_t w)
{
    return (w << 8) | (w >> 24);
}

/**
 * @brief AES key expansion (FIPS 197 sec 5.2). @p nk key words (4=AES-128, 8=AES-256); @p rk receives
 *        4*(@p nk + 7) round-key words (44 for AES-128, 60 for AES-256).
 */
inline void pc_aes_key_expand(const uint8_t *key, int nk, uint32_t *rk)
{
    // Rcon[1..10] (index 0 unused); AES-128 uses up to [10], AES-256 up to [7].
    static const uint8_t RCON[11] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36};
    for (int i = 0; i < nk; i++)
    {
        rk[i] = ((uint32_t)key[4 * i] << 24) | ((uint32_t)key[4 * i + 1] << 16) | ((uint32_t)key[4 * i + 2] << 8) |
                (uint32_t)key[4 * i + 3];
    }

    int total = 4 * (nk + 7);
    for (int i = nk; i < total; i++)
    {
        uint32_t t = rk[i - 1];
        if (i % nk == 0)
        {
            t = pc_aes_sub_word(pc_aes_rot_word(t)) ^ ((uint32_t)RCON[i / nk] << 24);
        }
        else if (nk > 6 && i % nk == 4) // AES-256 applies an extra SubWord at the mid-point of each 8-word run.
        {
            t = pc_aes_sub_word(t);
        }
        rk[i] = rk[i - nk] ^ t;
    }
}

/**
 * @brief AES single-block encrypt (FIPS 197 sec 5.1), @p nr rounds (10=AES-128, 14=AES-256). State is
 *        column-major: s[col*4 + row]. @p rk is the schedule from pc_aes_key_expand.
 */
inline void pc_aes_encrypt_block(const uint32_t *rk, int nr, const uint8_t in[16], uint8_t out[16])
{
    uint8_t s[16];
    for (int i = 0; i < 16; i++)
    {
        s[i] = in[i] ^ (uint8_t)(rk[i / 4] >> (24 - (i % 4) * 8));
    }

    for (int r = 1; r < nr; r++)
    {
        for (int i = 0; i < 16; i++)
        {
            s[i] = PC_AES_SBOX[s[i]];
        }

        uint8_t t;
        t = s[1]; // row 1 <<< 1
        s[1] = s[5];
        s[5] = s[9];
        s[9] = s[13];
        s[13] = t;
        t = s[2]; // row 2 <<< 2
        s[2] = s[10];
        s[10] = t;
        t = s[6];
        s[6] = s[14];
        s[14] = t;
        t = s[15]; // row 3 <<< 3
        s[15] = s[11];
        s[11] = s[7];
        s[7] = s[3];
        s[3] = t;

        for (int c = 0; c < 4; c++)
        {
            uint8_t a = s[c * 4];
            uint8_t b = s[c * 4 + 1];
            uint8_t cc = s[c * 4 + 2];
            uint8_t d = s[c * 4 + 3];
            uint8_t e = a ^ b ^ cc ^ d;
            s[c * 4] = a ^ e ^ pc_aes_xtime(a ^ b);
            s[c * 4 + 1] = b ^ e ^ pc_aes_xtime(b ^ cc);
            s[c * 4 + 2] = cc ^ e ^ pc_aes_xtime(cc ^ d);
            s[c * 4 + 3] = d ^ e ^ pc_aes_xtime(d ^ a);
        }

        for (int i = 0; i < 16; i++)
        {
            s[i] ^= (uint8_t)(rk[r * 4 + i / 4] >> (24 - (i % 4) * 8));
        }
    }

    for (int i = 0; i < 16; i++)
    {
        s[i] = PC_AES_SBOX[s[i]];
    }

    uint8_t t;
    t = s[1];
    s[1] = s[5];
    s[5] = s[9];
    s[9] = s[13];
    s[13] = t;
    t = s[2];
    s[2] = s[10];
    s[10] = t;
    t = s[6];
    s[6] = s[14];
    s[14] = t;
    t = s[15];
    s[15] = s[11];
    s[11] = s[7];
    s[7] = s[3];
    s[3] = t;

    for (int i = 0; i < 16; i++)
    {
        s[i] ^= (uint8_t)(rk[nr * 4 + i / 4] >> (24 - (i % 4) * 8));
    }

    memcpy(out, s, 16);
}

#endif // PROTOCORE_AES_BLOCK_H
