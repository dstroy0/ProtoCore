// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file utf8.c
 * @brief UTF-8 well-formedness. See utf8.h.
 *
 * Pure: the run is the caller's and nothing is held between calls, so there is no storage member.
 */

#include "shared/utf8/utf8.h"

void protocore_utf8_valid(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *s = Utf8V.args.s;
    const size_t n = Utf8V.args.n;

    Utf8V.ok = PROTO_FALSE;
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
            return; // 0x80..0xBF lead, or 0xF8.. invalid
        }
        if (i + need >= n)
        {
            return; // truncated multi-byte sequence
        }
        for (size_t k = 1; k <= need; k++)
        {
            uint8_t cc = s[i + k];
            if ((cc & 0xC0) != 0x80)
            {
                return; // bad continuation byte
            }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (cp < lo || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu))
        {
            return; // overlong, out-of-range, or surrogate
        }
        i += need + 1;
    }
    Utf8V.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
Utf8Vars Utf8V;
