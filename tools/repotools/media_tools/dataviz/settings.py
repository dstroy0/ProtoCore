# repotools-stamp: media_tools/dataviz/settings.py 772ce8526654b193
# repo_tools - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
"""Opening state for a viewer page, as parameters instead of edits to a template.

Every generator here takes --set key=value, repeatable, and passes the result through to the page.
It decides what the viewer looks like when it opens: which representation, where the observer is,
whether it turns on its own, what is already selected. A caller that wants a particular view asks
for it instead of publishing a page and telling the reader which controls to move.

    python tools/view/build_blob_view.py file.bin --set shape=hilbert --set spin=0.3
    python tools/view/build_field_view.py data.csv --set theme=light --set floor=20

Unknown keys are refused and never ignored. A typo in a setting is silent otherwise, and a page
that opens in the wrong state looks like a bug in the viewer.
"""

import sys

# name, kind, default, and what it does. The page applies these; nothing here draws anything.
KNOWN = {
    # what is shown
    "step": ("int", 0, "which step is selected, counting from zero"),
    "shape": ("str", "", "representation key, such as plane, sphere, hilbert, torus"),
    "transform": ("str", "", "transform key, such as none, invert, shadow, wall"),
    "overlay": ("str", "", "step key whose values drive color, or empty for off"),
    "order": ("int", 0, "sides, petals or winding, 3 to 4096"),
    "wrap": ("int", 0, "how many times the data is laid around the shape"),

    # how it is drawn
    "height": ("int", 0, "relief, 1 to 60"),
    "floor": ("int", 0, "hides cells quieter than this, 0 to 90"),
    "contrast": ("float", 0.0, "exponent the value is raised to before the ramp"),
    "theme": ("str", "", "light or dark, or empty to follow the reader's system"),
    "background": ("str", "", "ground color as #rrggbb"),
    "low": ("str", "", "ramp color for the low end"),
    "mid": ("str", "", "ramp color at zero"),
    "high": ("str", "", "ramp color for the high end"),
    "opacity": ("int", 0, "how present the control boxes are, 15 to 100"),

    # where the observer is, and whether anything moves
    "yaw": ("float", 0.0, "observer angle around the object, in radians"),
    "pitch": ("float", 0.0, "observer angle above the object, in radians"),
    "distance": ("float", 0.0, "observer distance, 60 to 1400"),
    "spin": ("float", 0.0, "turns per minute the object rotates on its own, 0 for still"),
    "spinaxis": ("str", "", "up or right, the axis the spin turns about"),

    # what is already selected
    "select_from": ("float", 0.0, "low end of a value range selected on opening"),
    "select_to": ("float", 0.0, "high end of that range"),
}


def usage():
    """The settings table, for a generator's own help text."""
    lines = ["  --set key=value, repeatable. Known keys:"]
    for name in sorted(KNOWN):
        kind, _, what = KNOWN[name]
        lines.append("    %-12s %-6s %s" % (name, kind, what))
    return "\n".join(lines)


def collect(argv):
    """Reads every --set from a command line and returns them as a dict the page understands.

    Values are converted to the kind the page expects. The page never has to guess whether "3" is
    a number or a word. A key that is not known, or a value that is not the right kind, exits with
    a message: a setting that does not apply is worse than one that was never given, because the
    page opens looking wrong and the fault is invisible.
    """
    out = {}
    at = 0
    while at < len(argv):
        if argv[at] != "--set":
            at += 1
            continue
        if at + 1 >= len(argv):
            sys.stderr.write("--set needs a key=value\n")
            raise SystemExit(1)
        pair = argv[at + 1]
        if "=" not in pair:
            sys.stderr.write("--set %s is not key=value\n" % pair)
            raise SystemExit(1)
        name, _, text = pair.partition("=")
        name = name.strip()
        if name not in KNOWN:
            sys.stderr.write("unknown setting %s. Known:\n%s\n" % (name, usage()))
            raise SystemExit(1)
        kind = KNOWN[name][0]
        try:
            if kind == "int":
                out[name] = int(text)
            elif kind == "float":
                out[name] = float(text)
            else:
                out[name] = text
        except ValueError:
            sys.stderr.write("setting %s wants %s, got %s\n" % (name, kind, text))
            raise SystemExit(1)
        at += 2
    return out
