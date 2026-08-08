#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generate a build_opt.h next to every example that needs feature flags.

Arduino IDE has no build_flags field, and a `#define` in the .ino does not reach the
separately-compiled library .cpp files. The one Arduino-native mechanism that does is a
file named `build_opt.h` in the sketch folder: the IDE feeds its contents to every
compilation unit, library sources included. So each example that a `pio ci` build enables
via `-D` flags gets a matching build_opt.h, letting it build unmodified in Arduino IDE.

Single source of truth: the first `build_flags=...` line documented in each example's
README.md (the same one tools/ci_tooling/generate/example_footprints.py reads). Regenerate whenever an
example's flags change; the CI check fails if a committed build_opt.h drifts.

    python -m tools.ci_tooling.generate.gen_build_opt          # write / prune build_opt.h files
    python -m tools.ci_tooling.generate.gen_build_opt check     # CI gate: fail if any is stale/missing
"""

import glob
import os
import re
import sys
from tools.ci_tooling.lib import doc_region as dr

ROOT = dr.repo_root(__file__)
EX = os.path.join(ROOT, "examples")
FLAGS_RE = re.compile(r'build_flags=([^"]*)"')
DEF_RE = re.compile(r"-D\S+")


def flags_for(example_dir):
    """Ordered list of -D flags from the example README's first build_flags line."""
    readme = os.path.join(example_dir, "README.md")
    if not os.path.exists(readme):
        return []
    m = FLAGS_RE.search(open(readme, encoding="utf-8", errors="replace").read())
    return DEF_RE.findall(m.group(1)) if m else []


def wanted():
    """Map example dir -> desired build_opt.h text (empty string = no file wanted)."""
    out = {}
    for ino in glob.glob(os.path.join(EX, "**", "*.ino"), recursive=True):
        d = os.path.dirname(ino)
        defs = flags_for(d)
        out[d] = ("\n".join(defs) + "\n") if defs else ""
    return out


def main():
    # `--check` is the one convention across tools/ci_tooling/ (see tools/ci_tooling/README.md).
    # This took a positional `check`, so the documented spelling silently exited 0.
    check = "--check" in sys.argv[1:]
    stale, wrote, pruned = [], 0, 0
    for d, text in sorted(wanted().items()):
        path = os.path.join(d, "build_opt.h")
        cur = open(path, encoding="utf-8").read() if os.path.exists(path) else ""
        # Compare newline-insensitively so a CRLF checkout never false-flags.
        if cur.replace("\r\n", "\n") == text:
            continue
        rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
        if check:
            stale.append(rel)
            continue
        if text:
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
            wrote += 1
        elif os.path.exists(path):
            os.remove(path)  # example no longer enables anything
            pruned += 1
    if check:
        if stale:
            print("build_opt.h out of date; run: python -m tools.ci_tooling.generate.gen_build_opt")
            for s in stale:
                print("  ", s)
            sys.exit(1)
        print("build_opt.h files up to date")
    else:
        print("build_opt.h: wrote %d, pruned %d" % (wrote, pruned))


if __name__ == "__main__":
    main()
