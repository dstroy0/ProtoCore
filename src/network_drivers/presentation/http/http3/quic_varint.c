// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_quic_varint.cpp
 * @brief QUIC variable-length integer coding - implementation. See pc_quic_varint.h.
 */

#include "network_drivers/presentation/http/http3/quic_varint.h"

#if PC_ENABLE_HTTP3

size_t pc_quic_varint_len(uint64_t value)
{
    if (value <= 0x3F)
    {
        return 1;
    }
    if (value <= 0x3FFF)
    {
        return 2;
    }
    if (value <= 0x3FFFFFFF)
    {
        return 4;
    }
    if (value <= QUIC_VARINT_MAX)
    {
        return 8;
    }
    return 0;
}

size_t pc_quic_varint_encode(uint8_t *out, size_t cap, uint64_t value)
{
    size_t n = pc_quic_varint_len(value);
    if (n == 0 || cap < n)
    {
        return 0;
    }
    // The 2-bit length prefix (log2 n) sits in the top bits of the first byte.
    static const uint8_t prefix[9] = {0, 0x00, 0x40, 0, 0x80, 0, 0, 0, 0xC0};
    for (size_t i = 0; i < n; i++)
    {
        out[n - 1 - i] = (uint8_t)(value >> (8 * i));
    }
    out[0] |= prefix[n];
    return n;
}

proto_bool pc_quic_varint_decode(const uint8_t *in, size_t len, uint64_t *value, size_t *consumed)
{
    if (len < 1)
    {
        return PROTO_FALSE;
    }
    size_t n = (size_t)1 << (in[0] >> 6); // 1, 2, 4, or 8
    if (len < n)
    {
        return PROTO_FALSE;
    }
    uint64_t v = (uint64_t)(in[0] & 0x3F);
    for (size_t i = 1; i < n; i++)
    {
        v = (v << 8) | in[i];
    }
    *value = v;
    *consumed = n;
    return PROTO_TRUE;
}

#endif // PC_ENABLE_HTTP3
