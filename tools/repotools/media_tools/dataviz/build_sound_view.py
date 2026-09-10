# repotools-stamp: media_tools/dataviz/build_sound_view.py 15c7d4b4f710c5c7
# repo_tools - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
"""Turns a sound into a solid: frequency against time, as a surface you can turn.

    python tools/view/build_sound_view.py take.wav
    python tools/view/build_sound_view.py take.wav --fft 2048 --pad 8 --window blackman
    python tools/view/build_sound_view.py --synth

  --fft N      samples per frame. Default 1024.
  --pad K      zero-pad factor. The transform is fft times this. Default 4.
  --hop H      samples between frames. Default fft/4, which is 75 percent overlap.
  --window W   rect, hann, hamming, blackman or flattop. Default hann.
  --frames M   most frames to keep. Default 256.
  --top HZ     highest frequency drawn. Default 8000.
  --seconds S  most seconds of audio to read. Default 20.
  --synth      use a generated signal instead of a file: two tones beating at 3 Hz, a sweep from
               1 kHz to 3 kHz, a quiet 7 kHz tone and white noise under all of it.
  --out FILE   where to write.

Four readings of the same frames:

  level      magnitude in dB, floored 90 dB under the loudest cell
  linear     magnitude as it is, which buries everything quiet and is the honest default
  flux       how much each bin changed since the frame before, which is where onsets live
  slope      difference between neighboring bins, which sharpens a peak and kills a plateau

Zero-padding is the oversampling. It adds no resolution: two tones closer together than the frame
can separate stay unseparated however large the pad. What it does is interpolate the spectrum onto
a finer grid. A peak sitting between two bins is then drawn at its real height and position
and not smeared across the two. Raise --fft to separate tones, raise --pad to place them.
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


def main():
    argv = sys.argv[1:]

    def number(name, fallback):
        return int(argv[argv.index(name) + 1]) if name in argv else fallback

    def text(name):
        return argv[argv.index(name) + 1] if name in argv else None

    source = None
    if argv and not argv[0].startswith("-"):
        source = argv[0]
    if source is None and "--synth" not in argv:
        sys.stderr.write(__doc__)
        return 2

    fft_size = dsp.next_power(number("--fft", 1024))
    pad = max(1, number("--pad", 4))
    hop = number("--hop", max(1, fft_size // 4))
    frames_max = number("--frames", 256)
    top_hz = number("--top", 8000)
    seconds = number("--seconds", 20)
    kind = text("--window") or "hann"
    if kind not in dsp.WINDOWS:
        sys.stderr.write("--window must be one of %s\n" % ", ".join(dsp.WINDOWS))
        return 1

    rate = 44100
    if source:
        if not os.path.exists(source):
            sys.stderr.write("no such file: %s\n" % source)
            return 1
        try:
            values, rate = dsp.read_wave(source, seconds * 96000)
        except Exception as err:
            sys.stderr.write("could not read %s as a wav: %s\n" % (source, err))
            return 1
        name = os.path.basename(source)
    else:
        values = dsp.synth(seconds * rate // 4, rate)
        name = "generated signal"

    if len(values) < fft_size:
        sys.stderr.write("only %d samples, need at least --fft %d\n" % (len(values), fft_size))
        return 1

    pad_to = dsp.next_power(fft_size * pad)
    win = dsp.window(kind, fft_size)

    starts = list(range(0, len(values) - fft_size + 1, hop))
    if len(starts) > frames_max:
        # Spread the kept frames across the whole signal and never take the first of them, so
        # the picture is of the recording and not of its opening second.
        step = len(starts) / float(frames_max)
        starts = [starts[int(i * step)] for i in range(frames_max)]

    # Only the bins under --top are kept. The rest is transform we paid for and do not draw, which
    # is unavoidable: the fast transform has no way to compute part of a spectrum.
    bins_all = pad_to // 2
    hz_per_bin = rate / float(pad_to)
    bins = min(bins_all, int(top_hz / hz_per_bin) + 1)

    began = time.time()
    columns = []
    for at, start in enumerate(starts):
        columns.append(dsp.spectrum(values[start:start + fft_size], win, pad_to)[:bins])
        if at and at % 32 == 0:
            sys.stderr.write("  %d of %d frames\r" % (at, len(starts)))
            sys.stderr.flush()
    sys.stderr.write(" " * 40 + "\r")

    loudest = 0.0
    for column in columns:
        for one in column:
            if one > loudest:
                loudest = one
    if loudest <= 0.0:
        loudest = 1.0

    # Rows are frequency bins, columns are frames: one series per bin, measured over time.
    level, linear, flux, slope = [], [], [], []
    floor_db = -90.0
    for b in range(bins):
        row_db, row_lin, row_flux, row_slope = [], [], [], []
        for t in range(len(columns)):
            one = columns[t][b]
            row_lin.append(round(one / loudest, 5))
            db = floor_db if one <= 0 else max(floor_db, 20.0 * math.log10(one / loudest))
            row_db.append(round(db, 3))
            was = columns[t - 1][b] if t else one
            row_flux.append(round((one - was) / loudest, 5))
            near = columns[t][b - 1] if b else one
            row_slope.append(round((one - near) / loudest, 5))
        level.append(row_db)
        linear.append(row_lin)
        flux.append(row_flux)
        slope.append(row_slope)

    took = time.time() - began
    payload = {
        "depth": len(columns),
        "depthLabel": "frame (%d)" % len(columns),
        "valueLabel": "level",
        "eyebrow": "Sound - rendered as a solid",
        "title": name,
        "blurb": ("%s at %d Hz. %d frames of %d samples, transformed at %d after zero-padding %dx, "
                  "%s window, hop %d. Depth runs left to right as time; the other horizontal axis "
                  "is frequency, %d bins up to %d Hz at %.2f Hz each."
                  % (name, rate, len(columns), fft_size, pad_to, pad, kind, hop,
                     bins, int(bins * hz_per_bin), hz_per_bin)),
        "noteTitle": "Padding places a peak, it does not separate two",
        "note": ("Resolution is set by how many samples a frame holds and nothing else: two tones "
                 "closer together than the frame can separate stay unseparated at any pad. Padding "
                 "interpolates onto a finer grid, so a peak falling between bins is drawn at its "
                 "real height and place instead of smeared across two. Raise fft to separate, "
                 "raise pad to place. A feature that appears at one window and not another belongs "
                 "to the window."),
        "settings": settings.collect(sys.argv[1:]),
        "fields": [
            {"key": "level", "label": "Level dB", "axis": "frequency bin", "rows": level},
            {"key": "linear", "label": "Linear", "axis": "frequency bin", "rows": linear},
            {"key": "flux", "label": "Flux", "axis": "frequency bin", "rows": flux},
            {"key": "slope", "label": "Bin slope", "axis": "frequency bin", "rows": slope},
        ],
    }

    with io.open(TEMPLATE, encoding="utf-8") as handle:
        page = handle.read()
    if "</script>" not in page:
        raise SystemExit("template is truncated: the script tag is never closed")
    page = page.replace("/*VOXEL_DATA*/null", json.dumps(payload, separators=(",", ":")))

    target = text("--out") or os.path.join(HERE, "sound_view.html")
    with io.open(target, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(page)

    print("wrote %s (%.1f KB)" % (target, os.path.getsize(target) / 1024.0))
    print("  %d frames of %d, transform %d (%dx pad), %s window"
          % (len(columns), fft_size, pad_to, pad, kind))
    print("  %d bins to %d Hz, %.2f Hz per bin" % (bins, int(bins * hz_per_bin), hz_per_bin))
    print("  %.1f s of transforms" % took)
    return 0


if __name__ == "__main__":
    sys.exit(main())
