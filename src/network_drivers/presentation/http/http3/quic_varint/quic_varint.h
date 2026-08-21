// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_QUIC_VARINT_H
#define PROTOCORE_QUIC_VARINT_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file quic_varint.h
 * @brief QUIC variable-length integer coding (RFC 9000 sec 16).
 *
 * QUIC, HTTP/3 (RFC 9114), and QPACK (RFC 9204) encode most lengths and identifiers as a
 * variable-length integer: the two most-significant bits of the first byte give the total length
 * (00 -> 1 byte / 6-bit value, 01 -> 2 / 14-bit, 10 -> 4 / 30-bit, 11 -> 8 / 62-bit), and the
 * remaining bits hold the value big-endian. The representable range is 0 .. 2^62-1.
 *
 * This is the foundational primitive of the HTTP/3 stack. Pure and host-tested.
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_QUIC_VARINT_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/** @brief Largest value a QUIC varint can hold (2^62 - 1). */
#define QUIC_VARINT_MAX 0x3FFFFFFFFFFFFFFFull

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    size_t (*len)(uint8_t *, uint64_t);
    size_t (*encode)(uint8_t *, uint8_t *, size_t, uint64_t);
    proto_bool (*decode)(uint8_t *, const uint8_t *, size_t, uint64_t *, size_t *);
} QuicVarintNs;
PROTOCORE_NS_LAYOUT(QuicVarintNs, len, encode, decode);

/**
 * @brief Bytes value encodes to (1 / 2 / 4 / 8), or 0 if it exceeds .
 * @param work PROTOCORE_QUIC_VARINT_BORROW bytes the caller took. Not held past the call.
 * @param value Value
 * @return The size_t.
 */
size_t protocore_quic_varint_len(uint8_t *work, uint64_t value);
/**
 * @brief Encode value in its shortest form. bytes written, or 0 on overflow .
 * @param work PROTOCORE_QUIC_VARINT_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param value Value
 * @return The size_t.
 */
size_t protocore_quic_varint_encode(uint8_t *work, uint8_t *out, size_t cap, uint64_t value);
/**
 * @brief Decode a varint at in. Sets value and consumed (1/2/4/8). false if .
 * @param work PROTOCORE_QUIC_VARINT_BORROW bytes the caller took. Not held past the call.
 * @param in In
 * @param len Len
 * @param value Value
 * @param consumed Consumed
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_quic_varint_decode(uint8_t *work, const uint8_t *in, size_t len, uint64_t *value,
                                        size_t *consumed);

/** @brief Module namespace. */
PROTOCORE_NS QuicVarintNs QuicVarint PROTOCORE_UNUSED = {
    .len = protocore_quic_varint_len, .encode = protocore_quic_varint_encode, .decode = protocore_quic_varint_decode};

PROTOCORE_END_DECLS

#endif // PROTOCORE_QUIC_VARINT_H
