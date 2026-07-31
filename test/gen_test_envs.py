#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
gen_test_envs.py - generate the native [env:*] blocks in platformio.ini from the
single source of truth, test/test_matrix.json.

Why: the native test envs used to be ~63 hand-written, near-duplicate blocks (each
re-listing the same -std/-I flags, its src filter, and its test filter), and the
runner only invoked a handful - so most per-feature suites silently fell out of the
report. Now there is ONE table; this script regenerates the env blocks from it, and
run_tests.sh auto-discovers every generated env. Edit the table, not the ini.

Each entry keeps strict per-feature isolation (its own flags + src + test dirs), so
"this feature compiles and tests on its own" stays guaranteed.

Usage:
    python3 test/gen_test_envs.py            # rewrite platformio.ini in place
    python3 test/gen_test_envs.py --check    # exit 1 if the ini is out of date (CI)

The table schema (test/test_matrix.json), per env:
    "native_x": {
        "desc":  "free text -> emitted as ; comments above the env",
        "base":  "native_base" (default) | "env:native_app",
        "flags": ["-DPC_ENABLE_X=1", ...],     # extras beyond the base flags
        "src":   ["+<services/x/x.cpp>", "-<*>"], # build_src_filter lines, verbatim
        "tests": ["test_x", ...],                 # test_filter entries
        "test_build_src": "no"                    # optional override
    }
"""

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
INI = os.path.join(ROOT, "platformio.ini")
TABLE = os.path.join(HERE, "test_matrix.json")

BEGIN = "; >>> GENERATED TEST ENVS - do not edit below; edit test/test_matrix.json and run test/gen_test_envs.py >>>"
END = "; <<< END GENERATED TEST ENVS <<<"


def render_env(name, e):
    base = e.get("base", "native_base")
    lines = []
    desc = e.get("desc", "").strip()
    if desc:
        for dl in desc.split("\n"):
            lines.append(f"; {dl}".rstrip())
    lines.append(f"[env:{name}]")
    lines.append(f"extends = {base}")
    flags = e.get("flags", [])
    if flags:
        lines.append("build_flags =")
        lines.append(f"    ${{{base}.build_flags}}")
        for fl in flags:
            lines.append(f"    {fl}")
    src = e.get("src", [])
    if src:
        lines.append("build_src_filter =")
        for s in src:
            lines.append(f"    {s}")
    tests = e.get("tests", [])
    if tests:
        lines.append("test_filter =")
        for t in tests:
            lines.append(f"    {t}")
    if e.get("test_build_src"):
        lines.append(f"test_build_src = {e['test_build_src']}")
    return "\n".join(lines)


def render_block(table):
    envs = table["envs"]
    parts = [BEGIN, "; Single source of truth: test/test_matrix.json  (" + str(len(envs)) + " native envs)"]
    for name, e in envs.items():
        parts.append("")
        parts.append(render_env(name, e))
    parts.append("")
    parts.append(END)
    return "\n".join(parts) + "\n"


def split_head(text):
    """Return the static head (everything above the generated region)."""
    if BEGIN in text:
        return text.split(BEGIN, 1)[0].rstrip("\n") + "\n\n"
    # First run: head = everything up to the first native env (minus its comments).
    lines = text.split("\n")
    first = next((i for i, l in enumerate(lines) if l.startswith("[env:native")), len(lines))
    j = first
    while j > 0 and (lines[j - 1].strip() == "" or lines[j - 1].lstrip().startswith(";")):
        j -= 1
    return "\n".join(lines[:j]).rstrip("\n") + "\n\n"


def build(text, table):
    return split_head(text) + render_block(table)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="exit 1 if platformio.ini is out of date (no write)")
    args = ap.parse_args()

    with open(TABLE, encoding="utf-8") as f:
        table = json.load(f)
    with open(INI, encoding="utf-8") as f:
        cur = f.read()

    new = build(cur, table)

    if args.check:
        if cur != new:
            print("platformio.ini is out of date; run: python3 test/gen_test_envs.py", file=sys.stderr)
            return 1
        print("platformio.ini is up to date.")
        return 0

    with open(INI, "w", encoding="utf-8", newline="\n") as f:
        f.write(new)
    print(f"Wrote {len(table['envs'])} native envs to platformio.ini")
    return 0


if __name__ == "__main__":
    sys.exit(main())
