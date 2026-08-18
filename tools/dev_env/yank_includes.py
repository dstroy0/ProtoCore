#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Move a header's #include lines down into the files that include it.

A header that includes another header hands every consumer a dependency it never asked for and hides
what a translation unit actually needs. This takes the includes out of the header and puts them in
each .c that reaches it, immediately before that file's own include of the header, so what a TU
compiles against is stated in the TU.

Dry run by default; --go rewrites in place.

    python tools/harness.py edit yank src/network_drivers/tls/record/record.h
    python tools/harness.py edit yank src/network_drivers/tls --go

What it refuses, and why each refusal is right:

  * an include inside `#if` / `#ifdef` - it is conditional on something the consumer may not have
    settled, so moving it changes when it is read. Pass --conditional to take those too.
  * an include the consumer already has - nothing to add; the header's copy is still removed.
  * a header no .c reaches - removing its includes would break it with nothing to catch it, so it is
    reported and left alone unless --orphan is given.
  * the entry-point headers (protocore_config.h and what it assembles) - every file's first include
    by design. Named in KEEP below.
"""

import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
MANIFEST = os.path.join(ROOT, "test", "yanked_includes.json")

# The assembly chain: these exist to be included by everything, so their includes are the point.
KEEP = {
    "src/protocore_config.h",
    "src/config/platform/platform.h",
    "src/config/platform/types.h",
    "src/derived_sizing.h",
    "vendor/vendor_detect.h",
    "vendor/vendor_hal.h",
}

# Never yanked, whoever includes it. A header gates its own body on PROTOCORE_* - `#if
# PROTOCORE_TLS_SOFTWARE` around everything it declares - and those macros come from the entry point.
# Take it out and the header's body vanishes before any consumer's forced include can matter, because
# the consumer's include is read after the header's own guard has already decided.
NEVER_YANK = {
    "protocore_config.h",
    "config/platform/platform.h",
    "config/platform/types.h",
}

INC = re.compile(r'^\s*#\s*include\s+(?P<what>"[^"]+"|<[^>]+>)(?P<rest>.*)$')
COND_OPEN = re.compile(r"^\s*#\s*(if|ifdef|ifndef)\b")
COND_CLOSE = re.compile(r"^\s*#\s*endif\b")
GUARD = re.compile(r"^\s*#\s*ifndef\s+(\w+)\s*$")


def rel(p):
    return os.path.relpath(p, ROOT).replace("\\", "/")


def sources(exts=(".c", ".h")):
    for base in ("src", "test", "vendor", "examples", "include"):
        d = os.path.join(ROOT, base)
        if not os.path.isdir(d):
            continue
        for dirpath, _dn, files in os.walk(d):
            for f in files:
                if f.endswith(exts):
                    yield os.path.join(dirpath, f)


def read(p):
    with open(p, encoding="utf-8", errors="replace") as f:
        return f.read().split("\n")


def write(p, lines):
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


def header_includes(lines, take_conditional):
    """(index, text) of every include to yank, and the include-guard depth map.

    The file's own include guard is depth 1 for the whole body, so it is discounted: an include at
    guard depth is top level, not conditional.
    """
    out = []
    depth = 0
    guard_depth = None
    for i, line in enumerate(lines):
        if guard_depth is None:
            m = GUARD.match(line)
            if m and i + 1 < len(lines) and re.match(r"^\s*#\s*define\s+%s\s*$" % re.escape(m.group(1)), lines[i + 1]):
                depth += 1
                guard_depth = depth
                continue
        if COND_OPEN.match(line):
            depth += 1
            continue
        if COND_CLOSE.match(line):
            depth -= 1
            continue
        m = INC.match(line)
        if not m:
            continue
        conditional = depth > (guard_depth or 0)
        if conditional and not take_conditional:
            continue
        what = m.group("what")
        if what.startswith('"') and what[1:-1] in NEVER_YANK:
            continue
        out.append((i, line.rstrip(), what))
    return out


_INDEX = None


def build_index():
    """basename -> [(file, line, what it named)], read once.

    One pass over the tree rather than one per header: the walk is the cost, and a sweep over src/
    asks the same question a few hundred times.
    """
    global _INDEX
    if _INDEX is not None:
        return _INDEX
    idx = {}
    for p in sources():
        for i, line in enumerate(read(p)):
            m = INC.match(line)
            if not m:
                continue
            what = m.group("what")
            if not what.startswith('"'):
                continue
            named = what[1:-1]
            idx.setdefault(named.split("/")[-1], []).append((p, i, named))
    _INDEX = idx
    return idx


def consumers(header_rel):
    """Every file that includes @p header_rel, by the tail of the path it names."""
    name = header_rel.split("/")[-1]
    hits = []
    for p, i, named in build_index().get(name, []):
        if rel(p) == header_rel:
            continue
        # the path a consumer names has to end with the header's own path tail
        if header_rel.endswith(named) or named.endswith(name):
            hits.append((p, i, named))
    return hits


def plan_one(header, take_conditional, allow_orphan):
    hrel = rel(header)
    if hrel in KEEP:
        return None, "entry point, left alone"
    lines = read(header)
    yanks = header_includes(lines, take_conditional)
    if not yanks:
        return None, "no includes to yank"
    cons = consumers(hrel)
    if not cons and not allow_orphan:
        return None, "no file includes it (pass --orphan to yank anyway)"
    return {"header": header, "lines": lines, "yanks": yanks, "consumers": cons}, None


def apply(plan, go, manifest):
    """Take the includes out of the header and record what each consumer now owes.

    The dependency does not vanish with the line: a TU that reached the header for those types still
    needs them. It is recorded per consumer in the manifest, which gen_cmake.py turns into a forced
    include on exactly that source file, so the build states the dependency instead of the header.
    """
    header, lines, yanks, cons = plan["header"], plan["lines"], plan["yanks"], plan["consumers"]
    hrel = rel(header)
    texts = [t for _i, t, _w in yanks]
    owed = 0

    for cpath, _cline, _named in cons:
        crel = rel(cpath)
        have = set()
        for l in read(cpath):
            m = INC.match(l)
            if m:
                have.add(m.group("what"))
        new = [w for _i, _t, w in yanks if w not in have]
        if not new:
            continue
        entry = manifest.setdefault(crel, [])
        for w in new:
            if w not in entry:
                entry.append(w)
                owed += 1
        print("    -> %-56s %s" % (crel, ", ".join(new)))

    keep = [l for i, l in enumerate(lines) if i not in {i for i, _t, _w in yanks}]
    if go:
        write(header, keep)
    print("  %-60s -%d include(s), %d owed by %d consumer(s)" % (hrel, len(texts), owed, len(cons)))
    return len(texts), owed


def main():
    ap = argparse.ArgumentParser(
        prog="yank_includes", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("paths", nargs="+", help="header files, or directories to walk for .h")
    ap.add_argument("--go", action="store_true", help="rewrite in place (default is a dry run)")
    ap.add_argument("--conditional", action="store_true", help="also take includes inside #if / #ifdef")
    ap.add_argument("--orphan", action="store_true", help="yank even when no file includes the header")
    a = ap.parse_args()

    targets = []
    for p in a.paths:
        full = p if os.path.isabs(p) else os.path.join(ROOT, p)
        if os.path.isdir(full):
            for dirpath, _dn, files in os.walk(full):
                targets += [os.path.join(dirpath, f) for f in sorted(files) if f.endswith(".h")]
        elif os.path.isfile(full):
            targets.append(full)
        else:
            print("no such path: %s" % p, file=sys.stderr)
            return 1

    manifest = {}
    if os.path.isfile(MANIFEST):
        with open(MANIFEST, encoding="utf-8") as f:
            manifest = json.load(f).get("owed", {})

    print("%s %d header(s)" % ("rewriting" if a.go else "dry run over", len(targets)))
    moved = owed = touched = 0
    for h in sorted(targets):
        plan, why = plan_one(h, a.conditional, a.orphan)
        if plan is None:
            if why != "no includes to yank":
                print("  %-60s skipped: %s" % (rel(h), why))
            continue
        m, o = apply(plan, a.go, manifest)
        moved += m
        owed += o
        touched += 1

    if a.go and manifest:
        with open(MANIFEST, "w", encoding="utf-8", newline="\n") as f:
            json.dump(
                {
                    "_note": (
                        "Written by tools/dev_env/yank_includes.py. Each entry is a source file and the "
                        "includes its header no longer carries; tools/ci_tooling/build/gen_cmake.py turns "
                        "them into a forced include on that one source file. Regenerate the CMake after "
                        "changing this: python tools/harness.py build cmake"
                    ),
                    "owed": dict(sorted(manifest.items())),
                },
                f,
                indent=2,
            )
            f.write("\n")
        print("\nwrote %s" % rel(MANIFEST))

    print(
        "\n%d header(s), %d include(s) yanked, %d owed%s"
        % (touched, moved, owed, "" if a.go else " - DRY RUN, nothing written")
    )
    if a.go:
        print("run `python tools/harness.py build cmake` so the build carries what the headers no longer do")
    return 0


if __name__ == "__main__":
    sys.exit(main())
