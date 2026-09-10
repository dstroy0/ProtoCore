# repotools-stamp: media_tools/dataviz/build_blob_view.py fddd7815c676b410
# repo_tools - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
"""Turns a raw binary file into the shape viewer, with no idea what the file is.

A blob has no columns and no header. Only one structure is available, the one every binary
has:
it is a sequence of bytes, and any sequence can be folded into a grid by choosing a width. Choose
the right width and repeating structure lines up into columns you can see; choose the wrong one and
it shears. That shearing is itself the reading, and width is therefore the first thing this exposes.

    python tools/view/build_blob_view.py firmware.bin
    python tools/view/build_blob_view.py firmware.bin --width 16 --offset 4096 --rows 4096

  --width     bytes per row, taken as the depth axis. Default 64.
  --offset    first byte to read. Default 0.
  --rows      how many rows to keep. Default 1024. Use 0 for the whole file.
  --window    span of the entropy and repeat windows, in bytes. Default 32.
  --title     heading for the page. Default: the file's name.
  --out       where to write. Default: <name>_view.html beside the blob.

Eight readings are built from the same bytes, because a blob rarely says anything under one of them:

  byte      the value itself, 0 to 255. Text, code, tables and padding each look distinct.
  ones      how many bits are set, 0 to 8. Discards value and leaves density.
  delta     each byte minus the one before. Zero is a run; a repeating delta is a fixed stride.
  entropy   Shannon entropy of the window around each position, in bits, 0 to 8. Flat and high is
            compressed or encrypted; low is structure.
  distinct  how many different byte values the window holds. Agrees with entropy on random data
            and disagrees on skewed data, which is where the interesting cases live.
  run       length of the run of identical bytes this position sits in. Padding and alignment.
  high      the top nibble alone, which is where ASCII ranges and opcode classes separate.
  low       the bottom nibble alone, which is where alignment and table strides show.

Writes a self-contained page: no server, no fetch at run time, nothing to install.
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

import settings

HERE = os.path.dirname(os.path.abspath(__file__))
TEMPLATE = os.path.join(HERE, "voxel_view_template.html")


def window_readings(data, window):
    """Entropy in bits and distinct-value count, for the window ending at each position.

    Counts are carried from step to step and never recounted, and the number of live values is
    tracked as counts cross zero, so both readings cost a constant amount per byte plus one pass
    over the values actually present. Recounting the window at every position instead would make a
    few megabytes take minutes for no better answer.
    """
    span = len(data)
    bits = [0.0] * span
    kinds = [0.0] * span
    counts = [0] * 256
    present = 0
    live = 0

    for at in range(span):
        one = data[at]
        if counts[one] == 0:
            present += 1
        counts[one] += 1
        live += 1

        if live > window:
            gone = data[at - window]
            counts[gone] -= 1
            if counts[gone] == 0:
                present -= 1
            live -= 1

        total = float(live)
        entropy = 0.0
        start = at - live + 1
        # At most `window` values are present, so this walks the window once and not all 256.
        for each in set(data[start:at + 1]):
            share = counts[each] / total
            entropy -= share * math.log(share, 2)

        bits[at] = entropy
        kinds[at] = float(present)

    return bits, kinds


def run_lengths(data):
    """For each position, the length of the run of identical bytes containing it."""
    span = len(data)
    out = [1.0] * span
    at = 0
    while at < span:
        end = at + 1
        while end < span and data[end] == data[at]:
            end += 1
        length = float(end - at)
        for each in range(at, end):
            out[each] = length
        at = end
    return out


def main():
    argv = sys.argv[1:]
    if not argv or argv[0].startswith("-"):
        sys.stderr.write(__doc__)
        return 2

    source = argv[0]
    if not os.path.exists(source):
        sys.stderr.write("no such file: %s\n" % source)
        return 1

    def number(name, fallback):
        return int(argv[argv.index(name) + 1]) if name in argv else fallback

    def text(name):
        return argv[argv.index(name) + 1] if name in argv else None

    width = number("--width", 64)
    offset = number("--offset", 0)
    rows_wanted = number("--rows", 1024)
    window = number("--window", 32)
    if width < 1:
        sys.stderr.write("--width must be at least 1\n")
        return 1
    if window < 2:
        sys.stderr.write("--window must be at least 2\n")
        return 1

    size = os.path.getsize(source)
    with io.open(source, "rb") as handle:
        handle.seek(offset)
        data = handle.read(-1 if rows_wanted == 0 else width * rows_wanted)
    if not data:
        sys.stderr.write("nothing to read at offset %d of a %d byte file\n" % (offset, size))
        return 1

    # A trailing partial row is padded and never dropped, so the last bytes of a file stay
    # visible. The padding is zero, which reads as a run and is not mistaken for data.
    rows = (len(data) + width - 1) // width
    padded = data + b"\x00" * (rows * width - len(data))

    bits, kinds = window_readings(padded, window)
    runs = run_lengths(padded)

    byte_rows, ones_rows, delta_rows = [], [], []
    ent_rows, kind_rows, run_rows = [], [], []
    high_rows, low_rows = [], []

    for r in range(rows):
        base = r * width
        chunk = padded[base:base + width]
        byte_rows.append([float(one) for one in chunk])
        ones_rows.append([float(bin(one).count("1")) for one in chunk])
        delta_rows.append([float(chunk[c] - padded[base + c - 1]) if base + c > 0 else 0.0
                           for c in range(width)])
        ent_rows.append([round(one, 3) for one in bits[base:base + width]])
        kind_rows.append(list(kinds[base:base + width]))
        run_rows.append(list(runs[base:base + width]))
        high_rows.append([float(one >> 4) for one in chunk])
        low_rows.append([float(one & 15) for one in chunk])

    title = text("--title") or os.path.basename(source)
    payload = {
        "depth": width,
        "depthLabel": "byte in row (%d)" % width,
        "valueLabel": "value",
        "eyebrow": "Raw binary - rendered as a solid",
        "title": title,
        "blurb": ("%s read as a grid %d bytes wide, %d rows from offset %d of %d bytes. Depth runs "
                  "left to right as position within the row; the other horizontal axis is the row, "
                  "which is the file in order. Repeating structure whose period divides the width "
                  "stands up as columns; anything else shears diagonally, and the shear angle is "
                  "the real period."
                  % (os.path.basename(source), width, rows, offset, size)),
        "noteTitle": "Width is the only assumption",
        "note": ("Nothing in a blob says how wide it is, so structure that appears at one width and "
                 "vanishes at another belongs to the choice of width rather than to the file. "
                 "Change it and keep what survives."),
        "settings": settings.collect(sys.argv[1:]),
        "fields": [
            {"key": "byte", "label": "Byte", "axis": "row", "rows": byte_rows},
            {"key": "ones", "label": "Bits set", "axis": "row", "rows": ones_rows},
            {"key": "delta", "label": "Delta", "axis": "row", "rows": delta_rows},
            {"key": "entropy", "label": "Entropy", "axis": "row", "rows": ent_rows},
            {"key": "distinct", "label": "Distinct", "axis": "row", "rows": kind_rows},
            {"key": "run", "label": "Run length", "axis": "row", "rows": run_rows},
            {"key": "high", "label": "High nibble", "axis": "row", "rows": high_rows},
            {"key": "low", "label": "Low nibble", "axis": "row", "rows": low_rows},
        ],
    }

    with io.open(TEMPLATE, encoding="utf-8") as handle:
        page = handle.read()
    if "</script>" not in page:
        raise SystemExit("template is truncated: the script tag is never closed")
    page = page.replace("/*VOXEL_DATA*/null", json.dumps(payload, separators=(",", ":")))

    target = text("--out")
    if target is None:
        target = os.path.join(os.path.dirname(os.path.abspath(source)),
                              os.path.splitext(os.path.basename(source))[0] + "_view.html")
    with io.open(target, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(page)

    written = os.path.getsize(target)
    print("wrote %s (%.1f KB)" % (target, written / 1024.0))
    print("  %d bytes of %d, from offset %d" % (len(data), size, offset))
    print("  %d rows of %d, %d cells in each of 8 fields" % (rows, width, rows * width))

    # The page draws one box per cell above the floor, so the cell count decides whether it
    # opens or hangs. Said as a warning and not a limit: a large blob is a legitimate thing to
    # look at, and the floor slider is there to make one tractable once it is loaded.
    if rows * width > 200000:
        print("  note: %d cells per field is a heavy page. Raise the Floor slider once it opens,"
              % (rows * width))
        print("        or narrow it with --rows / --offset to walk the file in pieces.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
