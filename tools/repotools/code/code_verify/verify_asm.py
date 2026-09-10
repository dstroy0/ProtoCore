#!/usr/bin/env python3
# repotools-stamp: code/code_verify/verify_asm.py 0913ec01f75d22c0
# repo_tools - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
"""Compile a source for a target, disassemble the object, and read what the assembler actually emitted.

  verify_asm.py                  every case in the manifest
  verify_asm.py <manifest.toml>  a manifest named on the command line
  verify_asm.py --show <label>   the disassembly of one case, for working out what to require

WHAT THIS CATCHES THAT A BUILD DOES NOT

A build tells you the compiler accepted the source. It does not tell you the compiler did what the
source says. Four failures pass a clean build and are invisible until somebody profiles:

  - an intrinsics header that silently fell back to a scalar loop on a target it does not support
  - an intrinsic the compiler emulated in software instead of issuing as one instruction
  - an architecture flag accepted and then ignored, so the wide instructions were never selected
  - a hand written kernel the optimizer replaced with a call to libc

All four are correct. All four are slow. Tests pass, because a fallback computes the same answer.

This reads the object file back and requires the instructions the source was written to use to be
present, and the ones it was written to avoid to be absent.

WHAT IT DOES NOT CATCH, AND THE GRADE THAT SAYS SO

Nothing is executed. No value is computed and nothing is compared against a reference. A logic
error inside the vector loop passes this check untouched.

So a case carries a `grade`, and the three grades are never interchanged:

  agrees   run on real hardware and compared item by item against a reference implementation
  builds   compiled for a target, and real machine code confirmed generated for it
  emits    disassembled, and the instructions it was written to use confirmed present

This tool issues `emits` and nothing stronger. A target with no hardware anywhere can reach `emits`
and stops there, and the convention is that such a build carries its grade in its own name, so a
table of results cannot show an unrun arm beside a run one with the difference invisible.

THE ANECDOTE THAT IS THE REASON TO TRUST IT

The first SVE case written against this reported "did not emit whilelt". The code was right and the
expectation was wrong: `svwhilelt_b32` on unsigned operands emits WHILELO, because WHILELT is the
signed form. The check caught a wrong belief rather than wrong code, which is the case it is least
likely to be trusted on and the one worth writing down. A check that only ever confirms what you
already believed is not doing anything.

THE MANIFEST

  [[case]]
  label    = "sve-unrun"
  grade    = "emits"
  compiler = "aarch64-linux-gnu-gcc"
  flags    = ["-O2", "-march=armv8.2-a+sve"]
  source   = "src/engine/limbs.c"
  objdump  = "aarch64-linux-gnu-objdump"
  symbol   = "limb_compare"
  require  = ["whilelo", "ld1w"]
  forbid   = ["bl.*memcpy"]

`source` is relative to the repository root. `symbol` is optional and narrows the disassembly to one
function, which keeps a `forbid` from matching a call in unrelated code. `require` and `forbid` are
regular expressions matched case insensitively against the disassembly text.

A compiler that is not installed makes the case SKIPPED and not a pass. A skip is printed and does
not refuse, because a manifest naming four cross compilers is normal and nobody has all four; what
would be wrong is counting the absent ones as satisfied.
"""

import os
import sys

_at = os.path.dirname(os.path.abspath(__file__))
while _at != os.path.dirname(_at) and not os.path.isdir(os.path.join(_at, "lib", "repotools")):
    _at = os.path.dirname(_at)
sys.path.insert(0, os.path.join(_at, "lib"))

import re  # noqa: E402
import shutil  # noqa: E402
import subprocess  # noqa: E402
import tomllib  # noqa: E402

from repotools import config, findings, root  # noqa: E402

# Where a manifest is looked for when the caller names none, under the repository root.
DEFAULT_MANIFEST = "verify_asm.toml"

# The grades this tool is allowed to issue. `agrees` is not among them: nothing here runs anything.
ISSUABLE = ("emits", "builds")


def manifest_path(cfg, named):
    """The manifest to read, and where it was asked for, from the command line then the config.

    Returns (path, how) so a refusal can say which of the three it was looking for. A default that
    quietly resolves to nothing is the same shape as a prose root that had moved: the run reads zero
    cases and reports success. Every path this returns is checked for existence by the caller.
    """
    if named:
        return (named if os.path.isabs(named) else os.path.join(cfg.where, named)), "named"
    asked = cfg.data.get("verify", {}).get("manifest")
    if asked:
        return os.path.join(cfg.where, asked), "[verify] manifest"
    return os.path.join(cfg.where, DEFAULT_MANIFEST), "the default name"


def disassemble(case, where, work, report):
    """Compile one case and return its disassembly text, or None where it could not be produced.

    A failure to compile is a breaking finding: the manifest claims this source builds for this
    target, and it did not.
    """
    compiler = case["compiler"]
    if shutil.which(compiler) is None:
        return "SKIP"

    source = os.path.join(where, case["source"])
    if not os.path.isfile(source):
        report.breaking(case["source"], 0, "%s: no source at that path" % case["label"])
        return None

    stem = re.sub(r"[^A-Za-z0-9_]", "_", case["label"])
    obj = os.path.join(work, stem + ".o")
    built = subprocess.run([compiler] + list(case.get("flags", [])) + ["-c", source, "-o", obj],
                           capture_output=True, text=True)
    if built.returncode != 0:
        first = (built.stderr.strip().splitlines() or ["no message"])[0]
        report.breaking(case["source"], 0, "%s: did not compile: %s" % (case["label"], first[:120]))
        return None

    dumper = case.get("objdump", "objdump")
    if shutil.which(dumper) is None:
        return "SKIP"

    # --disassemble=SYMBOL narrows to one function where the toolchain supports it. An older
    # objdump rejects the form, and the whole object is read instead rather than the case failing
    # over a flag: a wider disassembly can only make `forbid` stricter, never looser.
    wanted = ["-d"]
    if case.get("symbol"):
        narrowed = subprocess.run([dumper, "--disassemble=" + case["symbol"], obj],
                                  capture_output=True, text=True)
        if narrowed.returncode == 0:
            return narrowed.stdout
    read = subprocess.run([dumper] + wanted + [obj], capture_output=True, text=True)
    if read.returncode != 0:
        report.breaking(case["source"], 0, "%s: %s could not read the object" % (case["label"], dumper))
        return None
    return read.stdout


def main():
    argv = [one for one in sys.argv[1:] if not one.startswith("-")]
    showing = None
    if "--show" in sys.argv:
        spot = sys.argv.index("--show")
        showing = sys.argv[spot + 1] if (spot + 1) < len(sys.argv) else None
        argv = [one for one in argv if one != showing]

    cfg = config.load()
    where = cfg.where
    path, how = manifest_path(cfg, argv[0] if argv else None)

    report = findings.Report("verify_asm", strict="--strict" in sys.argv)

    if not os.path.isfile(path):
        print("  no manifest at %s, which is where %s pointed" % (root.rel(where, path), how))
        print("  name one on the command line, or set [verify] manifest in repotools.toml")
        raise SystemExit(findings.EXIT_READ_NOTHING)

    with open(path, "rb") as handle:
        cases = tomllib.load(handle).get("case", [])
    if not cases:
        print("  %s holds no [[case]] entries" % root.rel(where, path))
        raise SystemExit(findings.EXIT_READ_NOTHING)

    # Named by the repository and never assumed. Two repositories here call their throwaway
    # directory build-cov and build-werror, so a tool with `build` written into it would drop
    # object files into a tracked tree in both.
    work = os.path.join(cfg.scratch_root(), "verify_asm")
    os.makedirs(work, exist_ok=True)

    print("")
    print("  Instruction selection, read off the object file. This grades emission, never behavior.")
    print("")

    for case in cases:
        label = case.get("label", "unlabelled")
        if showing and (label != showing):
            continue

        grade = case.get("grade", "emits")
        if grade not in ISSUABLE:
            report.breaking(root.rel(where, path), 0,
                            "%s asks for grade %r, which this tool cannot issue: it runs nothing"
                            % (label, grade))
            continue

        text = disassemble(case, where, work, report)
        if text is None:
            print("  %-24s FAILED" % label)
            continue
        if text == "SKIP":
            print("  %-24s SKIPPED, no %s" % (label, case.get("objdump", case["compiler"])))
            continue

        report.saw()

        if showing:
            print(text)
            continue

        missing = [one for one in case.get("require", [])
                   if not re.search(one, text, re.IGNORECASE)]
        present = [one for one in case.get("forbid", [])
                   if re.search(one, text, re.IGNORECASE)]

        if missing:
            report.breaking(case["source"], 0,
                            "%s did not emit: %s" % (label, " ".join(missing)))
        if present:
            report.breaking(case["source"], 0,
                            "%s emitted what it must not: %s" % (label, " ".join(present)))
        if missing or present:
            print("  %-24s FAILED" % label)
        else:
            print("  %-24s %s %s" % (label, grade, " ".join(case.get("require", []))))

    print("")
    raise SystemExit(report.done())


if __name__ == "__main__":
    main()
