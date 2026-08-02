// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hex.h
 * @brief Base-16 conversion between raw bytes and their ASCII digits.
 *
 * Four operations cover every hex site in the library: one nibble out, one digit in, a machine
 * word out, and a byte run in either direction. Each writes into a caller-owned buffer, takes no
 * heap and no `<stdlib.h>`, and is inline so an unused one costs nothing.
 *
 * The decoders report failure through a negative return rather than a sentinel digit, so a
 * malformed byte can never be mistaken for a valid zero.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HEX_H
#define PROTOCORE_HEX_H

#include "protocore_config.h" // the entry point: types.h for the widths and PC_INLINE

/** @brief The 16 hex digits, lowercase - the default rendering everywhere in this library. */
static const char *const PC_HEX_LOWER = "0123456789abcdef";
/** @brief The 16 hex digits, uppercase - for the protocols that specify capitals (RFC 3986 %XX). */
static const char *const PC_HEX_UPPER = "0123456789ABCDEF";

/** @brief Low nibble of @p nibble as a hex character, uppercase when @p upper. */
PC_INLINE char pc_hex_digit(uint8_t nibble, proto_bool upper)
{
    return (upper ? PC_HEX_UPPER : PC_HEX_LOWER)[nibble & 0x0Fu];
}

/** @brief Hex character @p c as 0..15, or -1 when @p c is not a hex digit of either case. */
PC_INLINE int8_t pc_hex_val(char c)
{
    if (c >= '0' && c <= '9')
    {
        return (int8_t)(c - '0');
    }
    if (c >= 'a' && c <= 'f')
    {
        return (int8_t)(c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F')
    {
        return (int8_t)(c - 'A' + 10);
    }
    return -1;
}

/**
 * @brief Write @p v into @p out as lowercase hex digits, most significant first, and return the
 *        digit count (1..8; zero renders as a single "0").
 *
 * No `0x` prefix, no NUL, and no leading zeros, which is the form the HTTP/1.1 chunked size line
 * takes. The width is counted before any digit is written so each digit lands at its final index,
 * which is why no reversal buffer is needed. @p out needs room for 8 characters.
 */
PC_INLINE uint8_t pc_hex_u32(uint32_t v, char *out)
{
    uint8_t digits = 1;
    for (uint32_t probe = v; probe >= 16u; probe >>= 4)
    {
        digits++;
    }
    for (uint8_t i = digits; i > 0; i--)
    {
        out[i - 1] = PC_HEX_LOWER[v & 0x0Fu];
        v >>= 4;
    }
    return digits;
}

/**
 * @brief Write @p n bytes of @p in into @p out as 2 * @p n hex characters plus a NUL.
 * @param upper  uppercase A-F when true, else lowercase a-f.
 *
 * @p out needs a capacity of at least 2 * @p n + 1; the caller owns that bound, since a byte run
 * has no self-describing end.
 */
PC_INLINE void pc_hex_encode(const uint8_t *in, uint32_t n, char *out, proto_bool upper)
{
    const char *digits = upper ? PC_HEX_UPPER : PC_HEX_LOWER;
    for (uint32_t i = 0; i < n; i++)
    {
        out[2 * i] = digits[(in[i] >> 4) & 0x0Fu];
        out[2 * i + 1] = digits[in[i] & 0x0Fu];
    }
    out[2 * n] = '\0';
}

/**
 * @brief Read exactly @p hexlen hex characters from @p in into @p out as @p hexlen / 2 bytes.
 * @return the byte count written, or -1 when @p hexlen is odd, the result exceeds @p out_cap, or
 *         any character is not a hex digit. Nothing is written on the odd-length or capacity
 *         rejections; a bad digit stops the run where it is found.
 */
PC_INLINE int32_t pc_hex_decode(const char *in, uint32_t hexlen, uint8_t *out, uint32_t out_cap)
{
    if ((hexlen % 2) != 0 || (hexlen / 2) > out_cap)
    {
        return -1;
    }
    for (uint32_t i = 0; i < hexlen; i += 2)
    {
        int8_t hi = pc_hex_val(in[i]);
        int8_t lo = pc_hex_val(in[i + 1]);
        if (hi < 0 || lo < 0)
        {
            return -1;
        }
        out[i / 2] = (uint8_t)((hi << 4) | lo);
    }
    return (int32_t)(hexlen / 2);
}

#endif // PROTOCORE_HEX_H
