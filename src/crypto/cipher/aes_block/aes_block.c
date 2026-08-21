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

#include "protocore_config.h" // the entry point: the widths

#include "crypto/cipher/aes_block/aes_block.h"

// --- the entries -----------------------------------------------------------

// Expand the key into 4 * (nk + 7) round-key words in the caller's rk.
proto_bool protocore_aes_block_key_expand(uint8_t *restrict work, const uint8_t *key, int nk, uint32_t *rk)
{
    proto_bool ok = PROTO_FALSE;
    (void)work;
    ok = PROTO_FALSE;
    if (!key || !rk)
    {
        return ok;
    }
    protocore_aes_key_expand(key, nk, rk);
    return PROTO_TRUE;
}

// Encrypt one 16-byte block under the caller's schedule, nr rounds.
proto_bool protocore_aes_block_encrypt_block(uint8_t *restrict work, const uint32_t *rk, int nr, const uint8_t *in,
                                             uint8_t *out)
{
    proto_bool ok = PROTO_FALSE;
    (void)work;
    ok = PROTO_FALSE;
    if (!rk || !in || !out)
    {
        return ok;
    }
    protocore_aes_encrypt_block(rk, nr, in, out);
    return PROTO_TRUE;
}
