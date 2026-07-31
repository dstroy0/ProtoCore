// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file utf8.h
 * @brief Strict UTF-8 validation (RFC 3629), one shared copy.
 *
 * Several protocols must reject non-UTF-8 input: WebSocket TEXT frames
 * (RFC 6455 8.1, fail with close 1007) and MQTT strings (MQTT 1.5.3). Both use
 * this single validator rather than each rolling its own. Header-only inline,
 * like the other shared primitives - zero link cost when unused.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_UTF8_H
#define PROTOCORE_UTF8_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief True if [s, s+n) is well-formed UTF-8.
 *
 * Rejects overlong encodings, surrogate code points (U+D800..U+DFFF), values
 * above U+10FFFF, bad continuation bytes, and truncated multi-byte sequences.
 */
inline bool pc_utf8_valid(const uint8_t *s, size_t n)
{
    size_t i = 0;
    while (i < n)
    {
        uint8_t c = s[i];
        if (c < 0x80)
        {
            i++;
            continue;
        }
        size_t need;
        uint32_t cp;
        uint32_t lo;
        if ((c & 0xE0) == 0xC0)
        {
            need = 1;
            cp = c & 0x1F;
            lo = 0x80;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            need = 2;
            cp = c & 0x0F;
            lo = 0x800;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            need = 3;
            cp = c & 0x07;
            lo = 0x10000;
        }
        else
        {
            return false; // 0x80..0xBF lead, or 0xF8.. invalid
        }
        if (i + need >= n)
        {
            return false; // truncated multi-byte sequence
        }
        for (size_t k = 1; k <= need; k++)
        {
            uint8_t cc = s[i + k];
            if ((cc & 0xC0) != 0x80)
            {
                return false; // bad continuation byte
            }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (cp < lo || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu))
        {
            return false; // overlong, out-of-range, or surrogate
        }
        i += need + 1;
    }
    return true;
}

#endif // PROTOCORE_UTF8_H
