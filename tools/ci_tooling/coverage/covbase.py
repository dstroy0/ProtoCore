#!/usr/bin/env python3
"""covbase.py - regenerate test/coverage.xml from scratch over every native env.

covrun.py is the inner loop (a few envs, overlaid on the committed baseline). This is
the outer one: run the whole native matrix instrumented and write a fresh whole-project
report with no baseline overlay at all. Needed whenever something changes the shape of
the measurement itself (a compiler flag, a new env), because an overlay merge keeps the
richer per-line branch record and would preserve the stale shape forever.

Sequential by design - one compile at a time keeps it to roughly one core, so it can run
in the background while other work continues.

  covbase.py                      # all envs -> test/coverage.xml
  covbase.py --out /tmp/base.xml  # somewhere else
  covbase.py --resume             # skip envs whose report already exists
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time

from tools.ci_tooling.coverage import covmap
from tools.ci_tooling.coverage import covrun

ROOT = covmap.ROOT
SKIP = {"native_pentest", "native_codeql", "native_tsan", "esp32dev"}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join("test", "coverage.xml"))
    ap.add_argument("--build-dir", default=".pio_base")
    ap.add_argument("--reports-dir", default=".cov_base")
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--only", nargs="*", default=[])
    a = ap.parse_args()

    covrun.BUILD_DIR = os.path.join(ROOT, a.build_dir)
    covrun.REPORTS = os.path.join(ROOT, a.reports_dir)
    os.makedirs(covrun.REPORTS, exist_ok=True)

    envs = [e for e in covmap.parse_envs() if e.startswith("native") and e not in SKIP]
    if a.only:
        envs = [e for e in envs if e in a.only]

    t0 = time.time()
    failed = []
    for i, e in enumerate(envs, 1):
        rpt = os.path.join(covrun.REPORTS, f"{e}.xml")
        if a.resume and os.path.exists(rpt):
            print(f"[{i}/{len(envs)}] skip {e}", flush=True)
            continue
        ok = covrun.run_env(e, 1)
        try:
            covrun.gcovr(e)
        except SystemExit:
            ok = False
        if not ok:
            failed.append(e)
        el = int(time.time() - t0)
        print(f"[{i}/{len(envs)}] {'ok  ' if ok else 'FAIL'} {e}  ({el // 60}m{el % 60:02d}s)", flush=True)

    print(f"\n{len(envs) - len(failed)}/{len(envs)} envs ok")
    if failed:
        print(f"FAILED: {' '.join(failed)}")

    # Every env ran, so this is the whole measurement: gcovr unions the per-env tracefiles and
    # emits the report in one pass. Nothing stale can be carried forward because nothing is
    # carried forward.
    subprocess.run(
        [
            covrun.gcovr_python(),
            "-m",
            "gcovr",
            "--add-tracefile",
            os.path.join(a.reports_dir, "*.json"),
            "--sonarqube",
            a.out,
        ],
        cwd=ROOT,
        check=True,
    )
    print(f"wrote {a.out}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
