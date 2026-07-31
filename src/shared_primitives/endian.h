// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file endian.h
 * @brief Fixed-width integer serializers into a raw @c uint8_t* buffer - one source of truth.
 *
 * Every binary codec (ADS, AMQP, C37.118, NetFlow, WAL, SHA-1/256, NTLMSSP/SMB2, ...) had its own
 * byte-for-byte copy of these little-/big-endian pack/unpack helpers under a different local name
 * (@c put16le / @c put16 / @c wr16 / @c store_be32 / @c put_u32 ...). They now live here once.
 *
 * Writers return the number of bytes written (2/4/8) so a caller can advance an offset; a caller that
 * only needs the side effect simply ignores the return. Readers assume @p p has at least the width in
 * range - callers bounds-check the buffer, as the hand-rolled copies did.
 *
 * Header-only and pure (only @c <stdint.h> / @c <stddef.h>) so it is host-testable and identical on
 * device + host, and carries zero link cost when unused.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ENDIAN_H
#define PROTOCORE_ENDIAN_H

#include <stddef.h>
#include <stdint.h>

// --- little-endian ------------------------------------------------------------------------------

/** @brief Write @p v little-endian at @p p. @return 2. */
inline size_t pc_wr16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    return 2;
}

/** @brief Write @p v little-endian at @p p. @return 4. */
inline size_t pc_wr32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
    return 4;
}

/** @brief Write @p v little-endian at @p p. @return 8. */
inline size_t pc_wr64le(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
    {
        p[i] = (uint8_t)(v >> (8 * i));
    }
    return 8;
}

/** @brief Read a little-endian u16 at @p p. */
inline uint16_t pc_rd16le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/** @brief Read a little-endian u32 at @p p. */
inline uint32_t pc_rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/** @brief Read a little-endian u64 at @p p. */
inline uint64_t pc_rd64le(const uint8_t *p)
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
inline size_t pc_wr16be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
    return 2;
}

/** @brief Write @p v big-endian at @p p. @return 4. */
inline size_t pc_wr32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
    return 4;
}

/** @brief Write @p v big-endian at @p p. @return 8. */
inline size_t pc_wr64be(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
    {
        p[i] = (uint8_t)(v >> (8 * (7 - i)));
    }
    return 8;
}

/** @brief Read a big-endian u16 at @p p. */
inline uint16_t pc_rd16be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/** @brief Read a big-endian u32 at @p p. */
inline uint32_t pc_rd32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/** @brief Read a big-endian u64 at @p p. */
inline uint64_t pc_rd64be(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
    {
        v = (v << 8) | p[i];
    }
    return v;
}

#endif // PROTOCORE_ENDIAN_H
