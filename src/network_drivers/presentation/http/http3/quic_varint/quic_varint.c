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

static void quic_varint_len(uint8_t *restrict work);

static void quic_varint_len(uint8_t *restrict work)
{
    (void)work;
    uint64_t value = QuicVarint.len_args.value;

    if (value <= 0x3F)
    {
        QuicVarint.n = 1;
        return;
    }
    if (value <= 0x3FFF)
    {
        QuicVarint.n = 2;
        return;
    }
    if (value <= 0x3FFFFFFF)
    {
        QuicVarint.n = 4;
        return;
    }
    if (value <= QUIC_VARINT_MAX)
    {
        QuicVarint.n = 8;
        return;
    }
    QuicVarint.n = 0;
}

static void quic_varint_encode(uint8_t *restrict work)
{
    uint8_t *out = QuicVarint.encode_args.out;
    size_t cap = QuicVarint.encode_args.cap;
    uint64_t value = QuicVarint.encode_args.value;

    QuicVarint.len_args.value = value;
    quic_varint_len(work);
    size_t n = QuicVarint.n;
    if (n == 0 || cap < n)
    {
        QuicVarint.n = 0;
        return;
    }
    // The 2-bit length prefix (log2 n) sits in the top bits of the first byte.
    static const uint8_t prefix[9] = {0, 0x00, 0x40, 0, 0x80, 0, 0, 0, 0xC0};
    for (size_t i = 0; i < n; i++)
    {
        out[n - 1 - i] = (uint8_t)(value >> (8 * i));
    }
    out[0] |= prefix[n];
    QuicVarint.n = n;
}

static void quic_varint_decode(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *in = QuicVarint.decode_args.in;
    size_t len = QuicVarint.decode_args.len;
    uint64_t *value = QuicVarint.decode_args.value;
    size_t *consumed = QuicVarint.decode_args.consumed;

    if (len < 1)
    {
        QuicVarint.ok = PROTO_FALSE;
        return;
    }
    size_t n = (size_t)1 << (in[0] >> 6); // 1, 2, 4, or 8
    if (len < n)
    {
        QuicVarint.ok = PROTO_FALSE;
        return;
    }
    uint64_t v = (uint64_t)(in[0] & 0x3F);
    for (size_t i = 1; i < n; i++)
    {
        v = (v << 8) | in[i];
    }
    *value = v;
    *consumed = n;
    QuicVarint.ok = PROTO_TRUE;
}

QuicVarintNs QuicVarint = {
    .len = quic_varint_len,
    .encode = quic_varint_encode,
    .decode = quic_varint_decode,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3
