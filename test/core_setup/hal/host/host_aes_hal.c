// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file host_aes_hal.c
 * @brief The host arm of the AES accelerator. See host_aes_hal.h.
 *
 * The peripheral's key bank stands as one owned context here: a schedule and the round count the
 * loaded key implies. A block encrypt reads that bank, exactly as the register driver's does.
 */

#include "test/core_setup/hal/host/host_aes_hal.h"

#if PROTOCORE_HOST && PROTOCORE_HAS_HW_AES

#include "crypto/cipher/aes_block/aes_block.h" // protocore_aes_key_expand / protocore_aes_encrypt_block

PROTOCORE_BEGIN_DECLS

// The accelerator's key bank, one owned context (internal linkage): the expanded schedule the last
// setkey left and the rounds its length implies. Held across a run of blocks, as the bank is.
typedef struct
{
    uint32_t rk[PROTOCORE_AES_HOST_RK_WORDS]; ///< expanded round-key schedule
    int nr;                                   ///< rounds: 10, 12 or 14
    proto_bool keyed;                         ///< a key has been loaded since acquire
} HalAesHostCtx;
static HalAesHostCtx s_aes = {{0}, 0, PROTO_FALSE};

void protocore_aes_hw_acquire(void)
{
    // No clock, no reset, no second user. The bracket is kept so the arm's shape is the same
    // natively as on the part.
}

void protocore_aes_hw_release(void)
{
}

void protocore_aes_hw_setkey(const uint8_t *key, unsigned key_bytes)
{
    if (!key || (key_bytes != 16u && key_bytes != 24u && key_bytes != 32u))
    {
        s_aes.keyed = PROTO_FALSE;
        return;
    }
    const int nk = (int)(key_bytes / 4u); // key words: 4, 6 or 8
    protocore_aes_key_expand(key, nk, s_aes.rk);
    s_aes.nr = nk + 6; // FIPS 197 table 2: rounds = Nk + 6
    s_aes.keyed = PROTO_TRUE;
}

void protocore_aes_hw_block(const uint8_t *in, uint8_t *out)
{
    if (!in || !out)
    {
        return;
    }
    if (!s_aes.keyed)
    {
        for (unsigned i = 0; i < PROTOCORE_AES_HW_BLOCK_LEN; i++)
        {
            out[i] = 0u; // no key loaded: a zero block fails every downstream comparison
        }
        return;
    }
    protocore_aes_encrypt_block(s_aes.rk, s_aes.nr, in, out);
}

PROTOCORE_END_DECLS

#endif // PROTOCORE_HOST && PROTOCORE_HAS_HW_AES
