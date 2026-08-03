#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Every translation unit under src/ is exercised by a named test env.

`src/` is protocol logic and nothing else, so every file in it runs on the host. The one
exemption is `src/board_drivers/`, which speaks to vendor SDKs and only runs on silicon.

The check is that a TU is named by at least one env's `build_src_filter` in
test/test_matrix.json. Being *compiled* is not the bar: 24 envs carry no filter at all and
so build the whole tree, which would mark every file covered while testing none of it
deliberately. A file named in a filter has a suite that chose to build it.

This is a ratchet (ci_tooling/lib/baseline.py): the TUs that have no env today are recorded,
CI fails on a NEW one, and the floor drops as suites are added. A new module therefore
arrives with its `native_<name>` env or it does not merge - which is the rule
docs/SRCBANNED.md already states under "Required", now enforced instead of remembered.

Usage:
    python ci_tooling/check/check_test_coverage.py           # fail on a TU with no env
    python ci_tooling/check/check_test_coverage.py --list    # print the uncovered set
    python ci_tooling/check/check_test_coverage.py --save    # re-record the floor
"""

import json
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import baseline  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MATRIX = os.path.join(ROOT, "test", "test_matrix.json")
BASELINE = baseline.path_for(__file__)
EXTS = (".c", ".cpp")
EXEMPT = ("src/board_drivers/",)  # the vendor seam: no host path to exercise


def translation_units():
    """Every src/ TU the rule applies to, repo-relative with forward slashes."""
    out = []
    for dirpath, _dirnames, filenames in os.walk(os.path.join(ROOT, "src")):
        rel = os.path.relpath(dirpath, ROOT).replace(os.sep, "/")
        if any(rel.startswith(e.rstrip("/")) for e in EXEMPT):
            continue
        for f in filenames:
            if f.endswith(EXTS):
                out.append(rel + "/" + f)
    return sorted(out)


def named_by_an_env():
    """Every src/ path named by some env's build_src_filter."""
    with open(MATRIX, encoding="utf-8") as fh:
        envs = json.load(fh)["envs"]
    named = set()
    for entry in envs.values():
        for line in entry.get("src") or ():
            m = re.match(r"^\+<(.+)>$", line.strip())
            if m:
                named.add("src/" + m.group(1))
    return named


def main(argv):
    want_list = "--list" in argv
    want_save = "--save" in argv

    tus = translation_units()
    named = named_by_an_env()
    uncovered = [t for t in tus if t not in named]

    if want_save:
        n = baseline.save(BASELINE, uncovered)
        print(f"check_test_coverage: recorded {n} uncovered TU(s) as the floor")
        return 0

    if want_list:
        for t in uncovered:
            print(t)
        return 0

    new, known, fixed = baseline.filter_new(uncovered, lambda t: t, BASELINE)

    if new:
        print(
            f"check_test_coverage: {len(new)} src/ translation unit(s) have no test env:",
            file=sys.stderr,
        )
        for t in new:
            print(f"  {t}", file=sys.stderr)
        print(
            "\nAdd a native_<name> entry to test/test_matrix.json naming the file in its src\n"
            "filter, then regenerate with test/gen_test_envs.py. A module with no host test\n"
            "is a module whose behavior is asserted only by reading it.",
            file=sys.stderr,
        )
        return 1

    print(
        f"check_test_coverage: OK - {len(tus) - len(uncovered)} of {len(tus)} src/ TU(s) named by an env "
        f"({known} known gap(s) remain, {fixed} closed)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
