// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file membuild.c
 * @brief The bounded no-heap builder - see membuild.h.
 *
 * Each append tests @c ok, measures the field it is about to write, checks the one bound against
 * @c cap, then writes in place. A number is filled back-to-front where it lands, so nothing here
 * needs a scratch array. The float renderings take the value apart as the sign, exponent and
 * mantissa it is and scale an `n * 2^s` pair by whole decades, so the digits come off integers.
 *
 * The names @ref Sb aliases are defined here. Everything else in this file has internal linkage.
 */

#include "mmgr/membuild.h"
#include "mmgr/float_bits.h" // proto_dbl_sign / proto_dbl_exp / proto_dbl_mant - the field reads
#include "mmgr/protostr.h"   // str.len - a word per test, bounded by a known width
#include "mmgr/rawmemcpy.h"  // proto_raw_read - the span move protocore_sb_put_n is built on
#include "shared/hex/hex.h"  // PROTOCORE_HEX: the shared digit tables

/// @brief Working width of the `n * 2^s` pair below: four bits clear of the top so a decade fits.
#define PROTOCORE_G_WORK_BITS 58u

void protocore_sb_put_n(protocore_sb *b, const char *s, size_t sl)
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
    // The destination is a position inside the caller's buffer, so the exact mover: the bytes past
    // the append still belong to whoever owns the rest of it.
    proto_raw_read(b->p + b->len, s, sl);
    b->len += sl;
}

void protocore_sb_put(protocore_sb *b, const char *s)
{
    if (!b->ok)
    {
        return;
    }
    protocore_sb_put_n(b, s, str.len(s, b->cap));
}

void protocore_sb_put_clip(protocore_sb *b, const char *s)
{
    if (!b->ok || !s || b->len + 1 >= b->cap)
    {
        return;
    }
    size_t room = b->cap - b->len - 1;
    size_t sl = str.len(s, room);
    // The destination is a position inside the caller's buffer, so the exact mover: the bytes past
    // the append still belong to whoever owns the rest of it.
    proto_raw_read(b->p + b->len, s, sl);
    b->len += sl;
}

void protocore_sb_u64_clip(protocore_sb *b, uint64_t v, uint8_t columns)
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

void protocore_sb_xml(protocore_sb *b, const char *s)
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
            protocore_sb_put(b, rep);
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

void protocore_sb_ch(protocore_sb *b, char c)
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

void protocore_sb_uint(protocore_sb *b, uint64_t v, unsigned base, unsigned min_digits)
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
    // uint64_t `/= 10` is a software divide call per digit. Hex and octal shift, so neither cares.
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
            b->p[b->len + i] = PROTOCORE_HEX.lower[v & digit_mask];
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

void protocore_sb_u32w(protocore_sb *b, uint32_t v, unsigned min_digits)
{
    protocore_sb_uint(b, v, 10, min_digits);
}

void protocore_sb_hex(protocore_sb *b, uint64_t v, unsigned min_digits)
{
    protocore_sb_uint(b, v, 16, min_digits);
}

void protocore_sb_u32(protocore_sb *b, uint32_t v)
{
    protocore_sb_uint(b, v, 10, 1);
}

void protocore_sb_u64(protocore_sb *b, uint64_t v)
{
    protocore_sb_uint(b, v, 10, 1);
}

void protocore_sb_i64(protocore_sb *b, int64_t v)
{
    // Negating INT64_MIN overflows, so the magnitude is taken through unsigned arithmetic
    // rather than by negating the signed value.
    uint64_t mag = (v < 0) ? (uint64_t)(-(v + 1)) + 1u : (uint64_t)v;
    if (v < 0)
    {
        protocore_sb_ch(b, '-');
    }
    protocore_sb_uint(b, mag, 10, 1);
}

proto_bool protocore_signbit(double v)
{
    return proto_dbl_sign(v) != 0u;
}

proto_bool protocore_isinf(double v)
{
    return proto_dbl_exp(v) == 0x7FFu && proto_dbl_mant(v) == 0u;
}

proto_bool protocore_isnan(double v)
{
    return proto_dbl_exp(v) == 0x7FFu && proto_dbl_mant(v) != 0u;
}

// Emit @p digits decimal digits of @p mant, with a '.' after the first @p point_after. Peels digits
// by division so no scratch array is needed. @p point_after of 0 or of @p digits emits no point.
static void sb_digits(protocore_sb *b, uint64_t mant, unsigned digits, unsigned point_after)
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
            protocore_sb_ch(b, '.');
        }
        protocore_sb_ch(b, (char)('0' + (unsigned)((mant / div) % 10)));
        div /= 10;
    }
}

// Renormalize `n * 2^s` so @p n sits in the top half of the working width.
static void g_renorm(proto_u64 *n, proto_i32 *s)
{
    if (*n == 0u)
    {
        return;
    }
    while (*n >= (1ull << PROTOCORE_G_WORK_BITS))
    {
        *n >>= 1;
        *s += 1;
    }
    while (*n < (1ull << (PROTOCORE_G_WORK_BITS - 1u)))
    {
        *n <<= 1;
        *s -= 1;
    }
}

// Multiply the pair by ten.
static void g_mul10(proto_u64 *n, proto_i32 *s)
{
    *n *= 10u;
    g_renorm(n, s);
}

// Divide the pair by ten, shifting up first so the divide keeps the low bits.
static void g_div10(proto_u64 *n, proto_i32 *s)
{
    *n <<= 4;
    *s -= 4;
    *n /= 10u;
    g_renorm(n, s);
}

// The integer part of `n * 2^s`, rounded half to even on the bits below the point. The tie is the
// case where those bits are one followed by zeros, and it goes toward the even digit.
static proto_u64 g_round(proto_u64 n, proto_i32 s)
{
    if (s >= 0)
    {
        return n << (unsigned)s;
    }
    unsigned sh = (unsigned)(-s);
    if (sh >= 64u)
    {
        return 0u;
    }
    proto_u64 r = n >> sh;
    proto_u64 rem = n - (r << sh);
    proto_u64 half = 1ull << (sh - 1u);
    if (rem > half || (rem == half && (r & 1u) != 0u))
    {
        r++;
    }
    return r;
}

void protocore_sb_g(protocore_sb *b, double v, unsigned sig)
{
    if (!b->ok)
    {
        return;
    }
    if (sig == 0)
    {
        sig = 1;
    }
    if (protocore_isnan(v))
    {
        protocore_sb_put(b, "nan");
        return;
    }
    // `v < 0` is false for -0.0, but printf emits "-0" for it. The sign is a single bit of the
    // encoding, so it is read with a mask rather than recovered by dividing into it.
    if (protocore_signbit(v))
    {
        protocore_sb_ch(b, '-');
        v = -v;
    }
    if (protocore_isinf(v))
    {
        protocore_sb_put(b, "inf");
        return;
    }
    proto_u64 be = proto_dbl_exp(v);
    proto_u64 n = proto_dbl_mant(v);
    if (be == 0u && n == 0u)
    {
        protocore_sb_ch(b, '0');
        return;
    }

    // The value IS n * 2^s. A normal carries an implicit 1 above the stored mantissa; a subnormal
    // stores no leading 1 and denotes an exponent field of 1.
    proto_i32 s = 1 - PROTO_DBL_BIAS - (proto_i32)PROTO_DBL_MANT_BITS;
    if (be != 0u)
    {
        n |= 1ull << PROTO_DBL_MANT_BITS;
        s = (proto_i32)be - PROTO_DBL_BIAS - (proto_i32)PROTO_DBL_MANT_BITS;
    }
    g_renorm(&n, &s);

    proto_u64 limit = 1u;
    for (unsigned i = 0; i < sig; i++)
    {
        limit *= 10u;
    }

    // The binary exponent gives the decimal one to within a step, log10(2) being 78913/2^18. The
    // renormalize left the value in [2^(WORK-1+s), 2^(WORK+s)), and the settle below closes the step.
    int e = (int)(((int64_t)((int)PROTOCORE_G_WORK_BITS - 1 + s) * 78913) >> 18);

    // Whole decades onto the pair, so the scale is a multiply or a divide by ten and never a power
    // of ten that leaves the range.
    int p = (int)sig - 1 - e;
    while (p > 0)
    {
        g_mul10(&n, &s);
        p--;
    }
    while (p < 0)
    {
        g_div10(&n, &s);
        p++;
    }

    // A decade out either way is one more step on the pair, then the digits are taken again from
    // the unrounded value rather than rounded a second time.
    proto_u64 mant = g_round(n, s);
    for (unsigned guard = 0; guard < 4u; guard++)
    {
        if (mant >= limit) // the round carried into a new decade (9.9995 -> 10.000)
        {
            g_div10(&n, &s);
            e++;
        }
        else if (sig > 1u && mant < limit / 10u)
        {
            g_mul10(&n, &s);
            e--;
        }
        else
        {
            break;
        }
        mant = g_round(n, s);
    }

    unsigned digits = sig;
    while (digits > 1 && mant % 10 == 0) // %g strips trailing zeros
    {
        mant /= 10;
        digits--;
    }

    if (e < -4 || e >= (int)sig) // scientific, matching %g's threshold
    {
        sb_digits(b, mant, digits, 1);
        protocore_sb_ch(b, 'e');
        protocore_sb_ch(b, e < 0 ? '-' : '+');
        unsigned mag = (unsigned)(e < 0 ? -e : e);
        protocore_sb_u32w(b, mag, 2); // %g always emits at least two exponent digits
        return;
    }
    if (e >= (int)digits - 1) // integral: all significant digits, then padding zeros
    {
        sb_digits(b, mant, digits, 0);
        for (int i = 0; i < e - (int)digits + 1; i++)
        {
            protocore_sb_ch(b, '0');
        }
        return;
    }
    if (e >= 0)
    {
        sb_digits(b, mant, digits, (unsigned)e + 1);
        return;
    }
    protocore_sb_put(b, "0.");
    for (int i = 0; i < -e - 1; i++)
    {
        protocore_sb_ch(b, '0');
    }
    sb_digits(b, mant, digits, 0);
}

void protocore_sb_fixed(protocore_sb *b, double v, unsigned decimals)
{
    if (!b->ok)
    {
        return;
    }
    if (protocore_isnan(v))
    {
        protocore_sb_put(b, "nan");
        return;
    }
    // Same negative-zero rule as protocore_sb_g, read from the sign bit.
    if (protocore_signbit(v))
    {
        protocore_sb_ch(b, '-');
        v = -v;
    }
    if (protocore_isinf(v))
    {
        protocore_sb_put(b, "inf");
        return;
    }
    // Beyond the 64-bit range the integer part cannot go through uint64 at all - the cast wraps.
    // An exact %f expansion of such a magnitude needs big-integer arithmetic (~309 digits), so it
    // falls back to the significant-digit form rather than rendering a wrapped value. "At or above
    // 2^64" is the exponent field alone: 64 unbiased is 1023 + 64 biased.
    if (proto_dbl_exp(v) >= (PROTO_DBL_BIAS + 64))
    {
        protocore_sb_g(b, v, 10); // 10 is the precision protocore_sb_g is exact to
        return;
    }
    if (decimals > 18u) // 10^19 leaves the 64-bit range the carry check below is done in
    {
        decimals = 18u;
    }
    proto_u64 scale = 1u;
    for (unsigned i = 0; i < decimals; i++)
    {
        scale *= 10u;
    }

    // The value IS mant * 2^exp2, so the digits come off those integers. A normal carries an
    // implicit 1 above the stored mantissa; a subnormal stores no leading 1 and denotes an
    // exponent field of 1.
    proto_u64 mant = proto_dbl_mant(v);
    proto_u64 be = proto_dbl_exp(v);
    proto_i32 exp2 = 1 - PROTO_DBL_BIAS - (proto_i32)PROTO_DBL_MANT_BITS;
    if (be != 0u)
    {
        mant |= 1ull << PROTO_DBL_MANT_BITS;
        exp2 = (proto_i32)be - PROTO_DBL_BIAS - (proto_i32)PROTO_DBL_MANT_BITS;
    }

    proto_u64 ip = 0u;
    proto_u64 frac = 0u;
    if (exp2 >= 0)
    {
        ip = mant << (unsigned)exp2; // an integer already: nothing below the point to round
    }
    else
    {
        unsigned shift = (unsigned)(-exp2);
        proto_u64 num = mant;
        if (shift < 64u)
        {
            ip = mant >> shift;
            num = mant - (ip << shift);
        }
        // Each digit costs one multiply and one shift, so the numerator must stay clear of the top
        // four bits. Below 2^-60 of the value there is no digit left to decide.
        if (shift > 60u)
        {
            num >>= (shift - 60u);
            shift = 60u;
        }
        const proto_u64 den = 1ull << shift;
        for (unsigned i = 0; i < decimals; i++)
        {
            num *= 10u;
            proto_u64 digit = num >> shift;
            num -= digit << shift;
            frac = frac * 10u + digit;
        }
        // What is left is the remainder over the same denominator, so the tie is num*2 == den, and
        // printf breaks it toward the even digit.
        proto_u64 twice = num * 2u;
        if (twice > den || (twice == den && (frac & 1u) != 0u))
        {
            frac++;
        }
    }
    if (frac >= scale) // the fraction rounded up into the integer part
    {
        ip++;
        frac = 0u;
    }
    protocore_sb_u64(b, ip);
    if (decimals)
    {
        protocore_sb_ch(b, '.');
        protocore_sb_uint(b, frac, 10, decimals);
    }
}

// RFC 8259 sec 7 gives a two-character escape to five of the control characters; the letter each
// one takes, indexed by the code point, and 0 where the grammar names none.
static const char JSON_CTRL_ESC[32] = {0, 0, 0, 0, 0, 0, 0, 0, 'b', 't', 'n', 0, 'f', 'r', 0, 0,
                                       0, 0, 0, 0, 0, 0, 0, 0, 0,   0,   0,   0, 0,   0,   0, 0};

void protocore_sb_json(protocore_sb *b, const char *s)
{
    static const char HEX[] = "0123456789abcdef";

    if (!b->ok)
    {
        return; // the loop below stores through b->p directly, so the latch is tested here
    }
    protocore_sb_put(b, "\"");
    // RFC 8259 sec 7: a quotation mark, a reverse solidus and the control characters U+0000 through
    // U+001F MUST be escaped. The five with a named two-character form take it; the rest take
    // \u00XX, the six-character form sec 7 gives for a Basic Multilingual Plane code point.
    for (const char *p = s ? s : ""; *p; p++)
    {
        const unsigned char c = (unsigned char)*p;
        const char two = (c == '"' || c == '\\') ? (char)c : (c < 0x20u ? JSON_CTRL_ESC[c] : 0);
        if (two)
        {
            if (b->len + 2 >= b->cap)
            {
                b->ok = PROTO_FALSE;
                return;
            }
            b->p[b->len++] = '\\';
            b->p[b->len++] = two;
        }
        else if (c < 0x20u)
        {
            if (b->len + 6 >= b->cap)
            {
                b->ok = PROTO_FALSE;
                return;
            }
            b->p[b->len++] = '\\';
            b->p[b->len++] = 'u';
            b->p[b->len++] = '0';
            b->p[b->len++] = '0';
            b->p[b->len++] = HEX[(c >> 4) & 0xFu];
            b->p[b->len++] = HEX[c & 0xFu];
        }
        else if (b->len + 1 < b->cap)
        {
            b->p[b->len++] = (char)c;
        }
        else
        {
            b->ok = PROTO_FALSE;
        }
    }
    protocore_sb_put(b, "\"");
}

size_t protocore_sb_finish(protocore_sb *b)
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
