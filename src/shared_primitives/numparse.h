// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file numparse.h
 * @brief Tiny no-stdlib base-10 number parsers (strtol/strtoul/strtof replacements).
 *
 * This library does not pull in `<stdlib.h>` - no heap, no locale, no bloat. These
 * header-only inline helpers parse a base-10 integer / unsigned / float from a
 * string, mirroring the `strtol`-family `endptr` contract: skip leading
 * whitespace, accept an optional sign, consume digits, and (optionally) report
 * where parsing stopped. If no digit is converted, `*end` is set to the original
 * pointer (so callers can detect "no number") and the result is 0.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_NUMPARSE_H
#define PROTOCORE_NUMPARSE_H

inline bool pc_np_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
inline bool pc_np_digit(char c)
{
    return c >= '0' && c <= '9';
}

/** @brief Parse a base-10 long; sets @p end past the digits (or to @p s if none). */
inline long pc_strtol(const char *s, const char **end)
{
    const char *p = s;
    while (pc_np_ws(*p))
    {
        p++;
    }
    bool neg = false;
    if (*p == '+' || *p == '-')
    {
        neg = (*p++ == '-');
    }
    const char *ds = p;
    unsigned long v = 0; // accumulate unsigned: signed overflow (a huge digit run) is UB
    while (pc_np_digit(*p))
    {
        v = v * 10UL + (unsigned long)(*p++ - '0');
    }
    if (end)
    {
        *end = (p == ds) ? s : p;
    }
    return neg ? (long)(0UL - v) : (long)v; // two's-complement reinterpret, no negation UB
}

/** @brief Parse a base-10 unsigned long; sets @p end past the digits (or to @p s). */
inline unsigned long pc_strtoul(const char *s, const char **end)
{
    const char *p = s;
    while (pc_np_ws(*p))
    {
        p++;
    }
    if (*p == '+')
    {
        p++;
    }
    const char *ds = p;
    unsigned long v = 0;
    while (pc_np_digit(*p))
    {
        v = v * 10UL + (unsigned long)(*p++ - '0');
    }
    if (end)
    {
        *end = (p == ds) ? s : p;
    }
    return v;
}

// Parse the fractional part after a '.', advancing p and accumulating into val.
inline void pc_strtod_frac(const char *&p, double &val, bool &any)
{
    p++; // consume '.'
    double scale = 1.0;
    while (pc_np_digit(*p))
    {
        scale *= 10.0;
        val += (double)(*p++ - '0') / scale;
        any = true;
    }
}

// Apply a trailing exponent (e[+/-]NNN) to val, advancing p past it.
inline void pc_strtod_exp(const char *&p, double &val)
{
    p++; // consume 'e'/'E'
    bool eneg = false;
    if (*p == '+' || *p == '-')
    {
        eneg = (*p++ == '-');
    }
    int ex = 0;
    while (pc_np_digit(*p))
    {
        ex = (ex < 400) ? ex * 10 + (*p - '0') : ex; // clamp: 10^400 overflows the double to inf
        p++;
    }
    double m = 1.0;
    for (int k = 0; k < ex; k++)
    {
        m *= 10.0;
    }
    val = eneg ? val / m : val * m;
}

/** @brief Parse a double (integer[.frac][e[+/-]exp]); sets @p end (or to @p s if none). */
inline double pc_strtod(const char *s, const char **end)
{
    const char *p = s;
    while (pc_np_ws(*p))
    {
        p++;
    }
    bool neg = false;
    if (*p == '+' || *p == '-')
    {
        neg = (*p++ == '-');
    }
    bool any = false;
    double val = 0.0;
    while (pc_np_digit(*p))
    {
        val = val * 10.0 + (*p++ - '0');
        any = true;
    }
    if (*p == '.')
    {
        pc_strtod_frac(p, val, any);
    }
    if (any && (*p == 'e' || *p == 'E'))
    {
        pc_strtod_exp(p, val);
    }
    if (end)
    {
        *end = any ? p : s;
    }
    return neg ? -val : val;
}

/** @brief Parse a float (integer[.frac][e[+/-]exp]); sets @p end (or to @p s if none). */
inline float pc_strtof(const char *s, const char **end)
{
    return (float)pc_strtod(s, end); // GGA lat/lon and other sub-meter values need pc_strtod's precision
}

#endif // PROTOCORE_NUMPARSE_H
