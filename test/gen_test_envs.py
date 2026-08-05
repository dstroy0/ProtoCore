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
        "base":  "native_base" (default) | "env:native_stack_l46" | "env:native_stack_http",
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


# The flags every native env extends. Generated rather than left in the static head, because the
# head is only preserved - never rewritten - so a value in it could not be corrected by running
# this script, which is the one way platformio.ini is allowed to change.
NATIVE_BASE = """; Shared flags for all native environments
[native_base]
platform = native
build_flags =
    ; src/ is C11 (docs/SRC_LAW.md section 0).
    -std=c11
    ; strnlen is POSIX 2008, not ISO C11, and -std=c11 asks glibc for ISO C alone - so <string.h>
    ; declares strlen and not strnlen. Ban #1 (docs/SRCBANNED.md) requires strnlen everywhere, so
    ; without this the whole tree compiles it as an implicit declaration returning int, and every
    ; `size_t n = strnlen(...)` truncates through 32 bits with nothing but a warning. Naming the
    ; POSIX level is what makes the bounded string functions visible; the language stays C11.
    -D_POSIX_C_SOURCE=200809L
    ; src/ contains no throw / try / catch (no-heap, no-stdlib, deterministic - and the ESP32
    ; target builds without exceptions anyway), so this only strips what the host toolchain would
    ; add on its own. It matters for coverage: with exceptions on, g++ emits an unwind edge at
    ; every call to a function that is not noexcept, and gcov counts each one as a BRANCH. Those
    ; edges are unreachable by construction in a codebase that never throws, so they are pure
    ; noise in the branch numbers and they make 100% branch coverage unattainable - wamp.cpp alone
    ; carried 114 of them (280 branches -> 166, and 57 of its 84 "uncovered" branches vanished).
    ; Keep this here rather than only in the coverage run so the tested build and the measured
    ; build are the same build.
    -fno-exceptions
    -I test/mocks
    -I test/support
    -I src
    -DPROTOCORE_HOST=1
    ; libm is a separate library to the C driver. g++ pulled it in behind libstdc++, so nothing here
    ; ever named it; gcc does not, and every env whose sources call sin / cos / sqrt / atan2 / fabs
    ; fails at the link with an undefined reference instead. It sits in the shared block because
    ; which envs reach a math call is a property of their sources, not of their configuration.
    -lm
; The filesystem the device actually runs, so a host test asserts against real directory
; semantics rather than a hand-rolled tree that agrees with itself. Only the envs whose sources
; include lfs.h build it - the dependency finder does not compile what nothing reaches for.
lib_deps =
    anurag3301/littlefs@^2.11.6
test_build_src = yes"""


def render_block(table):
    envs = table["envs"]
    parts = [
        BEGIN,
        "; Single source of truth: test/test_matrix.json  (" + str(len(envs)) + " native envs)",
        "",
        NATIVE_BASE,
    ]
    for name, e in envs.items():
        parts.append("")
        parts.append(render_env(name, e))
    parts.append("")
    parts.append(END)
    return "\n".join(parts) + "\n"


def strip_native_base(head):
    """Drop a [native_base] left in the head; render_block emits it now.

    Without this the block would appear twice on the first run after it moved, and the stale copy
    is the one PlatformIO would read.
    """
    lines = head.split("\n")
    out = []
    i = 0
    while i < len(lines):
        if lines[i].strip() == "[native_base]":
            while out and (out[-1].strip() == "" or out[-1].lstrip().startswith(";")):
                out.pop()  # its own comment header goes with it
            i += 1
            while i < len(lines) and not lines[i].startswith("["):
                i += 1
            continue
        out.append(lines[i])
        i += 1
    return "\n".join(out)


def split_head(text):
    """Return the static head (everything above the generated region)."""
    if BEGIN in text:
        return strip_native_base(text.split(BEGIN, 1)[0]).rstrip("\n") + "\n\n"
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
