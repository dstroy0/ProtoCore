// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file bitio.h
 * @brief LSB-first bit writer over a caller-owned byte buffer - one source of truth.
 *
 * Packs bits LSB-first into a @c uint32_t accumulator, spills whole bytes to the output, and latches
 * @c overflow when the buffer is full. The DEFLATE encoder
 * (network_drivers/presentation/codec/deflate) and the SSH zlib@openssh.com stream compressor
 * (ssh/transport/ssh_zlib) both write their bitstreams through it.
 *
 * Distinct from bytes.h's @c protocore_bw_* helpers, which are a BYTE-oriented (big-endian) codec cursor.
 * This is a BIT writer, for the DEFLATE bitstream.
 *
 * The module exports one symbol, @ref bitw. The walks live in bitio.c.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_BITIO_H
#define PROTOCORE_BITIO_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/** @brief LSB-first bit writer over the caller's output buffer; @c overflow latches once @c cap is exceeded. */
typedef struct
{
    uint8_t *out;
    size_t cap;
    size_t cnt;   ///< bytes written so far
    uint32_t acc; ///< bit accumulator (LSB-first)
    int nbits;    ///< bits currently buffered (< 8 between calls)
    proto_bool overflow;
} protocore_bit_writer;

/**
 * @brief The bit-writer module. Bits enter an accumulator at the low end and leave it a byte at a time.
 *
 * @var BitwNs::put
 * Append the low @c n bits of @c bits to @c w, LSB-first, and spill every byte the accumulator
 * completes to @c out. A spill with @c cnt already at @c cap sets @c overflow, clears the
 * accumulator, and every later call returns at once.
 *
 * @var BitwNs::align
 * Write the partial byte @c w still holds, its high bits zero, then clear the accumulator. Nothing is
 * written when @c nbits is zero. A write with @c cnt already at @c cap sets @c overflow instead.
 *
 * No storage member: the buffer, the cursor and the accumulator all live in the caller's
 * ::protocore_bit_writer, and this module holds nothing of its own.
 */
typedef struct
{
    void (*put)(protocore_bit_writer *w, uint32_t bits, int n);
    void (*align)(protocore_bit_writer *w);
} BitwNs;

// The bit walks, in bitio.c. Named here because the table below has to name them, and prefixed
// because that puts them in the linker's namespace.
void protocore_bitw_put(protocore_bit_writer *w, uint32_t bits, int n);
void protocore_bitw_align(protocore_bit_writer *w);

/**
 * @brief The names, aliased.
 *
 * `static const` and initialized here, not declared `extern` against a definition in the .c: a
 * translation unit that can see this initializer knows which function each member holds, so a member
 * read folds away and the call to the walk in bitio.c is direct, leaving the table referenced by
 * nothing for the linker to drop.
 *
 * `unused` because this header reaches files that take none of it.
 */
// Designated, so a member's position in the struct does not decide what it binds to.
static const BitwNs bitw __attribute__((unused)) = {.put = protocore_bitw_put, .align = protocore_bitw_align};

PROTOCORE_END_DECLS

#endif // PROTOCORE_BITIO_H
