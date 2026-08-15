#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Regenerate the counts block in docs/INTEROP_MATRIX.md from the tree.

Every number in "Status at a glance" is derivable, and hand-maintaining them failed:
the table claimed 34 interop peers / ~39 benches while the tree had 39 / 207, and the
prose contradicted its own table ("11 of ~50"). A matrix that understates coverage is
worse than none - it sends you to build a bench that already exists.

Counted from:
  interop peers  test/servers/peers/*_peer.py
  benches        test/performance_benching/**/platformio.ini  (one per benchable unit)
  attacks        @attack(...) decorators in test/penetration_testing/pc_pentest.py
  fuzz           `void test_*` case count in the native_pentest suite source
  services       src/services/<group>/<module>/

Usage:  python -m tools.ci_tooling.generate.gen_interop_matrix [--check]
"""

import os
import re
import subprocess
import sys

from tools.ci_tooling.lib import doc_region as dr

ROOT = dr.repo_root(__file__)
DOC = os.path.join(ROOT, "docs/INTEROP_MATRIX.md")
REGION = dr.Region(DOC, "COUNTS", dr.tool_id(__file__))

# The single suite `[env:native_pentest]` selects through its test_filter (platformio.ini).
FUZZ_SUITE = "test/unit/src/network_drivers/application/smb/test_pentest/test_pentest.c"


def sh(*a):
    return subprocess.run(a, cwd=ROOT, capture_output=True, text=True).stdout


def main() -> int:
    peers = [p for p in sh("git", "ls-files", "test/servers/peers/*_peer.py").split() if p]
    benches = [p for p in sh("git", "ls-files", "test/performance_benching").split() if p.endswith("platformio.ini")]
    services = [p for p in sh("git", "ls-files", "src/services").split()]
    modules = sorted({"/".join(p.split("/")[:4]) for p in services if len(p.split("/")) >= 5})

    # Adversarial coverage has TWO halves and they are not interchangeable:
    #   attacks - targeted, protocol-aware exploits fired at a LIVE rig over the network
    #             (test/penetration_testing/pc_pentest.py, @attack decorators)
    #   fuzz    - host-level adversarial input in the native_pentest env, run separately
    #             from the main suite because it is heavy
    pentest = os.path.join(ROOT, "test/penetration_testing/pc_pentest.py")
    attacks = 0
    if os.path.exists(pentest):
        attacks = len(re.findall(r"^@attack\(", open(pentest, encoding="utf-8", errors="replace").read(), re.M))

    # The suite carries no RUN_TEST: test/harness.py generate_runner() feeds the one source
    # holding `void test_` to Unity's generate_test_runner.rb, which emits them. Count the
    # case definitions the runner is built from.
    fuzzer = os.path.join(ROOT, FUZZ_SUITE)
    fuzz = 0
    if os.path.exists(fuzzer):
        fuzz = len(re.findall(r"^void\s+test_\w+\s*\(", open(fuzzer, encoding="utf-8", errors="replace").read(), re.M))

    peer_names = sorted(os.path.basename(p)[: -len("_peer.py")] for p in peers)

    block = [
        "| Dimension            | Count | Source of truth                                  |",
        "| -------------------- | ----: | ------------------------------------------------ |",
        f"| Interop peers        | {len(peers):>5} | `test/servers/peers/*_peer.py`                   |",
        f"| Throughput benches   | {len(benches):>5} | `test/performance_benching/**/platformio.ini`    |",
        f"| Advanced attacks     | {attacks:>5} | `@attack(...)` in `test/penetration_testing/pc_pentest.py`       |",
        f"| Adversarial fuzz     | {fuzz:>5} | `native_pentest` (`{os.path.dirname(FUZZ_SUITE)}/`)          |",
        f"| Service modules      | {len(modules):>5} | `src/services/<group>/<module>/`                 |",
        "",
        f"Interop peers cover **{len(peers)} of {len(modules)}** service modules. Not every module needs a"
        " third-party",
        "peer - many are pure codecs with pinned spec vectors, and some need hardware the project does not" " have -",
        "but the ratio is the honest measure of how much is judged by something other than ourselves.",
        "",
        "**Peers:** " + ", ".join(f"`{n}`" for n in peer_names),
    ]
    return dr.apply(DOC, {REGION: "\n".join(block)}, check="--check" in sys.argv)


if __name__ == "__main__":
    sys.exit(main())
