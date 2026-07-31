// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file bitio.h
 * @brief LSB-first bit writer over a caller-owned byte buffer - one source of truth.
 *
 * The DEFLATE encoder (network_drivers/presentation/codec/deflate) and the SSH zlib@openssh.com stream compressor
 * (ssh/transport/ssh_zlib) each carried a byte-for-byte copy of the same bit writer: pack bits LSB-first into a
 * @c uint32_t accumulator, spill whole bytes to the output, and latch @c overflow when the buffer is full. It
 * lives here once.
 *
 * NOTE: distinct from bytes.h's @c pc_bw_* helpers, which are a BYTE-oriented (big-endian) codec cursor. This
 * is a BIT writer (@c pc_bitw_*), for the DEFLATE bitstream. Header-only, pure (only @c <stdint.h> /
 * @c <stddef.h>), zero link cost when unused.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_BITIO_H
#define PROTOCORE_BITIO_H

#include <stddef.h>
#include <stdint.h>

/** @brief LSB-first bit writer over the caller's output buffer; @c overflow latches once @c cap is exceeded. */
struct pc_bit_writer
{
    uint8_t *out;
    size_t cap;
    size_t cnt;   ///< bytes written so far
    uint32_t acc; ///< bit accumulator (LSB-first)
    int nbits;    ///< bits currently buffered (< 8 between calls)
    bool overflow;
};

/** @brief Append the low @p n bits of @p bits, LSB-first, spilling any completed bytes to the output. */
inline void pc_bitw_put(pc_bit_writer *w, uint32_t bits, int n)
{
    w->acc |= bits << w->nbits;
    w->nbits += n;
    while (w->nbits >= 8)
    {
        if (w->cnt >= w->cap)
        {
            w->overflow = true;
            return;
        }
        w->out[w->cnt++] = (uint8_t)(w->acc & 0xFF);
        w->acc >>= 8;
        w->nbits -= 8;
    }
}

/** @brief Flush any partial byte, padding the high bits with zero (byte alignment). */
inline void pc_bitw_align(pc_bit_writer *w)
{
    if (w->nbits > 0)
    {
        if (w->cnt >= w->cap)
        {
            w->overflow = true;
            return;
        }
        w->out[w->cnt++] = (uint8_t)(w->acc & 0xFF);
        w->acc = 0;
        w->nbits = 0;
    }
}

#endif // PROTOCORE_BITIO_H
