#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Coverage from the CMake build: configure instrumented, run ctest, gcovr, union.

The suite is built and run by CMake - `ctest --test-dir build/native` is what turns 441 tests green.
Coverage used to come from somewhere else entirely: `pio run` per env into .pio_cov, which has not
linked for some time (about 1,079 undefined references across the matrix, on a clean tree). So the
measured build and the tested build were two different builds, and only one of them worked.

This measures the one that works.

    cmake -S test -B build/cov  -DCMAKE_C_FLAGS="--coverage -O0"
    ctest --test-dir build/cov
    gcovr per env directory -> coverage_reports/<env>.json
    gcovr --add-tracefile   -> test/coverage.xml   (SonarQube generic)

Per env rather than one pass over the whole tree, for the reason gen_coverage.sh already gives: a
single gcovr over every env's objects cannot tell which env exercised which line, and one env that
aborts takes the whole report with it. A union of per-env tracefiles loses only the env that failed.

Usage:
    python -m tools.ci_tooling.coverage.cmake_coverage                # configure, build, run, report
    python -m tools.ci_tooling.coverage.cmake_coverage --report-only  # gcovr over an existing build
    python -m tools.ci_tooling.coverage.cmake_coverage --envs native_mnt native_arena
"""

import argparse
import glob
import json
import os
import shutil
import subprocess
import sys

from tools.ci_tooling.lib import doc_region as dr

ROOT = dr.repo_root(__file__)
BUILD = os.path.join(ROOT, "build", "cov")
REPORTS = os.path.join(ROOT, "coverage_reports")
OUT = os.path.join(ROOT, "test", "coverage.xml")

# gcov and ThreadSanitizer do not mix: tsan replaces the runtime the counters are written by.
SKIP = {"native_tsan"}


def run(cmd, **kw):
    return subprocess.run(cmd, cwd=kw.pop("cwd", ROOT), **kw)


def configure(cc):
    os.makedirs(BUILD, exist_ok=True)
    args = [
        "cmake",
        "-S",
        os.path.join(ROOT, "test"),
        "-B",
        BUILD,
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Debug",
        # -O0 so a line maps to the statement that wrote it. At -O1 the optimiser folds and moves
        # code, and a report against folded lines says a branch was never taken when it no longer
        # exists as a branch.
        "-DCMAKE_C_FLAGS=--coverage -O0",
        "-DCMAKE_EXE_LINKER_FLAGS=--coverage",
    ]
    if cc:
        args.append("-DCMAKE_C_COMPILER=%s" % cc)
    return run(args, capture_output=True, text=True).returncode


def env_targets():
    """Every ctest name in the instrumented build."""
    r = run(["ctest", "--test-dir", BUILD, "-N"], capture_output=True, text=True)
    out = []
    for line in r.stdout.splitlines():
        if ":" in line and "Test #" in line:
            out.append(line.split(":", 1)[1].strip())
    return [e for e in out if e not in SKIP]


def gcovr_env(env):
    """One env's tracefile, from the object directory CMake gave that target."""
    d = os.path.join(BUILD, "CMakeFiles", "%s.dir" % env)
    if not os.path.isdir(d):
        return False
    out = os.path.join(REPORTS, "%s.json" % env)
    r = run(
        ["gcovr", "--root", ".", "--filter", "src/.*", "--gcov-ignore-parse-errors", "--json", out, d],
        capture_output=True,
        text=True,
    )
    return r.returncode == 0 and os.path.exists(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--envs", nargs="*", help="subset (default: every test in the build)")
    ap.add_argument("--report-only", action="store_true", help="skip configure/build/run")
    ap.add_argument("--jobs", default=str(os.cpu_count() or 4))
    ap.add_argument("--cc", default=os.environ.get("CC"), help="C compiler for the instrumented build")
    a = ap.parse_args()

    if not shutil.which("gcovr"):
        sys.exit("gcovr not on PATH")

    if not a.report_only:
        if configure(a.cc) != 0:
            sys.exit("configure failed for %s" % BUILD)
        if run(["cmake", "--build", BUILD, "-j", a.jobs, "--", "-k", "0"]).returncode != 0:
            print("build reported failures; measuring what did build", file=sys.stderr)
        # Tests may fail; their counters are still written and still worth reporting.
        run(["ctest", "--test-dir", BUILD, "-j", a.jobs, "--output-on-failure"], stdout=subprocess.DEVNULL)

    os.makedirs(REPORTS, exist_ok=True)
    for f in glob.glob(os.path.join(REPORTS, "*.json")):
        os.remove(f)

    envs = a.envs or env_targets()
    ok, bad = 0, []
    for n, e in enumerate(envs, 1):
        if gcovr_env(e):
            ok += 1
        else:
            bad.append(e)
        if n % 50 == 0:
            print("  %d/%d" % (n, len(envs)), file=sys.stderr)
    print("gcovr: %d env(s) measured, %d without coverage data" % (ok, len(bad)), file=sys.stderr)
    if bad:
        print("  no data: %s" % ", ".join(sorted(bad)[:8]), file=sys.stderr)

    if not ok:
        sys.exit("no tracefiles; nothing to union")

    # --merge-mode-functions=separate is required, not a preference: without it gcovr 8.6 exits 64
    # and writes nothing at all.
    #
    # 107 names under src/ are defined twice in one file, under opposite arms of a conditional -
    # sha256_block at 102 and 148, blk_enc, ecdsa_hw_on/off, and the rest of the hardware-accelerated
    # against software-fallback pairs. That is deliberate, and the matrix runs both arms on purpose:
    # native_sha256_kat and native_sha256_kat_hw are the same suite over the two implementations. So
    # the union sees one name at two line numbers and the default merge mode refuses it. `separate`
    # keeps them apart, which is what they are - two functions, not one measured twice.
    r = run(
        [
            "gcovr",
            "--add-tracefile",
            os.path.join(REPORTS, "*.json"),
            "--merge-mode-functions",
            "separate",
            "--sonarqube",
            OUT,
        ],
        capture_output=True,
        text=True,
    )
    if r.returncode != 0:
        sys.exit("union failed: %s" % r.stderr.strip()[:400])

    run([sys.executable, "-m", "tools.ci_tooling.coverage.dedupe_sonar_cov", OUT])
    print("wrote %s" % os.path.relpath(OUT, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
