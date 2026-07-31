// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_quic_varint.h
 * @brief QUIC variable-length integer coding (RFC 9000 sec 16).
 *
 * QUIC, HTTP/3 (RFC 9114), and QPACK (RFC 9204) encode most lengths and identifiers as a
 * variable-length integer: the two most-significant bits of the first byte give the total length
 * (00 -> 1 byte / 6-bit value, 01 -> 2 / 14-bit, 10 -> 4 / 30-bit, 11 -> 8 / 62-bit), and the
 * remaining bits hold the value big-endian. The representable range is 0 .. 2^62-1.
 *
 * This is the foundational primitive of the HTTP/3 stack. Pure and host-tested.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_QUIC_VARINT_H
#define PROTOCORE_QUIC_VARINT_H

#include "protocore_config.h"

#if PC_ENABLE_HTTP3

#include <stddef.h>
#include <stdint.h>

/** @brief Largest value a QUIC varint can hold (2^62 - 1). */
#define QUIC_VARINT_MAX 0x3FFFFFFFFFFFFFFFull

/** @brief Bytes @p value encodes to (1 / 2 / 4 / 8), or 0 if it exceeds QUIC_VARINT_MAX. */
size_t pc_quic_varint_len(uint64_t value);

/** @brief Encode @p value in its shortest form. @return bytes written, or 0 on overflow / cap. */
size_t pc_quic_varint_encode(uint8_t *out, size_t cap, uint64_t value);

/**
 * @brief Decode a varint at @p in. Sets @p value and @p consumed (1/2/4/8). @return false if the
 * buffer is shorter than the length the first byte announces.
 */
bool pc_quic_varint_decode(const uint8_t *in, size_t len, uint64_t *value, size_t *consumed);

#endif // PC_ENABLE_HTTP3
#endif // PROTOCORE_QUIC_VARINT_H
