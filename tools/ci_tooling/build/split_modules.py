#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Give every .c/.h pair its own directory.

A module is a .c and the .h beside it. Where several sit in one directory there is nothing in the
tree that says which header belongs to which source, and a per-module CMakeLists.txt has nowhere to
live. This moves each pair down into a directory of its own:

    src/mmgr/protomem.c      ->  src/mmgr/protomem/protomem.c
    src/mmgr/protomem.h      ->  src/mmgr/protomem/protomem.h

Only directories holding more than one pair are touched; a module that already has its directory to
itself is left where it is.

Every reference moves with it. A header is named three ways in this tree - repo-relative
("mmgr/protomem.h"), bare from a sibling ("protomem.h"), and dotted from a child ("../protomem.h") -
and all three are rewritten to the repo-relative form of the new location, which is the one spelling
that survives the file moving again.

Dry run by default; --go performs the moves.

    python tools/harness.py build split
    python tools/harness.py build split --go
"""

import argparse
import json
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
SRC = os.path.join(ROOT, "src")

# Where a source filter or an include may name a moved file.
SCAN_EXT = (".c", ".h", ".cpp", ".ino", ".json", ".ini", ".py", ".md", ".yml", ".yaml", ".txt")
SKIP_DIRS = {".git", ".pio", ".pio_base", ".pio_cov", "build", "__pycache__", ".cache", ".cov_base"}


def rel(p, base=ROOT):
    return os.path.relpath(p, base).replace("\\", "/")


def plan():
    """(old .c, old .h, new dir) for every pair that has to move."""
    pairs = {}
    for dirpath, _dn, files in os.walk(SRC):
        stems = [f[:-2] for f in sorted(files) if f.endswith(".c") and f[:-2] + ".h" in files]
        if len(stems) < 2:
            continue  # already alone in its directory
        for s in stems:
            pairs[os.path.join(dirpath, s)] = os.path.join(dirpath, s, s)
    return pairs


def rewrites(pairs):
    """Every include spelling that has to change, old -> new, longest first."""
    subs = {}
    for old_stem, new_stem in pairs.items():
        for ext in (".h", ".c"):
            old_rel = rel(old_stem + ext, SRC)
            new_rel = rel(new_stem + ext, SRC)
            base = os.path.basename(old_stem) + ext
            subs[old_rel] = new_rel  # "mmgr/protomem.h"
            subs[base] = new_rel  # "protomem.h" from a sibling
            subs["../" + base] = new_rel  # "../protomem.h" from a child
            subs["../" + old_rel] = new_rel
    return subs


INC = re.compile(r'(#\s*include\s+")([^"]+)(")')
FILT = re.compile(r"([+-]<)([^>]+)(>)")


def apply_text(text, subs, in_filter):
    """Rewrite include directives and build_src_filter entries, and nothing else."""
    changed = [0]

    def inc(m):
        target = subs.get(m.group(2))
        if target and target != m.group(2):
            changed[0] += 1
            return m.group(1) + target + m.group(3)
        return m.group(0)

    def filt(m):
        body = m.group(2)
        stem = body[3:] if body.startswith("../") else body
        target = subs.get(stem)
        if target and target != stem:
            changed[0] += 1
            return m.group(1) + target + m.group(3)
        return m.group(0)

    text = INC.sub(inc, text)
    if in_filter:
        text = FILT.sub(filt, text)
    return text, changed[0]


def main():
    ap = argparse.ArgumentParser(
        prog="split_modules", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--go", action="store_true", help="perform the moves (default is a dry run)")
    ap.add_argument("--list", action="store_true", help="list the pairs that would move and stop")
    a = ap.parse_args()

    pairs = plan()
    if not pairs:
        print("every .c/.h pair already has its own directory")
        return 0

    if a.list:
        for old in sorted(pairs):
            print("  %-58s -> %s/" % (rel(old + ".c"), rel(os.path.dirname(pairs[old]))))
        print("\n%d pair(s) in %d directory(ies)" % (len(pairs), len({os.path.dirname(p) for p in pairs})))
        return 0

    subs = rewrites(pairs)

    touched = edits = 0
    for dirpath, dnames, files in os.walk(ROOT):
        dnames[:] = [d for d in dnames if d not in SKIP_DIRS]
        for f in files:
            if not f.endswith(SCAN_EXT):
                continue
            p = os.path.join(dirpath, f)
            try:
                text = open(p, encoding="utf-8").read()
            except (OSError, UnicodeDecodeError):
                continue
            new, n = apply_text(text, subs, f.endswith((".json", ".ini")))
            if n:
                edits += n
                touched += 1
                if a.go:
                    open(p, "w", encoding="utf-8", newline="\n").write(new)

    print("%s: %d pair(s) move, %d reference(s) in %d file(s)" % ("moving" if a.go else "DRY RUN", len(pairs), edits, touched))

    if not a.go:
        print("\nnothing written - pass --go")
        return 0

    for old_stem, new_stem in sorted(pairs.items()):
        os.makedirs(os.path.dirname(new_stem), exist_ok=True)
        for ext in (".c", ".h"):
            if os.path.isfile(old_stem + ext):
                shutil.move(old_stem + ext, new_stem + ext)
    print("moved %d pair(s)" % len(pairs))
    print("now run: python tools/harness.py build modules && python tools/harness.py build cmake")
    return 0


if __name__ == "__main__":
    sys.exit(main())
