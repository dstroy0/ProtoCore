#!/usr/bin/env python3
"""covrun.py - run selected native envs instrumented and report their coverage gaps.

The whole-suite loop (test/run_tests.sh --coverage) is far too slow to iterate against while
writing tests. This runs only the envs that compile the sources you are working on, gcovr's each,
and unions those into a scratch report to measure against. It writes nothing shared, so several
can run at once; test/coverage.xml is written only by covbase.py, over the whole matrix.

  covrun.py --src src/services/system/control/control.c   # envs inferred
  covrun.py --env native_control native_coap              # explicit envs

Prints the remaining branch gaps for the touched sources when it is done.
"""
from __future__ import annotations

import argparse
import glob
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import covmap  # noqa: E402

ROOT = covmap.ROOT

# Set per-invocation by main(); parallel workers each pass their own so two concurrent runs never
# share a PlatformIO build dir (they would clobber each other's .gcda) or a report dir.
BUILD_DIR = os.path.join(ROOT, ".pio_cov")
REPORTS = os.path.join(ROOT, "coverage_reports")


# Windows Application Control intermittently refuses to launch a freshly-linked test binary
# ("[WinError 4551] An Application Control policy has blocked this file"). It is not a build or
# test failure and it clears on a rebuild, but left alone it shows up as a failed env and would
# block the merge - so retry once rather than lose the run.
_TRANSIENT = ("WinError 4551", "Application Control policy")


def run_env(env: str, jobs: int, _retry: bool = True) -> bool:
    envvars = dict(os.environ)
    envvars["PLATFORMIO_BUILD_DIR"] = BUILD_DIR
    envvars["PLATFORMIO_BUILD_FLAGS"] = "-fprofile-arcs -ftest-coverage -lgcov"
    # stale .gcda from an earlier layout of the same source makes gcov bail out
    for gcda in glob.glob(os.path.join(BUILD_DIR, env, "**", "*.gcda"), recursive=True):
        os.unlink(gcda)
    p = subprocess.run(
        ["pio", "test", "-e", env],
        cwd=ROOT, env=envvars, capture_output=True, text=True,
    )
    if p.returncode == 0:
        return True
    out = p.stdout + p.stderr
    if _retry and any(m in out for m in _TRANSIENT):
        print(f"  {env}: blocked by Application Control, rebuilding once", flush=True)
        shutil.rmtree(os.path.join(BUILD_DIR, env), ignore_errors=True)
        return run_env(env, jobs, _retry=False)
    sys.stdout.write(p.stdout[-4000:])
    sys.stdout.write(p.stderr[-2000:])
    return False


_GCOVR_PY: str | None = None


def gcovr_python() -> str:
    """Interpreter that can `-m gcovr`.

    Not necessarily the one running this script: PlatformIO puts its own venv first on PATH, and
    that venv has no gcovr, so `sys.executable` silently produces no report. Resolve it once, and
    fail loudly rather than leaving an empty report dir to be read as "no coverage".
    """
    global _GCOVR_PY
    if _GCOVR_PY:
        return _GCOVR_PY
    cands = [sys.executable, shutil.which("python"), shutil.which("python3")]
    cands += sorted(glob.glob(os.path.expanduser(
        r"~/AppData/Local/Programs/Python/Python*/python.exe")), reverse=True)
    seen = []
    for cand in cands:
        if not cand or cand in seen:
            continue
        seen.append(cand)
        p = subprocess.run([cand, "-m", "gcovr", "--version"], capture_output=True, text=True)
        if p.returncode == 0:
            _GCOVR_PY = cand
            return cand
    raise SystemExit(f"no interpreter with gcovr installed (tried: {seen}); "
                     f"run `<python> -m pip install gcovr`")


def gcovr(env: str) -> None:
    os.makedirs(REPORTS, exist_ok=True)
    out = os.path.join(REPORTS, f"{env}.xml")
    # Also emit gcovr's JSON: it keeps the PER-BRANCH counts that the SonarQube generic format
    # throws away, which is what lets --add-tracefile union a condition whose branches are split
    # across envs instead of keeping only the best single env's aggregate.
    js = os.path.join(REPORTS, f"{env}.json")
    p = subprocess.run(
        [gcovr_python(), "-m", "gcovr", "--root", ".", "--filter", "src/.*",
         "--gcov-ignore-parse-errors", "--sonarqube", out, "--json", js,
         os.path.join(BUILD_DIR, env)],
        cwd=ROOT, capture_output=True, text=True,
    )
    if p.returncode != 0 or not os.path.exists(out):
        sys.stdout.write(p.stdout[-2000:] + p.stderr[-2000:])
        raise SystemExit(f"gcovr produced no report for {env}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", nargs="*", default=[])
    ap.add_argument("--env", nargs="*", default=[])
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--keep-reports", action="store_true")
    ap.add_argument("--build-dir", default=".pio_cov")
    ap.add_argument("--reports-dir", default="coverage_reports")
    a = ap.parse_args()

    global BUILD_DIR, REPORTS
    BUILD_DIR = os.path.join(ROOT, a.build_dir)
    REPORTS = os.path.join(ROOT, a.reports_dir)

    envs_tbl = covmap.parse_envs()
    srcs = [s.replace("\\", "/") for s in a.src]
    envs = list(a.env)
    for s in srcs:
        for e in covmap.owners(envs_tbl, s):
            if e not in envs and e not in ("native_pentest", "native_codeql", "native_tsan"):
                envs.append(e)
    if not envs:
        print("nothing to run (no envs matched)", file=sys.stderr)
        return 2

    print(f"envs: {' '.join(envs)}")
    shutil.rmtree(REPORTS, ignore_errors=True)
    failed = []
    for i, e in enumerate(envs, 1):
        ok = run_env(e, a.jobs)
        print(f"[{i}/{len(envs)}] {'ok  ' if ok else 'FAIL'} {e}", flush=True)
        if not ok:
            failed.append(e)
        gcovr(e)

    if failed:
        # A failing env produces truncated .gcda (the run aborted partway), so merging it would
        # write a bogus low-coverage record over a good baseline. Bail out instead.
        print(f"\nFAILED ENVS: {' '.join(failed)} - baseline NOT updated")
        shutil.rmtree(REPORTS, ignore_errors=True)
        return 1

    # This ran a subset of the envs, so it measures a subset of src/. It reports the gaps in what
    # it ran and writes nothing shared; test/coverage.xml is only ever written by a whole-matrix
    # run (covbase.py), so the committed report always covers all of src/.
    scratch = os.path.join(REPORTS, "_union.xml")
    subprocess.run(
        [gcovr_python(), "-m", "gcovr",
         "--add-tracefile", os.path.join(a.reports_dir, "*.json"),
         "--sonarqube", scratch],
        cwd=ROOT, check=True,
    )
    if srcs:
        subprocess.run([gcovr_python(), "ci_tooling/coverage/covmap.py", "gaps", "--cov", scratch, *srcs], cwd=ROOT)
    print(f"gaps reported against {scratch}; run covbase.py to refresh test/coverage.xml")

    if not a.keep_reports:
        shutil.rmtree(REPORTS, ignore_errors=True)
    if srcs:
        subprocess.run([gcovr_python(), "ci_tooling/coverage/covmap.py", "gaps", *srcs], cwd=ROOT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
