#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Guard over test/test_matrix.json: every source a filter names exists, and no env names one twice.

Two ways the matrix rots while the tree moves:

  1. an entry whose glob resolves to no file, left behind by a rename or a directory move
  2. an entry naming a .c the env's interpolated base filter already carries

The second is the one with teeth. An env that extends a stack base interpolates the base's whole
build_src_filter and then lists sources of its own, and the two overlap silently. A .c reaching one
link line twice is every symbol in it defined twice, which the linker refuses. test/harness.py drops
the repeat when it expands a filter, so the overlap is invisible from the test run and stays in the
matrix as a statement that does nothing until something expands a filter without that step.

Resolution is harness.py's own _resolve_src, so a filter cannot mean one thing here and another
thing to the build.

Rule 1 is absolute: the tree satisfies it today and a new dead entry is a rename that missed a
site. Rule 2 is ratcheted, because 63 known redundant entries predate the rule.

Run: python -m tools.ci_tooling.check.check_test_matrix [--baseline]
"""

import contextlib
import importlib.util
import io
import json
import os
import sys

from tools.ci_tooling.lib import baseline as bl
from tools.ci_tooling.lib import doc_region as dr

ROOT = dr.repo_root(__file__)
MATRIX = os.path.join(ROOT, "test", "test_matrix.json")
INI = os.path.join(ROOT, "platformio.ini")
BASELINE = bl.path_for(__file__, "test_matrix_baseline")

INTERP = "${env:"


def harness():
    """test/harness.py as a module, for the resolver the build itself uses."""
    spec = importlib.util.spec_from_file_location("protocore_harness", os.path.join(ROOT, "test", "harness.py"))
    mod = importlib.util.module_from_spec(spec)
    sys.modules["protocore_harness"] = mod
    spec.loader.exec_module(mod)
    return mod


def entry_glob(token):
    """The glob inside a `+<...>` token, or None for anything else."""
    if token.startswith("+<") and token.endswith(">"):
        return token[2:-1]
    return None


def resolve(h, glob, cache):
    """Repo-relative .c paths one filter glob names. Empty means it names none."""
    if glob not in cache:
        # _resolve_src reports a glob that matches nothing on stderr; the empty return is the signal
        with contextlib.redirect_stderr(io.StringIO()):
            cache[glob] = frozenset(h._resolve_src([glob]))
    return cache[glob]


def base_of(src):
    """The env a `${env:X.build_src_filter}` entry inherits from, or None."""
    for token in src:
        if token.startswith(INTERP) and "build_src_filter" in token:
            return token[len(INTERP) :].split(".", 1)[0]
    return None


def check():
    h = harness()
    ini_envs = h.parse_ini_envs(INI)
    with open(MATRIX, encoding="utf-8") as fh:
        matrix = json.load(fh)["envs"]
    cache = {}

    dead, redundant = [], []
    for name in sorted(matrix):
        src = matrix[name].get("src") or []

        for token in src:
            glob = entry_glob(token)
            if glob is not None and not resolve(h, glob, cache):
                dead.append((name, token))

        base = base_of(src)
        # A `-<...>` entry re-bases what the interpolation carries, so overlap is not decidable here
        if not base or any(t.startswith("-<") for t in src):
            continue
        carried = set()
        for glob in ini_envs.get(base, {}).get("src", []):
            carried |= resolve(h, glob, cache)
        for token in src:
            glob = entry_glob(token)
            if glob is None:
                continue
            files = resolve(h, glob, cache)
            if files and files <= carried:
                redundant.append((name, token, base))

    return dead, redundant


def key(v):
    return f"redundant|{v[0]}|{v[1]}"


def main(argv):
    dead, redundant = check()

    if "--baseline" in argv:
        n = bl.save(BASELINE, (key(v) for v in redundant))
        print(f"check_test_matrix: recorded {n} known redundant entry(s) as the floor")
        return 0

    new_redundant, known, fixed = bl.filter_new(redundant, key, BASELINE)

    if dead or new_redundant:
        print("check_test_matrix: test/test_matrix.json disagrees with the tree:", file=sys.stderr)
        for name, token in dead:
            print(f"  {name}: [dead entry] {token} names no file", file=sys.stderr)
        for name, token, base in new_redundant:
            print(f"  {name}: [redundant] {token} is already carried by {base}", file=sys.stderr)
        print(
            f"check_test_matrix: {len(dead) + len(new_redundant)} violation(s) - "
            "drop the entry, or name a file the base does not carry.",
            file=sys.stderr,
        )
        return 1

    msg = "check_test_matrix: OK - every filter entry names a file"
    if known:
        msg += f" ({known} known redundant entry(s) remain{f', {fixed} fixed' if fixed else ''})"
    print(msg)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
