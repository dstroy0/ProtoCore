// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_AES_BLOCK_H
#define PROTOCORE_AES_BLOCK_H

#include "crypto/cipher/aes_sbox/aes_sbox.h"
#include "mmgr/protomem/protomem.h"
#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file aes_block.h
 * @brief Table-free software AES key schedule and single-block encrypt (FIPS 197).
 *
 * The shared software AES primitive for the whole library: the key expansion of FIPS 197 sec 5.2 and
 * the SubBytes/ShiftRows/MixColumns/AddRoundKey block of sec 5.1, parameterized on @c nk (key words: 4
 * for AES-128, 8 for AES-256) and @c nr (rounds: 10 or 14), so one implementation serves both key
 * sizes. The software arms of AES-256-CTR, AES-256-GCM, AES-CCM and AES-CMAC run on it.
 *
 * Only the S-box (@c PROTOCORE_AES_SBOX from aes_sbox.h) and the GF(2^8) @c xtime are used, no large
 * T-tables: a table-indexed S-box and no secret-dependent branch. The five functions below stay inline
 * in this header, where the per-block loops of those arms call them; ::AesBlock reaches the same key
 * expansion and the same block through the namespace.
 *
 * The round-key schedule and the block are the CALLER's: both entries write into the buffers its args
 * name and hold neither past the call. @c work arrives goes unread: nothing is carried
 * from one call to the next, so this module states no borrow and neither takes those bytes, holds
 * them, releases them, nor wipes them.
 *
 * The five functions above are the same key expansion and the same block plus the three one-liners
 * they are built from, reached without the namespace, for a caller whose per-block loop inlines them.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

/** @brief GF(2^8) multiply-by-2 (xtime) for the AES MixColumns step. */
PROTOCORE_INLINE uint8_t protocore_aes_xtime(uint8_t a)
{
    return (uint8_t)((a << 1) ^ ((a >> 7) ? 0x1bu : 0x00u));
}

/** @brief AES SubWord (FIPS 197 sec 5.2): apply the S-box to each of the four bytes of a 32-bit word. */
PROTOCORE_INLINE uint32_t protocore_aes_sub_word(uint32_t w)
{
    return ((uint32_t)PROTOCORE_AES_SBOX[w >> 24] << 24) | ((uint32_t)PROTOCORE_AES_SBOX[(w >> 16) & 0xff] << 16) |
           ((uint32_t)PROTOCORE_AES_SBOX[(w >> 8) & 0xff] << 8) | (uint32_t)PROTOCORE_AES_SBOX[w & 0xff];
}

/** @brief AES RotWord (FIPS 197 sec 5.2): cyclically rotate a 32-bit word one byte left. */
PROTOCORE_INLINE uint32_t protocore_aes_rot_word(uint32_t w)
{
    return (w << 8) | (w >> 24);
}

/**
 * @brief AES key expansion (FIPS 197 sec 5.2). @p nk key words (4=AES-128, 8=AES-256); @p rk receives
 *        4*(@p nk + 7) round-key words (44 for AES-128, 60 for AES-256).
 */
PROTOCORE_INLINE void protocore_aes_key_expand(const uint8_t *key, int nk, uint32_t *rk)
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
            t = protocore_aes_sub_word(protocore_aes_rot_word(t)) ^ ((uint32_t)RCON[i / nk] << 24);
        }
        else if (nk > 6 && i % nk == 4) // AES-256 applies an extra SubWord at the mid-point of each 8-word run.
        {
            t = protocore_aes_sub_word(t);
        }
        rk[i] = rk[i - nk] ^ t;
    }
}

/**
 * @brief AES single-block encrypt (FIPS 197 sec 5.1), @p nr rounds (10=AES-128, 14=AES-256). State is
 *        column-major: s[col*4 + row]. @p rk is the schedule from protocore_aes_key_expand.
 */
PROTOCORE_INLINE void protocore_aes_encrypt_block(const uint32_t *rk, int nr, const uint8_t in[16], uint8_t out[16])
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
            s[i] = PROTOCORE_AES_SBOX[s[i]];
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
            s[c * 4] = a ^ e ^ protocore_aes_xtime(a ^ b);
            s[c * 4 + 1] = b ^ e ^ protocore_aes_xtime(b ^ cc);
            s[c * 4 + 2] = cc ^ e ^ protocore_aes_xtime(cc ^ d);
            s[c * 4 + 3] = d ^ e ^ protocore_aes_xtime(d ^ a);
        }

        for (int i = 0; i < 16; i++)
        {
            s[i] ^= (uint8_t)(rk[r * 4 + i / 4] >> (24 - (i % 4) * 8));
        }
    }

    for (int i = 0; i < 16; i++)
    {
        s[i] = PROTOCORE_AES_SBOX[s[i]];
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

    mem.cpy(out, s, 16);
}

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*key_expand)(uint8_t *, const uint8_t *, int, uint32_t *);
    proto_bool (*encrypt_block)(uint8_t *, const uint32_t *, int, const uint8_t *, uint8_t *);
} AesBlockNs;
PROTOCORE_NS_LAYOUT(AesBlockNs, key_expand, encrypt_block);

/**
 * @brief Expand a key into 4 * (nk + 7) round-key words.
 * @param work PROTOCORE_AES_BLOCK_BORROW bytes the caller took. Not held past the call.
 * @param key 4 * nk key bytes
 * @param nk key words: 4 for AES-128, 8 for AES-256
 * @param rk 4 * (nk + 7) round-key words
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_aes_block_key_expand(uint8_t *work, const uint8_t *key, int nk, uint32_t *rk);
/**
 * @brief Encrypt one 16-byte block under that schedule.
 * @param work PROTOCORE_AES_BLOCK_BORROW bytes the caller took. Not held past the call.
 * @param rk the schedule AesBlockNs::key_expand wrote
 * @param nr rounds: 10 for AES-128, 14 for AES-256
 * @param in 16 input bytes
 * @param out 16 output bytes; may alias in
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_aes_block_encrypt_block(uint8_t *work, const uint32_t *rk, int nr, const uint8_t *in,
                                             uint8_t *out);

/** @brief Module namespace. */
PROTOCORE_NS AesBlockNs AesBlock PROTOCORE_UNUSED = {.key_expand = protocore_aes_block_key_expand,
                                                     .encrypt_block = protocore_aes_block_encrypt_block};

PROTOCORE_END_DECLS

#endif // PROTOCORE_AES_BLOCK_H
