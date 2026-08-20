// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_quic_varint.c
 * @brief QUIC variable-length integer coding - implementation. See protocore_quic_varint.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HTTP3

#include "network_drivers/presentation/http/http3/quic_varint/quic_varint.h"

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_quic_varint_len(uint8_t *restrict work);

void protocore_quic_varint_len(uint8_t *restrict work)
{
    (void)work;
    uint64_t value = QuicVarintV.len_args.value;

    if (value <= 0x3F)
    {
        QuicVarintV.n = 1;
        return;
    }
    if (value <= 0x3FFF)
    {
        QuicVarintV.n = 2;
        return;
    }
    if (value <= 0x3FFFFFFF)
    {
        QuicVarintV.n = 4;
        return;
    }
    if (value <= QUIC_VARINT_MAX)
    {
        QuicVarintV.n = 8;
        return;
    }
    QuicVarintV.n = 0;
}

void protocore_quic_varint_encode(uint8_t *restrict work)
{
    uint8_t *out = QuicVarintV.encode_args.out;
    size_t cap = QuicVarintV.encode_args.cap;
    uint64_t value = QuicVarintV.encode_args.value;

    QuicVarintV.len_args.value = value;
    protocore_quic_varint_len(work);
    size_t n = QuicVarintV.n;
    if (n == 0 || cap < n)
    {
        QuicVarintV.n = 0;
        return;
    }
    // The 2-bit length prefix (log2 n) sits in the top bits of the first byte.
    static const uint8_t prefix[9] = {0, 0x00, 0x40, 0, 0x80, 0, 0, 0, 0xC0};
    for (size_t i = 0; i < n; i++)
    {
        out[n - 1 - i] = (uint8_t)(value >> (8 * i));
    }
    out[0] |= prefix[n];
    QuicVarintV.n = n;
}

void protocore_quic_varint_decode(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *in = QuicVarintV.decode_args.in;
    size_t len = QuicVarintV.decode_args.len;
    uint64_t *value = QuicVarintV.decode_args.value;
    size_t *consumed = QuicVarintV.decode_args.consumed;

    if (len < 1)
    {
        QuicVarintV.ok = PROTO_FALSE;
        return;
    }
    size_t n = (size_t)1 << (in[0] >> 6); // 1, 2, 4, or 8
    if (len < n)
    {
        QuicVarintV.ok = PROTO_FALSE;
        return;
    }
    uint64_t v = (uint64_t)(in[0] & 0x3F);
    for (size_t i = 1; i < n; i++)
    {
        v = (v << 8) | in[i];
    }
    *value = v;
    *consumed = n;
    QuicVarintV.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
QuicVarintVars QuicVarintV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3
