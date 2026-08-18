// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file endian.h
 * @brief Wire integers, byte at a time.
 *
 * Every protocol here puts its multi-byte fields in network byte order, and lands them wherever the
 * preceding fields end - RFC 826 puts a 32-bit protocol address at offset 14, RFC 791 puts one at
 * 12 inside a header that itself starts 14 bytes into the frame. Neither is aligned for its width.
 *
 * So a field is assembled from its bytes with shifts and masks rather than read through a cast. A
 * cast to a wider type at an odd address is a fault on the parts in the target list that require
 * natural alignment, and a silent penalty on the ones that only prefer it. Byte reads are always
 * aligned, so this is the only form that is correct everywhere and it is what every accessor here
 * compiles to.
 */

#ifndef PROTOCORE_IDEMIP_ENDIAN_H
#define PROTOCORE_IDEMIP_ENDIAN_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

/** @brief Read a big-endian 16-bit field at @p p. */
PROTOCORE_INLINE uint16_t idemip_rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/** @brief Read a big-endian 32-bit field at @p p. */
PROTOCORE_INLINE uint32_t idemip_rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/** @brief Write @p v as a big-endian 16-bit field at @p p. */
PROTOCORE_INLINE void idemip_wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

/** @brief Write @p v as a big-endian 32-bit field at @p p. */
PROTOCORE_INLINE void idemip_wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

PROTOCORE_END_DECLS

#endif // PROTOCORE_IDEMIP_ENDIAN_H
