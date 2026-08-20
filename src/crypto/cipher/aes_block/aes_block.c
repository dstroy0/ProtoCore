// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file aes_block.c
 * @brief AES key schedule and single-block encrypt (see aes_block.h).
 *
 * One set of entries and one arm: the FIPS 197 key expansion and block of aes_block.h, which stay
 * inline in that header for the per-block loops of the software AES-256-CTR, AES-256-GCM, AES-CCM and
 * AES-CMAC arms.
 *
 * There is no context and no borrow. The round-key schedule an expansion writes and the block an
 * encryption reads and writes are the caller's own buffers, named by the args members, so nothing is
 * carried from one call to the next and the @c work pointer goes unread.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_AES_BLOCK

#include "crypto/cipher/aes_block/aes_block.h"
#include "crypto/crypto_opt.h"

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// --- the entries -----------------------------------------------------------

// Expand the key into 4 * (nk + 7) round-key words in the caller's rk.
void protocore_aes_block_key_expand(uint8_t *restrict work)
{
    (void)work;
    AesBlockV.ok = PROTO_FALSE;
    if (!AesBlockV.key_expand_args.key || !AesBlockV.key_expand_args.rk)
    {
        return;
    }
    protocore_aes_key_expand(AesBlockV.key_expand_args.key, AesBlockV.key_expand_args.nk, AesBlockV.key_expand_args.rk);
    AesBlockV.ok = PROTO_TRUE;
}

// Encrypt one 16-byte block under the caller's schedule, nr rounds.
void protocore_aes_block_encrypt_block(uint8_t *restrict work)
{
    (void)work;
    AesBlockV.ok = PROTO_FALSE;
    if (!AesBlockV.encrypt_block_args.rk || !AesBlockV.encrypt_block_args.in || !AesBlockV.encrypt_block_args.out)
    {
        return;
    }
    protocore_aes_encrypt_block(AesBlockV.encrypt_block_args.rk, AesBlockV.encrypt_block_args.nr,
                                AesBlockV.encrypt_block_args.in, AesBlockV.encrypt_block_args.out);
    AesBlockV.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
AesBlockVars AesBlockV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_AES_BLOCK
