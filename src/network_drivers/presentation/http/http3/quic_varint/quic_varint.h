// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_quic_varint.h
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HTTP3

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief Largest value a QUIC varint can hold (2^62 - 1). */
#define QUIC_VARINT_MAX 0x3FFFFFFFFFFFFFFFull

/** @brief What len takes: value. */
typedef struct
{
    uint64_t value;
} QuicVarintLenArgs;

/** @brief What encode takes: out, cap, value. */
typedef struct
{
    uint8_t *out;
    size_t cap;
    uint64_t value;
} QuicVarintEncodeArgs;

/** @brief What decode takes: in, len, value, consumed. */
typedef struct
{
    const uint8_t *in;
    size_t len;
    uint64_t *value;
    size_t *consumed;
} QuicVarintDecodeArgs;

/**
 * @brief QUIC variable-length integer coding (RFC 9000 sec 16).
 *
 * A caller sets the members a call takes, invokes it through ::QuicVarint with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   QuicVarint.len_args.value = ...;
 *   QuicVarint.len(work);
 *   // QuicVarint.n is what the call reports
 *
 * @var QuicVarintNs::len_args  what len takes: value
 * @var QuicVarintNs::encode_args  what encode takes: out, cap, value
 * @var QuicVarintNs::decode_args  what decode takes: in, len, value, consumed
 * @var QuicVarintNs::ok  a call's true/false outcome
 * @var QuicVarintNs::n  the count a call reports
 * @var QuicVarintNs::len  bytes value encodes to (1 / 2 / 4 / 8), or 0 if it exceeds ...
 * @var QuicVarintNs::encode  encode value in its shortest form. bytes written, or 0 on overflow ...
 * @var QuicVarintNs::decode  decode a varint at in. Sets value and consumed (1/2/4/8). false if ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    QuicVarintLenArgs len_args;
    QuicVarintEncodeArgs encode_args;
    QuicVarintDecodeArgs decode_args;

    proto_bool ok;
    size_t n;

    void (*const len)(uint8_t *restrict work);
    void (*const encode)(uint8_t *restrict work);
    void (*const decode)(uint8_t *restrict work);
} QuicVarintNs;

/** @brief The one symbol this module exports. */
extern QuicVarintNs QuicVarint;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3

#endif // PROTOCORE_QUIC_VARINT_H
