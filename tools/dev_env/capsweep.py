#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Compile every capability-gated module with the capability OFF, and say what each one does.

WHY THIS EXISTS. A hardware capability has two arms and the host answers only one of them:
test/core_setup/hal/host states PROTOCORE_PLATFORM_HAS_BUS 1, so the #else half of every bus-owning
peripheral is text that nothing has ever compiled. Three of them had rotted all the way through -
each defined the pre-namespace flat API while its table went on binding entries that existed only
in the arm the host does build - and the whole matrix stayed green the entire time, because a green
matrix says nothing about code no target includes.

WHAT AN ANSWER MEANS. Two answers are correct and this tool does not rank them:

  refuses   an #error, because the module IS the hardware and has no software stand-in. Eleven I2C
            parts answer this way, and so does pmbus, which reaches its bus through smbus.
  compiles  a stand-in exists and builds. hmmd and ld2410 answer this way: each is a codec with a
            seam bolted to it, and the codec is exactly what a host can test.

Only the third answer is a defect:

  BROKEN    neither - the arm was meant to stand in and does not build.

A module that answers `compiles` should have an env building it that way, or the arm goes back to
being text nothing compiles. See native_hmmd_nobus and native_ld2410_nobus.

Usage:
    python tools/dev_env/capsweep.py                  every capability, every module that gates on it
    python tools/dev_env/capsweep.py PROTOCORE_HAS_BUS
    python tools/dev_env/capsweep.py --quiet          only the modules that answer BROKEN
"""

import io
import os
import re
import subprocess
import sys

R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
INCLUDES = [
    "test/core_setup/hal/host",
    "test/support",
    "src",
    "include",
    ".",
    "test",
]
# A capability is a hardware answer the vendor states, which is what makes its off-arm unbuildable
# by anything the host runs. Read from the file that demands them rather than listed here.
CAPS_FILE = "src/config/hardware_capabilities/hw_caps_en_error.h"


def capabilities():
    """Every PROTOCORE_HAS_* a vendor is required to state."""
    text = io.open(os.path.join(R, CAPS_FILE), encoding="utf-8", errors="replace").read()
    return sorted(set(re.findall(r"#ifndef\s+(PROTOCORE_HAS_\w+)", text)))


def env_flags():
    """src path -> the flags of an env that actually builds it, taken from the matrix.

    Not guessed from the filename. A module's own enable gate is rarely the only flag it needs:
    oauth2.c wants JSON, partition_monitor.c wants the HTTP route layer, and protocore.c wants most
    of the tree. Guessing one gate and compiling reported four modules BROKEN for missing types that
    have nothing to do with the capability under test - a tool that cries wolf about the thing it
    exists to find. test_matrix.json already states what each file is built with; that is the answer.
    """
    import json

    m = json.load(io.open(os.path.join(R, "test", "test_matrix.json"), encoding="utf-8"))
    envs = m.get("envs", m)
    best = {}
    for name, e in envs.items():
        if not isinstance(e, dict):
            continue
        flags = [f.lstrip("-D") for f in e.get("flags", []) if f.startswith("-D")]
        for entry in e.get("src", []):
            mm = re.match(r"\+<(.+)>$", entry)
            if not mm:
                continue
            rel = "src/" + mm.group(1)
            # The env with the MOST flags builds the file in its fullest configuration, which is the
            # one where a missing dependency is least likely to be mistaken for a broken arm.
            if rel not in best or len(flags) > len(best[rel][1]):
                best[rel] = (name, flags)
    return best


def gaters(cap, flags_of):
    """Every .c under src/ that branches on `cap`, with the flags the matrix builds it with."""
    out = []
    for root, _dirs, files in os.walk(os.path.join(R, "src")):
        for f in files:
            if not f.endswith(".c"):
                continue
            p = os.path.join(root, f)
            s = io.open(p, encoding="utf-8", errors="replace").read()
            if cap not in s:
                continue
            rel = os.path.relpath(p, R).replace("\\", "/")
            known = flags_of.get(rel)
            if known:
                out.append((rel, known[0], known[1]))
            else:
                # Built by no env at all: the file's own gate is the only thing to go on, and the
                # verdict is worth less. Said out loud rather than folded in silently.
                m = re.search(r"^#if\s+(PROTOCORE_ENABLE_\w+)", s, re.M)
                out.append((rel, None, ["%s=1" % m.group(1)] if m else []))
    return sorted(out)


def compile_with(rel, defines):
    cmd = ["gcc", "-std=gnu11", "-fno-exceptions", "-O1", "-fsyntax-only"]
    cmd += ["-I" + os.path.join(R, i).replace("\\", "/") for i in INCLUDES]
    cmd += ["-D%s" % d for d in defines]
    cmd += [os.path.join(R, rel)]
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=R)
    return r.returncode, r.stderr or r.stdout


def classify(rel, cap, flags):
    """refuses | compiles | BROKEN | n/a, and the first line of evidence for BROKEN.

    THE CONTROL COMES FIRST. Compiling with the capability ON says whether these flags build this
    file at all; without it, a missing dependency is indistinguishable from a rotted arm. oauth2.c
    reported `'JsonV' undeclared` with HAS_NET_STACK at 0 and reports the same at 1 - the file wants
    JSON either way, and the arm itself is one line. Calling that BROKEN is the tool crying wolf
    about the thing it exists to find.
    """
    base = ["_POSIX_C_SOURCE=200809L"] + [f for f in flags if not f.startswith(cap + "=")]
    on, on_log = compile_with(rel, base + ["%s=1" % cap])
    if on != 0 and not any("#error" in ln for ln in on_log.splitlines()):
        first = [ln for ln in on_log.splitlines() if "error:" in ln]
        return "n/a", "does not build with the capability ON either: %s" % (
            first[0].split("error:", 1)[-1].strip() if first else ""
        )

    # The capability goes on LAST so it wins over anything the env states about it.
    defines = base + ["%s=0" % cap]
    code, log = compile_with(rel, defines)
    if code == 0:
        return "compiles", ""
    # An #error ANYWHERE is the module refusing on purpose, and the verdict stops there. gcc does
    # not: it reports the #error and carries on through a file the directive just said should not be
    # compiled, so pmbus - which refuses correctly - also reported three undefined statics from the
    # arm its own refusal skipped. Reading those as the verdict called a correct module broken.
    errs = [ln for ln in log.splitlines() if "error:" in ln]
    if any("#error" in e for e in errs):
        return "refuses", ""
    first = errs[0].split("error:", 1)[-1].strip() if errs else ""
    return "BROKEN", first


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    quiet = "--quiet" in sys.argv
    caps = args or capabilities()
    flags_of = env_flags()

    broken = 0
    unbuilt = 0
    for cap in caps:
        mods = gaters(cap, flags_of)
        if not mods:
            continue
        if not quiet:
            print("\n%s = 0" % cap)
            print("-" * 78)
        for rel, env, flags in mods:
            verdict, why = classify(rel, cap, flags)
            if verdict == "BROKEN":
                broken += 1
            if env is None:
                unbuilt += 1
            if quiet and verdict != "BROKEN":
                continue
            print("  %-9s %-58s %s%s" % (verdict, rel, "[no env] " if env is None else "", why[:60]))

    print("\n%d module(s) answer BROKEN" % broken)
    if unbuilt:
        print("%d were compiled on their own gate alone, because no env builds them" % unbuilt)
    return 1 if broken else 0


if __name__ == "__main__":
    sys.exit(main())
