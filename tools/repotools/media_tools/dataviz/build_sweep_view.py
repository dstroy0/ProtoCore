# repotools-stamp: media_tools/dataviz/build_sweep_view.py 59f2c5cc158fff1c
# repo_tools - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
"""Sweeps the analysis and draws every setting at once, so an artifact of the analysis is visible.

A spectrogram is a picture of a signal and a picture of the window that made it, and nothing in the
picture says which is which. This runs the same signal through a whole range of settings, puts each
setting on its own row of one solid, and adds a field that is each setting minus the consensus of
all of them. A feature that stands in the same place at every setting is in the signal. A feature
that moves, or appears at one setting and not the next, is the analysis talking about itself.

    python tools/view/build_sweep_view.py take.wav
    python tools/view/build_sweep_view.py --synth --sweep pad
    python tools/view/build_sweep_view.py --synth --sweep window --top 4000
    python tools/view/build_sweep_view.py --synth --sweep fft --exact 256

  --sweep WHAT   fft, pad or window. Default fft.
  --fft N        frame size when it is not the swept axis. Default 1024.
  --pad K        zero-pad factor when it is not the swept axis. Default 4.
  --window W     window when it is not the swept axis. Default hann.
  --grid N       points on the common frequency axis. Default 512.
  --top HZ       highest frequency drawn. Default 8000.
  --average M    frames averaged per setting. Default 16, which steadies the noise floor.
  --exact BITS   run the sweep in extended precision as well, at frame sizes small enough to
                 afford it, so the arithmetic's own floor is below anything being looked for.
  --synth        use a generated signal: two tones beating at 3 Hz, a sweep from 1 kHz to 3 kHz, a
                 quiet 7 kHz tone and white noise.
  --out FILE     where to write.

Every setting is resampled onto one frequency axis in Hz, because settings do not share a bin
spacing and comparing bin 40 of one against bin 40 of another compares two different frequencies.
The axis is Hz for all of them, since Hz is what they share.
"""

import os, sys
_at = os.path.dirname(os.path.abspath(__file__))
while _at != os.path.dirname(_at) and not os.path.isdir(os.path.join(_at, "lib", "repotools")):
    _at = os.path.dirname(_at)
sys.path.insert(0, os.path.join(_at, "lib"))

import io
import json
import math
import os
import sys
import time

from numerics import dsp
import settings

HERE = os.path.dirname(os.path.abspath(__file__))
TEMPLATE = os.path.join(HERE, "voxel_view_template.html")

FFT_SIZES = (64, 128, 256, 512, 1024, 2048, 4096)
PAD_FACTORS = (1, 2, 4, 8, 16, 32, 64)


def sample_grid(mags, hz_per_bin, grid_hz):
    """Puts one spectrum onto the shared frequency axis, linearly between neighboring bins."""
    out = []
    top_bin = len(mags) - 1
    for hz in grid_hz:
        where = hz / hz_per_bin
        low = int(math.floor(where))
        if low >= top_bin:
            out.append(mags[top_bin])
            continue
        if low < 0:
            out.append(mags[0])
            continue
        part = where - low
        out.append((mags[low] * (1.0 - part)) + (mags[low + 1] * part))
    return out


def average_spectrum(values, size, pad_to, kind, count):
    """Mean magnitude over several frames, which steadies the floor without moving a peak."""
    win = dsp.window(kind, size)
    hop = max(1, size // 2)
    starts = list(range(0, max(1, len(values) - size + 1), hop))[:count]
    if not starts:
        starts = [0]
    total = None
    for start in starts:
        chunk = values[start:start + size]
        if len(chunk) < size:
            chunk = chunk + [0.0] * (size - len(chunk))
        one = dsp.spectrum(chunk, win, pad_to)
        if total is None:
            total = one
        else:
            for at in range(len(total)):
                total[at] += one[at]
    return [one / len(starts) for one in total]


def main():
    argv = sys.argv[1:]

    def number(name, fallback):
        return int(argv[argv.index(name) + 1]) if name in argv else fallback

    def text(name, fallback=None):
        return argv[argv.index(name) + 1] if name in argv else fallback

    source = None
    if argv and not argv[0].startswith("-"):
        source = argv[0]
    if source is None and "--synth" not in argv:
        sys.stderr.write(__doc__)
        return 2

    which = text("--sweep", "fft")
    if which not in ("fft", "pad", "window"):
        sys.stderr.write("--sweep must be fft, pad or window\n")
        return 1

    fft_size = dsp.next_power(number("--fft", 1024))
    pad = max(1, number("--pad", 4))
    kind = text("--window", "hann")
    grid_n = number("--grid", 512)
    top_hz = number("--top", 8000)
    average = number("--average", 16)
    exact_bits = number("--exact", 0)

    rate = 44100
    if source:
        if not os.path.exists(source):
            sys.stderr.write("no such file: %s\n" % source)
            return 1
        values, rate = dsp.read_wave(source, 20 * 96000)
        name = os.path.basename(source)
    else:
        values = dsp.synth(rate * 4, rate)
        name = "generated signal"

    if which == "fft":
        swept = [one for one in FFT_SIZES if one <= len(values)]
        label_of = lambda one: "%d" % one
    elif which == "pad":
        swept = list(PAD_FACTORS)
        label_of = lambda one: "%dx" % one
    else:
        swept = list(dsp.WINDOWS)
        label_of = lambda one: one

    if not swept:
        sys.stderr.write("the signal is too short for any setting\n")
        return 1

    grid_hz = [top_hz * i / float(grid_n - 1) for i in range(grid_n)]

    began = time.time()
    rows = []
    for one in swept:
        size = one if which == "fft" else fft_size
        factor = one if which == "pad" else pad
        shape = one if which == "window" else kind
        pad_to = dsp.next_power(size * factor)
        mags = average_spectrum(values, size, pad_to, shape, average)
        rows.append(sample_grid(mags, rate / float(pad_to), grid_hz))
        sys.stderr.write("  %s %-8s\r" % (which, label_of(one)))
        sys.stderr.flush()
    sys.stderr.write(" " * 30 + "\r")

    loudest = max(max(row) for row in rows) or 1.0

    level = []
    linear = []
    for row in rows:
        level.append([round(max(-120.0, 20.0 * math.log10(one / loudest)) if one > 0 else -120.0, 3)
                      for one in row])
        linear.append([round(one / loudest, 6) for one in row])

    # The consensus at each frequency, and how far each setting departs from it. This is the field
    # the tool exists for: it is zero where the settings agree, and everything that is left is the
    # analysis and not the signal.
    consensus = []
    for at in range(grid_n):
        column = sorted(row[at] for row in level)
        middle = len(column) // 2
        consensus.append(column[middle] if len(column) % 2
                         else (column[middle - 1] + column[middle]) / 2.0)
    departure = [[round(row[at] - consensus[at], 3) for at in range(grid_n)] for row in level]

    fields = [
        {"key": "level", "label": "Level dB", "axis": which, "rows": level},
        {"key": "linear", "label": "Linear", "axis": which, "rows": linear},
        {"key": "departure", "label": "From consensus", "axis": which, "rows": departure},
    ]

    exact_note = ""
    if exact_bits:
        # Extended precision on the smallest setting only. The point is not to sweep at this cost
        # but to establish that the floor being looked at is the signal's and not the arithmetic's.
        from numerics import precision as extended
        prec = extended.digits_for(exact_bits)
        size = min(256, min(swept) if which == "fft" else fft_size)
        size = dsp.next_power(size)
        pi = extended.pi_at(prec)

        # The floor has to be measured on a signal whose exact spectrum is known in advance, which
        # is a tone sitting on a bin: one non-zero magnitude and the rest zero. Everything the
        # transform reports in the other bins is the arithmetic's own noise, alone.
        #
        # Measuring it on the recording instead gives the smallest bin the recording happens to
        # contain. That is the signal's dynamic range and is thousands of dB louder. The number
        # looks like an answer and is not one.
        at_bin = max(1, size // 8)
        began_exact = time.time()
        tone = [extended.turn(at_bin * i, size, prec, pi)[0] for i in range(size)]
        re, im = extended.transform(tone, prec)
        mags = extended.magnitudes(re, im, prec)
        top = max(mags)
        others = [one for where, one in enumerate(mags) if where not in (at_bin, size - at_bin)]
        worst = max(others)
        floor = float(20 * (worst / top).log10()) if worst > 0 else float("-inf")
        exact_note = (" Measured rather than claimed: a %d-point transform of a tone on a bin, at "
                      "%d bits, puts every other bin %.0f dB down, which is %.0f effective bits "
                      "against 51 for float64. Anything above that floor in the other fields is the "
                      "signal or the window and not the arithmetic. That measurement took %.1f s."
                      % (size, exact_bits, floor, -floor / 6.02, time.time() - began_exact))

    took = time.time() - began
    payload = {
        "depth": grid_n,
        "depthLabel": "Hz (0 to %d)" % top_hz,
        "valueLabel": "level",
        "eyebrow": "Analysis sweep - rendered as a solid",
        "title": "%s swept by %s" % (name, which),
        "blurb": ("%s analyzed at %d settings of %s, every one resampled onto the same %d point "
                  "frequency axis from 0 to %d Hz. Depth runs left to right as frequency; the other "
                  "horizontal axis is the setting. Settings do not share a bin spacing, so they are "
                  "put on a common axis in Hz before anything is compared."
                  % (name, len(swept), which, grid_n, top_hz)),
        "noteTitle": "What moves with the setting is the setting",
        "note": ("A feature standing in the same place at every setting is in the signal. One that "
                 "moves, or appears at one setting and not the next, is the analysis describing "
                 "itself. The third field is each setting minus the median of all of them, so it is "
                 "zero where they agree and shows only the disagreement." + exact_note),
        "settings": settings.collect(sys.argv[1:]),
        "fields": fields,
        "swept": [label_of(one) for one in swept],
    }

    with io.open(TEMPLATE, encoding="utf-8") as handle:
        page = handle.read()
    if "</script>" not in page:
        raise SystemExit("template is truncated: the script tag is never closed")
    page = page.replace("/*VOXEL_DATA*/null", json.dumps(payload, separators=(",", ":")))

    target = text("--out") or os.path.join(HERE, "sweep_view.html")
    with io.open(target, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(page)

    print("wrote %s (%.1f KB)" % (target, os.path.getsize(target) / 1024.0))
    print("  swept %s over %s" % (which, ", ".join(label_of(one) for one in swept)))
    print("  %d point axis to %d Hz, %d frames averaged per setting" % (grid_n, top_hz, average))
    print("  %.1f s" % took)
    if exact_note:
        print(" %s" % exact_note.strip())
    return 0


if __name__ == "__main__":
    sys.exit(main())
