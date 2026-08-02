// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file endian.h
 * @brief A fixed width moved between an integer and the bytes at a pointer.
 *
 * A width is all this file knows, so the bound is not its to check. shared_primitives/span.h holds
 * the bound and hands out a pointer to bytes it has already proven; a width is read or written there.
 *
 * Writers return their width (2/4/8) so a caller can advance by it.
 *
 * Byte at a time, never a cast to a wider pointer: correct where loads must be aligned, and correct
 * on either byte order. Header-only, so it costs nothing when unused.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ENDIAN_H
#define PROTOCORE_ENDIAN_H

#include "protocore_config.h" // the entry point: types.h for the widths and PC_INLINE

// --- little-endian ------------------------------------------------------------------------------

/** @brief Write @p v little-endian at @p p. @return 2. */
PC_INLINE size_t pc_wr16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    return 2;
}

/** @brief Write @p v little-endian at @p p. @return 4. */
PC_INLINE size_t pc_wr32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
    return 4;
}

/** @brief Write @p v little-endian at @p p. @return 8. */
PC_INLINE size_t pc_wr64le(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
    {
        p[i] = (uint8_t)(v >> (8 * i));
    }
    return 8;
}

/** @brief Read a little-endian u16 at @p p. */
PC_INLINE uint16_t pc_rd16le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/** @brief Read a little-endian u32 at @p p. */
PC_INLINE uint32_t pc_rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/** @brief Read a little-endian u64 at @p p. */
PC_INLINE uint64_t pc_rd64le(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
    {
        v |= (uint64_t)p[i] << (8 * i);
    }
    return v;
}

// --- big-endian (network order) -----------------------------------------------------------------

/** @brief Write @p v big-endian at @p p. @return 2. */
PC_INLINE size_t pc_wr16be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
    return 2;
}

/** @brief Write @p v big-endian at @p p. @return 4. */
PC_INLINE size_t pc_wr32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
    return 4;
}

/** @brief Write @p v big-endian at @p p. @return 8. */
PC_INLINE size_t pc_wr64be(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
    {
        p[i] = (uint8_t)(v >> (8 * (7 - i)));
    }
    return 8;
}

/** @brief Read a big-endian u16 at @p p. */
PC_INLINE uint16_t pc_rd16be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/** @brief Read a big-endian u32 at @p p. */
PC_INLINE uint32_t pc_rd32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/** @brief Read a big-endian u64 at @p p. */
PC_INLINE uint64_t pc_rd64be(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
    {
        v = (v << 8) | p[i];
    }
    return v;
}

#endif // PROTOCORE_ENDIAN_H
