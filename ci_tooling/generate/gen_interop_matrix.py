#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Regenerate the counts block in docs/INTEROP_MATRIX.md from the tree.

Every number in "Status at a glance" is derivable, and hand-maintaining them failed:
the table claimed 34 interop peers / ~39 benches while the tree had 39 / 207, and the
prose contradicted its own table ("11 of ~50"). A matrix that understates coverage is
worse than none - it sends you to build a bench that already exists.

Counted from:
  interop peers  test/servers/peers/*_peer.py
  benches        performance_benching/**/platformio.ini  (one per benchable unit)
  attacks        @attack(...) decorators in penetration_testing/pc_pentest.py
  fuzz           RUN_TEST count in test/test_pentest/test_pentest.c
  services       src/services/<group>/<module>/

Usage:  python ci_tooling/generate/gen_interop_matrix.py [--check]
"""

import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import doc_region as dr  # noqa: E402  (path set above)

ROOT = dr.repo_root(__file__)
DOC = os.path.join(ROOT, "docs/INTEROP_MATRIX.md")
REGION = dr.Region(DOC, "COUNTS", dr.tool_id(__file__))


def sh(*a):
    return subprocess.run(a, cwd=ROOT, capture_output=True, text=True).stdout


def main() -> int:
    peers = [p for p in sh("git", "ls-files", "test/servers/peers/*_peer.py").split() if p]
    benches = [p for p in sh("git", "ls-files", "performance_benching").split()
               if p.endswith("platformio.ini")]
    services = [p for p in sh("git", "ls-files", "src/services").split()]
    modules = sorted({"/".join(p.split("/")[:4]) for p in services
                      if len(p.split("/")) >= 5})

    # Adversarial coverage has TWO halves and they are not interchangeable:
    #   attacks - targeted, protocol-aware exploits fired at a LIVE rig over the network
    #             (penetration_testing/pc_pentest.py, @attack decorators)
    #   fuzz    - host-level adversarial input in the native_pentest env, run separately
    #             from the main suite because it is heavy
    pentest = os.path.join(ROOT, "penetration_testing/pc_pentest.py")
    attacks = 0
    if os.path.exists(pentest):
        attacks = len(re.findall(
            r"^@attack\(", open(pentest, encoding="utf-8", errors="replace").read(), re.M))

    fuzzer = os.path.join(ROOT, "test/test_pentest/test_pentest.c")
    fuzz = 0
    if os.path.exists(fuzzer):
        fuzz = len(re.findall(
            r"\bRUN_TEST\s*\(", open(fuzzer, encoding="utf-8", errors="replace").read()))

    peer_names = sorted(os.path.basename(p)[: -len("_peer.py")] for p in peers)

    block = [
        "| Dimension            | Count | Source of truth                                  |",
        "| -------------------- | ----: | ------------------------------------------------ |",
        f"| Interop peers        | {len(peers):>5} | `test/servers/peers/*_peer.py`                   |",
        f"| Throughput benches   | {len(benches):>5} | `performance_benching/**/platformio.ini`         |",
        f"| Advanced attacks     | {attacks:>5} | `@attack(...)` in `penetration_testing/pc_pentest.py`     |",
        f"| Adversarial fuzz     | {fuzz:>5} | `native_pentest` (`test/test_pentest/`)          |",
        f"| Service modules      | {len(modules):>5} | `src/services/<group>/<module>/`                 |",
        "",
        f"Interop peers cover **{len(peers)} of {len(modules)}** service modules. Not every module needs a"
        " third-party",
        "peer - many are pure codecs with pinned spec vectors, and some need hardware the project does not"
        " have -",
        "but the ratio is the honest measure of how much is judged by something other than ourselves.",
        "",
        "**Peers:** " + ", ".join(f"`{n}`" for n in peer_names),
    ]
    return dr.apply(DOC, {REGION: "\n".join(block)}, check="--check" in sys.argv)


if __name__ == "__main__":
    sys.exit(main())
