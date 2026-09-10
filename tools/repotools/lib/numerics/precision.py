# repotools-stamp: lib/numerics/precision.py d1df1b5cc862b55f
# repo_tools - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
"""Arbitrary-precision transform, for when float64 is the thing being measured.

A double carries 53 bits of mantissa. That puts the arithmetic's own noise floor around 300 dB
down, which is far below anything in a recording and far above nothing. When the question is what
the analysis invents and not what the signal contains, the transform has to be quieter than the
effect being looked for, and that means leaving hardware floating point behind.

Everything here is decimal.Decimal at a precision you choose. Nothing is imported that is not in
the standard library, so pi, sine and cosine are computed here and never looked up: the module
math has no more precision to give than the double it returns.

    prec = digits_for(1024)          # bits to decimal digits, with guard digits
    re, im = transform(samples, prec)

Cost grows steeply. A 256-point transform at 1024 bits is seconds; a 4096-point transform at the
same precision is minutes. That is the price of the floor, and it is why the fast path in dsp.py
stays the default.
"""

import decimal
import math

from decimal import Decimal

# Guard digits absorb the rounding of the last few operations so the answer is correct to the
# precision asked for and not to that precision minus whatever the algorithm loses.
GUARD = 16


def digits_for(bits):
    """Decimal digits that hold the given number of bits, plus guard."""
    return int(math.ceil(bits * 0.30102999566398119521)) + GUARD


def pi_at(prec):
    """Machin's formula, which converges fast enough that the series is not the slow part.

    pi = 16 arctan(1/5) - 4 arctan(1/239), each arctan by its Taylor series. Computed at extra
    precision and returned rounded, so the last digits of the result are not the series' own error.
    """
    with decimal.localcontext() as ctx:
        ctx.prec = prec + 10

        def arctan_inv(x):
            # arctan(1/x) = 1/x - 1/(3x^3) + 1/(5x^5) - ...
            x = Decimal(x)
            total = 1 / x
            term = total
            square = x * x
            step = 1
            while True:
                term = -term / square
                step += 2
                add = term / step
                if add == 0:
                    break
                before = total
                total += add
                if total == before:
                    break
            return total

        value = (16 * arctan_inv(5)) - (4 * arctan_inv(239))
        # Rounded here, inside the context. A unary plus after the with block has exited rounds to
        # whatever the default context is, which is 28 digits, and silently throws the rest away.
        ctx.prec = prec
        value = +value
    return value


def _cos_small(x, prec):
    """Taylor cosine for |x| <= pi/4, where the series converges quickly and cancels little."""
    with decimal.localcontext() as ctx:
        ctx.prec = prec + 10
        total = Decimal(1)
        term = Decimal(1)
        square = x * x
        n = 0
        while True:
            n += 2
            term = -term * square / (n * (n - 1))
            if term == 0:
                break
            before = total
            total += term
            if total == before:
                break
        ctx.prec = prec
        total = +total
    return total


def _sin_small(x, prec):
    with decimal.localcontext() as ctx:
        ctx.prec = prec + 10
        total = Decimal(x)
        term = Decimal(x)
        square = x * x
        n = 1
        while True:
            n += 2
            term = -term * square / (n * (n - 1))
            if term == 0:
                break
            before = total
            total += term
            if total == before:
                break
        ctx.prec = prec
        total = +total
    return total


def cos_sin(angle, prec, pi):
    """Cosine and sine of an angle, reduced into the octant where the series behaves.

    Reduction is done in the same precision as the answer. Reducing a large angle in double and
    then evaluating in Decimal would put a double's error into the argument and there is no way to
    get it back out. That is the usual way an extended-precision transform quietly turns back into
    a double one.
    """
    with decimal.localcontext() as ctx:
        ctx.prec = prec + 10
        two_pi = 2 * pi
        x = Decimal(angle) % two_pi
        if x > pi:
            x -= two_pi
        half = pi / 2
        quarter = pi / 4

        flip_cos = Decimal(1)
        flip_sin = Decimal(1)
        if x < 0:
            x = -x
            flip_sin = Decimal(-1)
        if x > half:
            x = pi - x
            flip_cos = Decimal(-1)
        if x > quarter:
            # cos(x) = sin(pi/2 - x), sin(x) = cos(pi/2 - x)
            y = half - x
            c = _sin_small(y, prec)
            s = _cos_small(y, prec)
        else:
            c = _cos_small(x, prec)
            s = _sin_small(x, prec)
        ctx.prec = prec
        c = +(flip_cos * c)
        s = +(flip_sin * s)
    return c, s


def turn(numerator, denominator, prec, pi):
    """Cosine and sine of 2*pi*numerator/denominator, with the angle formed at full precision.

    This exists because forming the angle is the easiest place to lose everything. Writing
    2 * pi * k / n at the call site evaluates in whatever context happens to be current, which is 28
    digits unless the caller thought about it, and no amount of precision inside the sine can
    recover an argument that arrived already rounded. Passing the fraction instead of the angle
    means the caller cannot make that mistake.
    """
    with decimal.localcontext() as ctx:
        ctx.prec = prec + 10
        angle = 2 * pi * Decimal(numerator) / Decimal(denominator)
    return cos_sin(angle, prec, pi)


def transform(values, prec):
    """Radix-2 Cooley-Tukey over Decimal, returning parallel real and imaginary lists.

    The twiddles for a stage are built by repeated multiplication from one root, exactly as the
    float version does, but at this precision that accumulates error over a long stage. Each root
    is therefore computed from its own angle instead. It costs more sines and buys the floor the
    whole module exists for.
    """
    n = len(values)
    if n & (n - 1):
        raise ValueError("length %d is not a power of two" % n)

    with decimal.localcontext() as ctx:
        ctx.prec = prec
        pi = pi_at(prec)

        re = [Decimal(str(one)) if not isinstance(one, Decimal) else one for one in values]
        im = [Decimal(0)] * n

        j = 0
        for i in range(1, n):
            bit = n >> 1
            while j & bit:
                j ^= bit
                bit >>= 1
            j |= bit
            if i < j:
                re[i], re[j] = re[j], re[i]
                im[i], im[j] = im[j], im[i]

        span = 2
        while span <= n:
            half = span >> 1
            roots = []
            for k in range(half):
                c, s = turn(-k, span, prec, pi)
                roots.append((c, s))
            for start in range(0, n, span):
                for k in range(half):
                    wr, wi = roots[k]
                    ar = re[start + k]
                    ai = im[start + k]
                    br = re[start + k + half]
                    bi = im[start + k + half]
                    tr = (br * wr) - (bi * wi)
                    ti = (br * wi) + (bi * wr)
                    re[start + k] = ar + tr
                    im[start + k] = ai + ti
                    re[start + k + half] = ar - tr
                    im[start + k + half] = ai - ti
            span <<= 1

    return re, im


def magnitudes(re, im, prec):
    """Absolute value of each bin, at the same precision."""
    with decimal.localcontext() as ctx:
        ctx.prec = prec
        return [((r * r) + (i * i)).sqrt() for r, i in zip(re, im)]


def floor_db(values, prec):
    """How far the smallest non-zero magnitude sits under the largest, in dB.

    Run on the transform of a pure tone this is the arithmetic's own noise floor, and it says what
    the precision actually bought.
    """
    with decimal.localcontext() as ctx:
        ctx.prec = prec
        top = max(values)
        if top == 0:
            return 0.0
        small = min(one for one in values if one > 0)
        return float(20 * (small / top).log10())
