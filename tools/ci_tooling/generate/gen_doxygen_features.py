#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Writes the feature flags of the documentation build, in docs/Doxyfile.

Doxygen evaluates each #if of a header with the macros it holds. The configuration headers default most
features to 0, so a module behind such a gate has no declaration that Doxygen can see. This tool writes a
PREDEFINED += list that sets each PROTOCORE_ENABLE_ flag to 1 with :=, so the site documents every module:

    PREDEFINED += PROTOCORE_ENABLE_AES128GCM:=1 \\
                  PROTOCORE_ENABLE_AMQP:=1 \\
                  ...

A flag is one that src/protocore_config.h or a header of src/config/ defaults under #ifndef. A _NEEDS_ flag
takes its value from the flags it names, so it stays out. A := flag keeps its value through the #define of its
default, and tools/ci_tooling/build/doxygen_config_filter.py opens the #ifndef guard of that default, so the site
documents the default too.

    python -m tools.ci_tooling.generate.gen_doxygen_features          # rewrite the region
    python -m tools.ci_tooling.generate.gen_doxygen_features --check  # fail when the region is stale
"""

import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
DOXYFILE = os.path.join(ROOT, "docs", "Doxyfile")
BEGIN = "# BEGIN GENERATED doxygen_features (tools/ci_tooling/generate/gen_doxygen_features.py)"
END = "# END GENERATED doxygen_features"
FLAG = re.compile(r"#\s*ifndef\s+(PROTOCORE_ENABLE_\w+)")


def config_headers():
    yield os.path.join(ROOT, "src", "protocore_config.h")
    for base, dirs, files in os.walk(os.path.join(ROOT, "src", "config")):
        dirs.sort()
        for name in sorted(files):
            if name.endswith(".h"):
                yield os.path.join(base, name)


def flags():
    found = set()
    for path in config_headers():
        with open(path, encoding="utf-8", errors="replace") as fh:
            found.update(FLAG.findall(fh.read()))
    return sorted(name for name in found if "_NEEDS_" not in name)


def render(names):
    lead = "PREDEFINED            += "
    pad = " " * len(lead)
    lines = [BEGIN]
    for n, name in enumerate(names):
        last = n == len(names) - 1
        lines.append("%s%s:=1%s" % (lead if n == 0 else pad, name, "" if last else " \\"))
    lines.append(END)
    return "\n".join(lines)


def main(argv):
    with open(DOXYFILE, encoding="utf-8", newline="") as fh:
        text = fh.read()
    if BEGIN not in text or END not in text:
        raise SystemExit("gen_doxygen_features: %s holds no doxygen_features region" % DOXYFILE)
    block = render(flags())
    new = re.sub(re.escape(BEGIN) + r".*?" + re.escape(END), lambda _m: block, text, flags=re.S)
    name = os.path.relpath(DOXYFILE, ROOT).replace(os.sep, "/")
    if "--check" in argv:
        if new != text:
            print("STALE: %s feature flags are out of date" % name, file=sys.stderr)
            return 1
        print("%s: feature flags up to date" % name)
        return 0
    if new != text:
        with open(DOXYFILE, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(new)
        print("wrote %s: feature flags" % name)
    else:
        print("%s: unchanged" % name)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
