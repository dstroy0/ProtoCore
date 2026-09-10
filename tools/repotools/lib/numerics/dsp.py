# repotools-stamp: lib/numerics/dsp.py 6272286875e86cee
# repo_tools - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
"""Transforms, windows and signal sources, standard library only.

Shared by build_sound_view.py and build_sweep_view.py.
"""

import cmath
import math
import struct
import wave

TAU = 2.0 * math.pi


def next_power(n):
    size = 1
    while size < n:
        size *= 2
    return size


def fft(values):
    """Iterative radix-2 Cooley-Tukey, in place, on a list of complex.

    Length must be a power of two. Twiddles are built once per stage and not per butterfly:
    calling exp inside the inner loop costs more than the transform itself in pure Python.
    """
    n = len(values)
    if n & (n - 1):
        raise ValueError("length %d is not a power of two" % n)

    # Bit-reversal permutation.
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            values[i], values[j] = values[j], values[i]

    span = 2
    while span <= n:
        step = cmath.exp(complex(0.0, -TAU / span))
        half = span >> 1
        twiddle = [1.0 + 0.0j] * half
        for k in range(1, half):
            twiddle[k] = twiddle[k - 1] * step
        for start in range(0, n, span):
            for k in range(half):
                a = values[start + k]
                b = values[start + k + half] * twiddle[k]
                values[start + k] = a + b
                values[start + k + half] = a - b
        span <<= 1
    return values


# Windows, by the sidelobe response that decides which artifacts an analysis invents.
def window(kind, n):
    if n < 2:
        return [1.0] * n
    last = n - 1.0
    if kind == "rect":
        return [1.0] * n
    if kind == "hann":
        return [0.5 - (0.5 * math.cos(TAU * i / last)) for i in range(n)]
    if kind == "hamming":
        return [0.54 - (0.46 * math.cos(TAU * i / last)) for i in range(n)]
    if kind == "blackman":
        return [0.42 - (0.5 * math.cos(TAU * i / last))
                + (0.08 * math.cos(2 * TAU * i / last)) for i in range(n)]
    if kind == "flattop":
        # Poor resolution, excellent amplitude accuracy. The one to reach for when the height of a
        # peak matters more than telling two peaks apart.
        a = (0.21557895, 0.41663158, 0.277263158, 0.083578947, 0.006947368)
        out = []
        for i in range(n):
            t = TAU * i / last
            out.append(a[0] - (a[1] * math.cos(t)) + (a[2] * math.cos(2 * t))
                       - (a[3] * math.cos(3 * t)) + (a[4] * math.cos(4 * t)))
        return out
    raise ValueError("unknown window: %s" % kind)


WINDOWS = ("rect", "hann", "hamming", "blackman", "flattop")


def read_wave(path, limit):
    """Mono samples in [-1, 1] and the sample rate. Channels are averaged."""
    with wave.open(path, "rb") as handle:
        channels = handle.getnchannels()
        width = handle.getsampwidth()
        rate = handle.getframerate()
        count = handle.getnframes()
        if limit and count > limit:
            count = limit
        raw = handle.readframes(count)

    if width == 1:
        # 8-bit wav is unsigned.
        values = [(one - 128) / 128.0 for one in raw]
    elif width == 2:
        values = [one / 32768.0 for one in struct.unpack("<%dh" % (len(raw) // 2), raw)]
    elif width == 3:
        values = []
        for at in range(0, len(raw) - 2, 3):
            one = raw[at] | (raw[at + 1] << 8) | (raw[at + 2] << 16)
            if one & 0x800000:
                one -= 0x1000000
            values.append(one / 8388608.0)
    elif width == 4:
        values = [one / 2147483648.0
                  for one in struct.unpack("<%di" % (len(raw) // 4), raw)]
    else:
        raise ValueError("unsupported sample width: %d bytes" % width)

    if channels > 1:
        mixed = []
        for at in range(0, len(values) - channels + 1, channels):
            mixed.append(sum(values[at:at + channels]) / channels)
        values = mixed
    return values, rate


def synth(count, rate, seed=7):
    """Two tones close enough to beat, a drifting third, and noise under all of it.

    A made signal whose contents are known is the only way to tell an artifact of the analysis from
    something that was there: every feature here can be named in advance.
    """
    state = seed
    out = []
    for i in range(count):
        t = i / float(rate)
        # 440 and 443 beat at 3 Hz.
        value = 0.42 * math.sin(TAU * 440.0 * t)
        value += 0.34 * math.sin(TAU * 443.0 * t)
        # A tone sliding from 1 kHz to 3 kHz, the thing a sweep is meant to follow.
        value += 0.22 * math.sin(TAU * (1000.0 + (2000.0 * t / max(1e-9, count / float(rate)))) * t)
        # A quiet high tone, near the level where windowing decides whether it is visible.
        value += 0.02 * math.sin(TAU * 7000.0 * t)
        # White noise from a small linear congruential generator, so the file needs no imports and
        # the same seed gives the same noise.
        state = (1103515245 * state + 12345) & 0x7FFFFFFF
        value += 0.03 * (((state / 1073741824.0) - 1.0))
        out.append(value)
    return out


def spectrum(chunk, win, pad_to):
    """One magnitude spectrum: window, zero-pad, transform, keep the real half.

    Zero-padding does not add resolution. It interpolates the spectrum onto a finer grid, and that
    interpolation stops a peak falling between two bins from being reported at the wrong height and
    the wrong place. That is the oversampling: the transform is longer than the data it holds.
    """
    n = len(chunk)
    data = [complex(chunk[i] * win[i], 0.0) for i in range(n)]
    if pad_to > n:
        data.extend([0j] * (pad_to - n))
    fft(data)
    half = len(data) // 2
    return [abs(one) for one in data[:half]]
