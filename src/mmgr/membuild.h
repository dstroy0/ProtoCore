// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file membuild.h
 * @brief Bounded no-heap builder that writes into memory the caller already owns.
 *
 * Under mmgr because building into a buffer is a memory operation: the builder never allocates,
 * it is handed a region and fills it. It bump-appends into a caller-owned `char[]` and latches
 * @c ok to false the first time something would not fit, so every later append is a no-op and the
 * caller tests one flag at the end rather than a return value per call. Header-only inline, so
 * there is zero link cost when unused.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MEMBUILD_H
#define PROTOCORE_MEMBUILD_H

#include "shared_primitives/rawmemcpy.h" // proto_raw_u64 - the IEEE-754 field reads below
#include "shared_primitives/runops.h"    // proto_scan_nul - a word per test, bounded by a known width

/** @brief Bump-append target; @c ok latches false once an append would overflow @c cap. */
typedef struct
{
    char *p;
    size_t cap;
    size_t len;
    proto_bool ok;
} pc_sb;

/**
 * @brief Append @p sl bytes of @p s - the primitive the others build on.
 *
 * Takes the length rather than finding it. Every frame here is mostly literal text whose length the
 * compiler already knows; scanning for a NUL to rediscover it is the same waste as re-parsing a
 * format string. Appending a literal goes through pc_sb_lit, which deduces the length.
 */
static inline void pc_sb_put_n(pc_sb *b, const char *s, size_t sl)
{
    if (!b->ok)
    {
        return;
    }
    if (b->len + sl >= b->cap)
    {
        b->ok = PROTO_FALSE;
        return;
    }
    char *d = b->p + b->len;
    for (size_t i = 0; i < sl; i++)
    {
        d[i] = s[i];
    }
    b->len += sl;
}

/** @brief Append NUL-terminated @p s; leaves the buffer untouched and clears @c ok if it would not fit. */
static inline void pc_sb_put(pc_sb *b, const char *s)
{
    if (!b->ok)
    {
        return;
    }
    pc_sb_put_n(b, s, proto_scan_nul(s, b->cap));
}

/**
 * @brief Append a string literal, taking the length from the array type.
 *
 * `pc_sb_put(b, "HTTP/1.1 ")` scans nine bytes at runtime to learn what the type already states;
 * `sizeof(s) - 1` is a constant the compiler folds. A macro because only the array type carries
 * the extent - passing a literal to a function decays it to a pointer and loses the length.
 *
 * @warning @p s must be a string literal or a `char[]`. A `const char *` compiles here and yields
 *          the pointer size, so it silently appends 3 or 7 bytes. Use pc_sb_put for a runtime
 *          string.
 */
#define pc_sb_lit(b, s) pc_sb_put_n((b), (s), sizeof(s) - 1)

/**
 * @brief Append as much of @p s as fits and stop, WITHOUT latching @c ok.
 *
 * For display text only - an `ls -l` line, a log message - where a short rendering is better than
 * none and nothing downstream parses the result. Never use it for a protocol field: a clipped
 * header or frame has no terminator and desynchronizes the peer, which is exactly what the
 * latching pc_sb_put exists to prevent. The two are deliberately different functions so the choice
 * is visible at the call site rather than being a flag someone sets once and forgets.
 */
static inline void pc_sb_put_clip(pc_sb *b, const char *s)
{
    if (!b->ok || !s || b->len + 1 >= b->cap)
    {
        return;
    }
    size_t room = b->cap - b->len - 1;
    size_t sl = proto_scan_nul(s, room);
    char *d = b->p + b->len;
    for (size_t i = 0; i < sl; i++)
    {
        d[i] = s[i];
    }
    b->len += sl;
}

/**
 * @brief Append @p v as decimal, right-aligned in at least @p columns with leading spaces, if the
 *        whole field fits - else append nothing, without latching @c ok.
 *
 * The display-text counterpart to pc_sb_u64. A half-written number reads as a different number, so
 * this one is all-or-nothing where pc_sb_put_clip is byte-wise. @p columns is what aligns a
 * fixed-width column (an `ls -l` date is `%2d` day and `%5d` year); 0 asks for the natural width.
 * Space padding, not the zero padding pc_sb_uint does: a column pads to align, a field pads to a
 * fixed digit count, and the two are not interchangeable on the wire.
 */
static inline void pc_sb_u64_clip(pc_sb *b, uint64_t v, uint8_t columns)
{
    if (!b->ok)
    {
        return;
    }
    uint64_t probe = v;
    size_t digits = 1;
    while (probe >= 10)
    {
        probe /= 10;
        digits++;
    }
    size_t width = (digits < columns) ? columns : digits;
    if (b->len + width >= b->cap)
    {
        return;
    }
    for (size_t i = width - digits; i-- > 0;)
    {
        b->p[b->len + i] = ' ';
    }
    for (size_t i = width; i-- > width - digits;)
    {
        b->p[b->len + i] = (char)('0' + (unsigned)(v % 10));
        v /= 10;
    }
    b->len += width;
}

/** @brief Append @p s XML-escaped (&amp; &lt; &gt; &quot;); a NULL @p s appends nothing. */
static inline void pc_sb_xml(pc_sb *b, const char *s)
{
    if (!b->ok || !s)
    {
        return;
    }
    for (; *s; s++)
    {
        const char *rep = NULL;
        switch (*s)
        {
        case '&':
            rep = "&amp;";
            break;
        case '<':
            rep = "&lt;";
            break;
        case '>':
            rep = "&gt;";
            break;
        case '"':
            rep = "&quot;";
            break;
        default:
            break;
        }
        if (rep)
        {
            pc_sb_put(b, rep);
        }
        else
        {
            if (b->len + 1 >= b->cap)
            {
                b->ok = PROTO_FALSE;
                return;
            }
            b->p[b->len] = *s;
            b->len++;
        }
    }
}

/** @brief Append a single character. */
static inline void pc_sb_ch(pc_sb *b, char c)
{
    if (!b->ok)
    {
        return;
    }
    if (b->len + 1 >= b->cap)
    {
        b->ok = PROTO_FALSE;
        return;
    }
    b->p[b->len++] = c;
}

/**
 * @brief Append @p v in @p base (10 or 16), left-padded with '0' to at least @p min_digits.
 *
 * The one shared engine behind the decimal and hex appenders: measure the field, bounds-check
 * once, then fill it back-to-front in place. Same shape as pc_sb_u32 so neither needs a
 * scratch array. @p min_digits is what carries a printf width like %08lx or %02d.
 */
static inline void pc_sb_uint(pc_sb *b, uint64_t v, unsigned base, unsigned min_digits)
{
    if (!b->ok)
    {
        return;
    }
    // A power-of-two base is a bit field, not an arithmetic one: one hex digit IS four bits and
    // one octal digit IS three, so extracting them is a shift and a mask. Only base 10 has to
    // divide. Naming the width says which it is; the constants are the digit width, not a
    // hand-tuned bit pattern.
    const unsigned bits_per_digit = (base == 16) ? 4u : (base == 8) ? 3u : 0u;
    const proto_bool power_of_two = bits_per_digit != 0;
    const uint64_t digit_mask = power_of_two ? ((1ull << bits_per_digit) - 1u) : 0u;

    // Decimal on a value that fits 32 bits uses 32-bit arithmetic: these cores are 32-bit, so a
    // uint64_t `/= 10` is a __udivdi3 libgcc call per digit. Hex and octal shift, so neither cares.
    const proto_bool narrow = !power_of_two && v <= 0xFFFFFFFFu;

    uint64_t probe = v;
    unsigned digits = 1;
    if (power_of_two)
    {
        while ((probe >>= bits_per_digit) != 0)
        {
            digits++;
        }
    }
    else if (narrow)
    {
        uint32_t p32 = (uint32_t)v;
        while (p32 >= 10u)
        {
            p32 /= 10u;
            digits++;
        }
    }
    else
    {
        while (probe >= 10)
        {
            probe /= 10;
            digits++;
        }
    }
    if (digits < min_digits)
    {
        digits = min_digits;
    }
    if (b->len + digits >= b->cap)
    {
        b->ok = PROTO_FALSE;
        return;
    }
    if (power_of_two)
    {
        for (unsigned i = digits; i-- > 0;)
        {
            b->p[b->len + i] = "0123456789abcdef"[v & digit_mask];
            v >>= bits_per_digit;
        }
    }
    else if (narrow)
    {
        uint32_t v32 = (uint32_t)v;
        for (unsigned i = digits; i-- > 0;)
        {
            b->p[b->len + i] = (char)('0' + (unsigned)(v32 % 10u));
            v32 /= 10u;
        }
    }
    else
    {
        for (unsigned i = digits; i-- > 0;)
        {
            b->p[b->len + i] = (char)('0' + (unsigned)(v % 10));
            v /= 10;
        }
    }
    b->len += digits;
}

/** @brief Append @p v as decimal, zero-padded to at least @p min_digits (printf "%0Nu"). */
static inline void pc_sb_u32w(pc_sb *b, uint32_t v, unsigned min_digits)
{
    pc_sb_uint(b, v, 10, min_digits);
}

/** @brief Append @p v as lowercase hex, zero-padded to at least @p min_digits (printf "%0Nx"). */
static inline void pc_sb_hex(pc_sb *b, uint64_t v, unsigned min_digits)
{
    pc_sb_uint(b, v, 16, min_digits);
}

/** @brief Append @p v as decimal (no leading zeros; "0" for zero). */
static inline void pc_sb_u32(pc_sb *b, uint32_t v)
{
    pc_sb_uint(b, v, 10, 1);
}

/** @brief Append @p v as decimal (64-bit). */
static inline void pc_sb_u64(pc_sb *b, uint64_t v)
{
    pc_sb_uint(b, v, 10, 1);
}

/** @brief Append @p v as signed decimal (64-bit), with a leading '-' when negative. */
static inline void pc_sb_i64(pc_sb *b, int64_t v)
{
    // Negating INT64_MIN overflows, so the magnitude is taken through unsigned arithmetic
    // rather than by negating the signed value.
    uint64_t mag = (v < 0) ? (uint64_t)(-(v + 1)) + 1u : (uint64_t)v;
    if (v < 0)
    {
        pc_sb_ch(b, '-');
    }
    pc_sb_uint(b, mag, 10, 1);
}

// 10^(2^k). Composing a power of ten from these costs at most 9 multiplies for the whole double
// range, instead of stepping one decade at a time.
static const double PC_POW10_BIN[9] = {1e1, 1e2, 1e4, 1e8, 1e16, 1e32, 1e64, 1e128, 1e256};

/** @brief True if @p v carries the IEEE-754 sign bit, including for -0.0 (a mask, not a divide). */
static inline proto_bool pc_signbit(double v)
{
    return (proto_raw_u64(&v) >> 63) != 0;
}

/** @brief True if @p v is an infinity: all exponent bits set, zero significand. */
static inline proto_bool pc_isinf(double v)
{
    const uint64_t bits = proto_raw_u64(&v);
    return ((bits >> 52) & 0x7FFu) == 0x7FFu && (bits & 0xFFFFFFFFFFFFFull) == 0;
}

/** @brief 10^p for p >= 0, by binary composition. */
static inline double pc_pow10i(int p)
{
    double r = 1.0;
    for (int k = 0; p != 0 && k < 9; k++, p >>= 1)
    {
        if (p & 1)
        {
            r *= PC_POW10_BIN[k];
        }
    }
    return r;
}

/**
 * @brief Decimal exponent of @p v (the X such that 10^X <= |v| < 10^(X+1)), for v > 0.
 *
 * The binary exponent is a field of the IEEE-754 encoding, so it is a mask and a shift rather
 * than something to converge on: multiplying it by log10(2) in fixed point (78913/2^18) gives the
 * decimal exponent to within one, which a single comparison settles.
 */
static inline int pc_dec_exp(double v)
{
    int be = (int)((proto_raw_u64(&v) >> 52) & 0x7FFu);
    int e;
    if (be == 0) // subnormal: no implicit leading 1, so scale into the normal range first
    {
        double s = v * 1e300;
        be = (int)((proto_raw_u64(&s) >> 52) & 0x7FFu);
        e = (int)(((int64_t)(be - 1023) * 78913) >> 18) - 300;
    }
    else
    {
        e = (int)(((int64_t)(be - 1023) * 78913) >> 18);
    }
    // The estimate is exact to +/-1; settle it by comparison rather than trusting the constant.
    double p = (e >= 0) ? pc_pow10i(e) : 1.0 / pc_pow10i(-e);
    if (p > v)
    {
        e--;
    }
    else if (v / 10.0 >= p)
    {
        e++;
    }
    return e;
}

/**
 * @brief Emit @p digits decimal digits of @p mant, with a '.' after the first @p point_after.
 *
 * Peels digits by division so no scratch array is needed. @p point_after == 0 or == @p digits
 * emits no point.
 */
static inline void pc_sb_digits(pc_sb *b, uint64_t mant, unsigned digits, unsigned point_after)
{
    uint64_t div = 1;
    for (unsigned i = 1; i < digits; i++)
    {
        div *= 10;
    }
    for (unsigned i = 0; i < digits; i++)
    {
        if (i == point_after && i != 0)
        {
            pc_sb_ch(b, '.');
        }
        pc_sb_ch(b, (char)('0' + (unsigned)((mant / div) % 10)));
        div /= 10;
    }
}

/**
 * @brief Append @p v with @p sig significant digits, choosing fixed or scientific form - the
 *        printf "%.<sig>g" rendering, including trailing-zero removal.
 *
 * Needed because %g is a wire format here, not a debug convenience: SCPI's NR2/NR3 numeric forms
 * and SenML/JSON numbers are both defined by what this produces, so the shape has to match what
 * the format string produced rather than being approximated with fixed decimals.
 *
 * Byte-identical to printf %.<sig>g for sig <= 10, which covers every call site in this library
 * (SCPI uses 10, JSON/SenML use the default 6). Above that the scaling is done in double, which
 * runs out of precision around 16 significant digits, so sig >= 15 can differ from libc in the
 * last digit.
 */
static inline void pc_sb_g(pc_sb *b, double v, unsigned sig)
{
    if (!b->ok)
    {
        return;
    }
    if (sig == 0)
    {
        sig = 1;
    }
    if (v != v) // NaN is the only value that is not equal to itself
    {
        pc_sb_put(b, "nan");
        return;
    }
    // `v < 0` is false for -0.0, but printf emits "-0" for it. The sign is a single bit of the
    // encoding, so it is read with a mask rather than recovered by dividing into it.
    if (pc_signbit(v))
    {
        pc_sb_ch(b, '-');
        v = -v;
    }
    if (pc_isinf(v))
    {
        pc_sb_put(b, "inf");
        return;
    }
    if (v == 0.0)
    {
        pc_sb_ch(b, '0');
        return;
    }

    const double v0 = v;
    const int e0 = pc_dec_exp(v);
    int e = e0;

    uint64_t limit = 1;
    for (unsigned i = 0; i < sig; i++)
    {
        limit *= 10;
    }
    // One scaling of the ORIGINAL value, not a round trip: multiply or divide, never both.
    int p = (int)sig - 1 - e;
    double scaled;
    if (p > 300 || p < -300)
    {
        // The scale factor alone would overflow, so bring the value to [1,10) first and scale
        // from there. Costs tie accuracy, which carries no meaning at an exponent this extreme.
        double norm = (e0 >= 0) ? v0 / pc_pow10i(e0) : v0 * pc_pow10i(-e0);
        scaled = norm * pc_pow10i((int)sig - 1);
    }
    else
    {
        double pw = pc_pow10i(p < 0 ? -p : p);
        scaled = (p >= 0) ? v0 * pw : v0 / pw;
    }

    // printf rounds half to even (2.5 at %.1g is "2", not "3"), so a plain +0.5 is wrong on ties.
    uint64_t mant = (uint64_t)scaled;
    double frac = scaled - (double)mant;
    if (frac > 0.5 || (frac == 0.5 && (mant & 1u)))
    {
        mant++;
    }
    if (mant >= limit) // the round carried into a new decade (9.9995 -> 10.000)
    {
        mant /= 10;
        e++;
    }
    // ...and the same in the other direction. If the scaling lands just under the decade the
    // mantissa has fewer than `sig` digits, and emitting it with the point after digit one
    // produces a malformed "0.999...e+300" whose leading digit is zero. A mantissa is always in
    // [1, 10), so pull it back into range and pay for it in the exponent.
    if (sig > 1 && mant < limit / 10)
    {
        mant = mant * 10 + 9; // the missing digit is unknown; 9 is the value that just rounded down
        e--;
    }

    unsigned digits = sig;
    while (digits > 1 && mant % 10 == 0) // %g strips trailing zeros
    {
        mant /= 10;
        digits--;
    }

    if (e < -4 || e >= (int)sig) // scientific, matching %g's threshold
    {
        pc_sb_digits(b, mant, digits, 1);
        pc_sb_ch(b, 'e');
        pc_sb_ch(b, e < 0 ? '-' : '+');
        unsigned mag = (unsigned)(e < 0 ? -e : e);
        pc_sb_u32w(b, mag, 2); // %g always emits at least two exponent digits
        return;
    }
    if (e >= (int)digits - 1) // integral: all significant digits, then padding zeros
    {
        pc_sb_digits(b, mant, digits, 0);
        for (int i = 0; i < e - (int)digits + 1; i++)
        {
            pc_sb_ch(b, '0');
        }
        return;
    }
    if (e >= 0)
    {
        pc_sb_digits(b, mant, digits, (unsigned)e + 1);
        return;
    }
    pc_sb_put(b, "0.");
    for (int i = 0; i < -e - 1; i++)
    {
        pc_sb_ch(b, '0');
    }
    pc_sb_digits(b, mant, digits, 0);
}

/**
 * @brief Append @p v with exactly @p decimals digits after the point (printf "%.<decimals>f").
 *
 * Byte-identical to printf for |v| < 2^64, which is the range a fixed-decimal reading occupies.
 * A larger magnitude falls back to the significant-digit form (see pc_sb_g) rather than being
 * rendered wrong: its exact %f expansion needs big-integer arithmetic.
 */
static inline void pc_sb_fixed(pc_sb *b, double v, unsigned decimals)
{
    if (!b->ok)
    {
        return;
    }
    if (v != v)
    {
        pc_sb_put(b, "nan");
        return;
    }
    // Same negative-zero rule as pc_sb_g, read from the sign bit.
    if (pc_signbit(v))
    {
        pc_sb_ch(b, '-');
        v = -v;
    }
    if (pc_isinf(v))
    {
        pc_sb_put(b, "inf");
        return;
    }
    // Beyond the 64-bit range the integer part cannot go through uint64 at all - the cast wraps.
    // An exact %f expansion of such a magnitude needs big-integer arithmetic (~309 digits), so it
    // falls back to the significant-digit form rather than rendering a wrapped value.
    if (v >= 18446744073709551616.0)
    {
        pc_sb_g(b, v, 10); // 10 is the precision pc_sb_g is exact to; asking for more only adds noise
        return;
    }
    double scale = 1.0;
    for (unsigned i = 0; i < decimals; i++)
    {
        scale *= 10.0;
    }
    // Split before scaling so a large magnitude does not overflow the 64-bit fraction math.
    double ip = (double)(uint64_t)v;
    uint64_t frac = (uint64_t)((v - ip) * scale + 0.5);
    if (frac >= (uint64_t)scale) // the fraction rounded up into the integer part
    {
        ip += 1.0;
        frac = 0;
    }
    pc_sb_u64(b, (uint64_t)ip);
    if (decimals)
    {
        pc_sb_ch(b, '.');
        pc_sb_u32w(b, (uint32_t)frac, decimals);
    }
}

/** @brief Append @p s as a JSON string literal: double-quoted, with `"` and `\` backslash-escaped. A NULL
 * @p s appends `""`. Control characters are passed through unescaped. */
static inline void pc_sb_json(pc_sb *b, const char *s)
{
    pc_sb_put(b, "\"");
    for (const char *p = s ? s : ""; *p; p++)
    {
        if (*p == '"' || *p == '\\')
        {
            if (b->len + 2 >= b->cap)
            {
                b->ok = PROTO_FALSE;
                return;
            }
            b->p[b->len++] = '\\';
            b->p[b->len++] = *p;
        }
        else if (b->len + 1 < b->cap)
        {
            b->p[b->len++] = *p;
        }
        else
        {
            b->ok = PROTO_FALSE;
        }
    }
    pc_sb_put(b, "\"");
}

/** @brief NUL-terminate and return the built length, or 0 if the build overflowed. */
static inline size_t pc_sb_finish(pc_sb *b)
{
    // cap == 0 owns no bytes at all, so even the terminator is out of bounds. Every appender
    // refuses to write into a zero-capacity buffer without latching `ok`, which left this the one
    // path that would still have written p[0].
    if (!b->ok || b->cap == 0)
    {
        return 0;
    }
    b->p[b->len] = '\0';
    return b->len;
}

#endif // PROTOCORE_MEMBUILD_H
