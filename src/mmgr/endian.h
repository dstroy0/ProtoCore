// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file endian.h
 * @brief A fixed width moved between an integer and the bytes at a pointer.
 *
 * A width is all this file knows, so the bound is not its to check. mmgr/span.h holds
 * the bound and hands out a pointer to bytes it has already proven; a width is read or written there.
 *
 * Writers return their width (2/4/8) so a caller can advance by it.
 *
 * Byte at a time, never a cast to a wider pointer: correct where loads must be aligned, and correct
 * on either byte order.
 *
 * The module exports one symbol, @ref endian. The moves themselves are in endian.c.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ENDIAN_H
#define PROTOCORE_ENDIAN_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @brief The fixed-width serializer module. Each entry steps one byte at a time over 2, 4 or 8 bytes.
 *
 * @var EndianNs::wr16le
 * Write @c v at @c p low byte first, @c p[0] taking bits 0..7. Returns 2.
 *
 * @var EndianNs::wr32le
 * Write @c v at @c p low byte first, each later byte shifted down another 8 bits. Returns 4.
 *
 * @var EndianNs::wr64le
 * Write @c v at @c p low byte first, eight steps of the same shift. Returns 8.
 *
 * @var EndianNs::rd16le
 * Read a u16 at @c p taken low byte first: @c p[0] is bits 0..7, @c p[1] shifted up 8.
 *
 * @var EndianNs::rd32le
 * Read a u32 at @c p taken low byte first, each later byte shifted up another 8 bits.
 *
 * @var EndianNs::rd64le
 * Read a u64 at @c p taken low byte first, eight bytes ORed in at their own shift.
 *
 * @var EndianNs::wr16be
 * Write @c v at @c p high byte first, @c p[0] taking bits 8..15. Returns 2.
 *
 * @var EndianNs::wr32be
 * Write @c v at @c p high byte first, each later byte shifted down another 8 bits. Returns 4.
 *
 * @var EndianNs::wr64be
 * Write @c v at @c p high byte first, eight steps down from bits 56..63. Returns 8.
 *
 * @var EndianNs::rd16be
 * Read a u16 at @c p taken high byte first: @c p[0] shifted up 8, @c p[1] is bits 0..7.
 *
 * @var EndianNs::rd32be
 * Read a u32 at @c p taken high byte first, @c p[0] shifted up 24.
 *
 * @var EndianNs::rd64be
 * Read a u64 at @c p taken high byte first, the accumulator shifted up 8 per byte.
 *
 * No storage member: every entry moves bytes between the caller's integer and the caller's pointer
 * and holds nothing of its own.
 */
typedef struct
{
    size_t (*wr16le)(uint8_t *p, uint16_t v);
    size_t (*wr32le)(uint8_t *p, uint32_t v);
    size_t (*wr64le)(uint8_t *p, uint64_t v);
    uint16_t (*rd16le)(const uint8_t *p);
    uint32_t (*rd32le)(const uint8_t *p);
    uint64_t (*rd64le)(const uint8_t *p);
    size_t (*wr16be)(uint8_t *p, uint16_t v);
    size_t (*wr32be)(uint8_t *p, uint32_t v);
    size_t (*wr64be)(uint8_t *p, uint64_t v);
    uint16_t (*rd16be)(const uint8_t *p);
    uint32_t (*rd32be)(const uint8_t *p);
    uint64_t (*rd64be)(const uint8_t *p);
} EndianNs;

// The width moves, in endian.c. Named here because the table below has to name them, and prefixed
// because that puts them in the linker's namespace.
size_t protocore_wr16le(uint8_t *p, uint16_t v);
size_t protocore_wr32le(uint8_t *p, uint32_t v);
size_t protocore_wr64le(uint8_t *p, uint64_t v);
uint16_t protocore_rd16le(const uint8_t *p);
uint32_t protocore_rd32le(const uint8_t *p);
uint64_t protocore_rd64le(const uint8_t *p);
size_t protocore_wr16be(uint8_t *p, uint16_t v);
size_t protocore_wr32be(uint8_t *p, uint32_t v);
size_t protocore_wr64be(uint8_t *p, uint64_t v);
uint16_t protocore_rd16be(const uint8_t *p);
uint32_t protocore_rd32be(const uint8_t *p);
uint64_t protocore_rd64be(const uint8_t *p);

/**
 * @brief The names, aliased.
 *
 * `static const` and initialized here, not declared `extern` against a definition in the .c: a
 * translation unit that can see this initializer knows which function each member holds, so a member
 * read folds away and the call to the move in endian.c is direct, leaving the table referenced by
 * nothing for the linker to drop.
 *
 * `unused` because this header reaches files that take none of it.
 */
// Designated, so a member's position in the struct does not decide what it binds to.
static const EndianNs endian __attribute__((unused)) = {.wr16le = protocore_wr16le,
                                                        .wr32le = protocore_wr32le,
                                                        .wr64le = protocore_wr64le,
                                                        .rd16le = protocore_rd16le,
                                                        .rd32le = protocore_rd32le,
                                                        .rd64le = protocore_rd64le,
                                                        .wr16be = protocore_wr16be,
                                                        .wr32be = protocore_wr32be,
                                                        .wr64be = protocore_wr64be,
                                                        .rd16be = protocore_rd16be,
                                                        .rd32be = protocore_rd32be,
                                                        .rd64be = protocore_rd64be};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENDIAN_H
