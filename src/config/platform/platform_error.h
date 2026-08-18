// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file platform_error.h
 * @brief The platform's compile-time rules: a bad width stops the build here, naming itself,
 *        instead of at the first expression that assumed it.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PLATFORM_ERROR_H
#define PROTOCORE_PLATFORM_ERROR_H

#if PROTO_WORD_BITS != 16 && PROTO_WORD_BITS != 32 && PROTO_WORD_BITS != 64
#error "PROTO_WORD_BITS must be 16, 32 or 64"
#endif
#if PROTO_INDEX_BITS != 16 && PROTO_INDEX_BITS != 32
#error "PROTO_INDEX_BITS must be 16 or 32"
#endif
#if PROTO_SWAR_BITS != 8 && PROTO_SWAR_BITS != 16 && PROTO_SWAR_BITS != 32 && PROTO_SWAR_BITS != 64
#error "PROTO_SWAR_BITS must be 8, 16, 32 or 64"
#endif
#if PROTO_INDEX_BITS > PROTO_WORD_BITS
#error "PROTO_INDEX_BITS exceeds PROTO_WORD_BITS: an index must fit the register it is carried in"
#endif
#if PROTO_SWAR_BITS > PROTO_WORD_BITS
#error "PROTO_SWAR_BITS exceeds PROTO_WORD_BITS: a lane carrier wider than the register is synthesized \
from halves and is slower than the width it decomposes into"
#endif

#endif // PROTOCORE_PLATFORM_ERROR_H
