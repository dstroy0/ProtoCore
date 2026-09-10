# repotools-stamp: media_tools/dataviz/build_field_view.py 1c3a1fca0f733787
# repo_tools - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
"""Turns any parametric table into the shape viewer, without knowing what the table is about.

The other generators here each read one measurement this repository produces. This one reads a CSV
and works out its own axes, so the same seven embeddings - plane, tube, toroid, sphere, cone, helix,
balloon - can be pointed at anything with the shape

    one value, measured over one depth axis, for each of many series

which is most measured data. Long format, one row per cell:

    kind,a,b,round,value
    residue,0,0,1,-31744.0
    residue,0,0,2,-30195.7

Usage, in the simple case where the columns are already named that way:

    python tools/view/build_field_view.py data.csv

and in the general case, naming the columns yourself:

    python tools/view/build_field_view.py data.csv --value amplitude --depth time --field sensor

  --value   the column holding the number to draw. Default: "value", else the last numeric column.
  --depth   the column that runs left to right. Default: the first of round, step, t, time, frame,
            depth, index that is present.
  --field   a column whose distinct values become the selectable fields. Default: "kind" if there
            is one, otherwise every row is one field.
  --title   heading for the page. Default: the file's name.
  --out     where to write. Default: <csv name>_view.html beside the CSV.

Every column that is none of those becomes part of the series key. A table indexed by two
parameters therefore draws one series per pair, and sorting the series by that key puts them
in a stable order along the cut.

Writes a self-contained page: no server, no fetch at run time, nothing to install.
"""

import os, sys
_at = os.path.dirname(os.path.abspath(__file__))
while _at != os.path.dirname(_at) and not os.path.isdir(os.path.join(_at, "lib", "repotools")):
    _at = os.path.dirname(_at)
sys.path.insert(0, os.path.join(_at, "lib"))

import csv
import io
import json
import os
import sys

import settings

HERE = os.path.dirname(os.path.abspath(__file__))
TEMPLATE = os.path.join(HERE, "voxel_view_template.html")

DEPTH_NAMES = ["round", "step", "t", "time", "frame", "depth", "index"]


def numeric(text):
    try:
        return float(text)
    except (TypeError, ValueError):
        return None


def pick_columns(header, rows, want_value, want_depth, want_field):
    """Decides which column is which, and says why, because a wrong guess is silent otherwise."""
    if want_value is None:
        if "value" in header:
            want_value = "value"
        else:
            # The last column that parses as a number in every row we sampled.
            for name in reversed(header):
                if all(numeric(row.get(name)) is not None for row in rows[:64]):
                    want_value = name
                    break
    if want_value is None or want_value not in header:
        raise SystemExit("no value column: name one with --value, from %s" % ", ".join(header))

    if want_depth is None:
        for name in DEPTH_NAMES:
            if name in header:
                want_depth = name
                break
    if want_depth is None or want_depth not in header:
        raise SystemExit("no depth column: name one with --depth, from %s" % ", ".join(header))

    if want_field is None and "kind" in header:
        want_field = "kind"
    if want_field is not None and want_field not in header:
        raise SystemExit("no column named %s, from %s" % (want_field, ", ".join(header)))

    return want_value, want_depth, want_field


def main():
    argv = sys.argv[1:]
    if not argv or argv[0].startswith("-"):
        sys.stderr.write(__doc__)
        return 2

    source = argv[0]
    if not os.path.exists(source):
        sys.stderr.write("no such file: %s\n" % source)
        return 1

    def option(name):
        return argv[argv.index(name) + 1] if name in argv else None

    with io.open(source, encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        sys.stderr.write("%s has no rows\n" % source)
        return 1

    header = [name for name in rows[0].keys() if name is not None]
    value_at, depth_at, field_at = pick_columns(
        header, rows, option("--value"), option("--depth"), option("--field"))

    keys = [name for name in header if name not in (value_at, depth_at, field_at)]

    # Depth values are collected as they are and then sorted numerically where they all parse.
    # A column of 1..64 therefore does not come back as 1, 10, 11.
    seen_depth = sorted({row[depth_at] for row in rows},
                        key=lambda one: (numeric(one) is None, numeric(one), one))
    slot = {name: at for at, name in enumerate(seen_depth)}

    fields = {}
    for row in rows:
        which = row[field_at] if field_at else "all"
        series = tuple(row[name] for name in keys)
        got = numeric(row[value_at])
        fields.setdefault(which, {}).setdefault(series, [0.0] * len(seen_depth))
        fields[which][series][slot[row[depth_at]]] = got if got is not None else 0.0

    packed = []
    for which in sorted(fields):
        order = sorted(fields[which],
                       key=lambda one: [(numeric(part) is None, numeric(part), part)
                                        for part in one])
        packed.append({
            "key": which,
            "label": which,
            "axis": " x ".join(keys) if keys else "series",
            "rows": [[round(cell, 4) for cell in fields[which][series]] for series in order],
        })

    title = option("--title") or os.path.splitext(os.path.basename(source))[0]
    payload = {
        "depth": len(seen_depth),
        "depthLabel": "%s (%d)" % (depth_at, len(seen_depth)),
        "valueLabel": value_at,
        "eyebrow": "Parametric field - rendered as a solid",
        "title": title,
        "blurb": ("Every series in %s, drawn as a solid. Depth runs left to right as %s; the other "
                  "horizontal axis is the series, ordered by %s; height and color are %s. The "
                  "embedding is a choice, not a measurement: structure that appears under one shape "
                  "and not another belongs to the map rather than to the data."
                  % (os.path.basename(source), depth_at,
                     " then ".join(keys) if keys else "row order", value_at)),
        "note": "",
        "settings": settings.collect(sys.argv[1:]),
        "fields": packed,
    }

    with io.open(TEMPLATE, encoding="utf-8") as handle:
        page = handle.read()
    if "</script>" not in page:
        raise SystemExit("template is truncated: the script tag is never closed")
    page = page.replace("/*VOXEL_DATA*/null", json.dumps(payload, separators=(",", ":")))

    target = option("--out")
    if target is None:
        target = os.path.join(os.path.dirname(os.path.abspath(source)),
                              os.path.splitext(os.path.basename(source))[0] + "_view.html")
    with io.open(target, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(page)

    print("wrote %s (%.1f KB)" % (target, os.path.getsize(target) / 1024.0))
    print("  value  %s" % value_at)
    print("  depth  %s, %d steps" % (depth_at, len(seen_depth)))
    print("  fields %s" % ", ".join("%s (%d series)" % (one["key"], len(one["rows"]))
                                    for one in packed))
    if keys:
        print("  series keyed by %s" % ", ".join(keys))
    return 0


if __name__ == "__main__":
    sys.exit(main())
