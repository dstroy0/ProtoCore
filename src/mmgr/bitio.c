// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file bitio.c
 * @brief The LSB-first bit writer - see bitio.h.
 *
 * Bits enter a @c uint32_t accumulator above the bits already buffered, so the first bit written sits
 * lowest. Whole bytes leave the low end of the accumulator into the caller's buffer, one per step,
 * until fewer than eight bits remain. A byte with @c cnt already at @c cap latches @c overflow and
 * empties the accumulator, and a latched writer returns from every later call.
 *
 * The one symbol this module exports is @ref bitw.
 */

#include "mmgr/bitio.h"

void protocore_bitw_put(protocore_bit_writer *w, uint32_t bits, int n)
{
    if (w->overflow)
    {
        return; // latched: nbits is no longer a shift distance this can use
    }
    // Only the low n bits enter the accumulator; anything above them is dropped rather than ORed
    // into the bits that follow.
    uint32_t low = (n >= 32) ? bits : (bits & ((1u << n) - 1u));
    w->acc |= low << w->nbits;
    w->nbits += n;
    while (w->nbits >= 8)
    {
        if (w->cnt >= w->cap)
        {
            w->overflow = PROTO_TRUE;
            w->nbits = 0;
            w->acc = 0;
            return;
        }
        w->out[w->cnt] = (uint8_t)(w->acc & 0xFF);
        w->cnt++;
        w->acc >>= 8;
        w->nbits -= 8;
    }
}

void protocore_bitw_align(protocore_bit_writer *w)
{
    if (w->nbits > 0)
    {
        if (w->cnt >= w->cap)
        {
            w->overflow = PROTO_TRUE;
            return;
        }
        w->out[w->cnt++] = (uint8_t)(w->acc & 0xFF);
        w->acc = 0;
        w->nbits = 0;
    }
}
