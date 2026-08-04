// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file iccp.c
 * @brief ICCP / TASE.2 data-value codec (see iccp.h).
 */

#include "services/energy/iccp/iccp.h"

#if PC_ENABLE_ICCP

#include <string.h>

// Append a short-form TLV (value length < 128). Returns bytes written at out, or 0 on overflow.
static size_t tlv(uint8_t tag, const uint8_t *val, size_t val_len, uint8_t *out, size_t cap)
{
    if (val_len > 0x7F || 2 + val_len > cap)
    {
        return 0; // internal linkage, so its only 7 call sites (all in this
                  // file) pass val_len<=15 (RealQ's line-99 wrap) - never near 0x7F.
    }
    out[0] = tag;
    out[1] = (uint8_t)val_len;
    if (val_len)
    {
        memcpy(out + 2, val, val_len); // 1/4-byte field, int_content's >=1 result, or n built from those).
    }
    return 2 + val_len;
}

// Minimal signed INTEGER content (two's complement, minimal length). Returns length in buf (<= 5).
static size_t int_content(int32_t v, uint8_t *buf)
{
    // Build big-endian, then trim redundant sign bytes.
    uint8_t tmp[4];
    tmp[0] = (uint8_t)(v >> 24);
    tmp[1] = (uint8_t)(v >> 16);
    tmp[2] = (uint8_t)(v >> 8);
    tmp[3] = (uint8_t)v;
    size_t start = 0;
    // Trim leading 0x00 (positive) or 0xFF (negative) while the sign bit is preserved.
    while (start < 3 && ((tmp[start] == 0x00 && (tmp[start + 1] & 0x80) == 0) ||
                         (tmp[start] == 0xFF && (tmp[start + 1] & 0x80) != 0)))
    {
        start++;
    }
    size_t n = 4 - start;
    for (size_t i = 0; i < n; i++)
    {
        buf[i] = tmp[start + i];
    }
    return n;
}

size_t pc_iccp_state_q(uint8_t state, uint8_t flags, const uint8_t time[4], uint8_t *out, size_t cap)
{
    if (!out)
    {
        return 0;
    }
    // Inner: stateAndQuality byte [85], optional time [17].
    uint8_t inner[16];
    size_t n = 0;
    uint8_t sq = (uint8_t)(((state & 0x3) << 6) | (flags & ICCP_QUAL_MASK)); // state in high bits + quality
    size_t r = tlv(0x85, &sq, 1, inner + n, sizeof(inner) - n);
    if (!r)
    {
        return 0;
    }
    n += r;
    if (time)
    {
        r = tlv(0x17, time, 4, inner + n, sizeof(inner) - n);
        if (!r)
        {
            return 0;
        }
        n += r;
    }
    // Wrap as StateQ [A2].
    return tlv(0xA2, inner, n, out, cap);
}

size_t pc_iccp_real_q(int32_t milli, uint8_t flags, const uint8_t time[4], uint8_t *out, size_t cap)
{
    if (!out)
    {
        return 0;
    }
    uint8_t inner[24];
    size_t n = 0;
    uint8_t ic[5];
    size_t il = int_content(milli, ic);
    size_t r = tlv(0x02, ic, il, inner + n, sizeof(inner) - n); // INTEGER value
    if (!r)
    {
        return 0;
    }
    n += r;
    uint8_t q = (uint8_t)(flags & ICCP_QUAL_MASK);
    r = tlv(0x85, &q, 1, inner + n, sizeof(inner) - n); // quality
    if (!r)
    {
        return 0;
    }
    n += r;
    if (time)
    {
        r = tlv(0x17, time, 4, inner + n, sizeof(inner) - n);
        if (!r)
        {
            return 0;
        }
        n += r;
    }
    // Wrap as RealQ [A3].
    return tlv(0xA3, inner, n, out, cap);
}

#endif // PC_ENABLE_ICCP
