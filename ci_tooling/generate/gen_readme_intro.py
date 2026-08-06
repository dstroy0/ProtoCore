#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generate the root README.md intro blocks that must not be hand-maintained.

Not to be confused with gen_readme_sections.py, which owns **docs/README.md**
(the docs landing page). This one owns the **root README.md**, and only two
regions of it:

  QUICK START     the minimal server, copied verbatim from a real sketch
  PROJECT STATUS  measured verification state, from committed artifacts

Why these two are generated:

QUICK START used to not exist at all - the README had zero lines of code, so a
reader could not see the API without leaving the page. Writing one by hand would
have created a second copy of the API to keep in step, and the example READMEs
already show what happens to hand-copied code (see docs/BUGS.md: 86 of them
drifted onto a banned API and 62 onto an enum form that no longer compiles). So
the block is copied from examples/Foundation/Basic/Basic.ino, which CI compiles
as a whole file. If the API changes, the sketch stops compiling and the README
follows in the same commit.

PROJECT STATUS replaces prose that claimed coverage "is lacking" while the
committed coverage report said 99.0% line / 96.1% branch. A progress claim
written by hand is wrong the week after it is written, in either direction.

Usage:  python -m ci_tooling.generate.gen_readme_intro [--check]
"""

import os
import re
import sys

from ci_tooling.lib import doc_region as dr

ROOT = dr.repo_root(__file__)
README = os.path.join(ROOT, "README.md")
SKETCH = os.path.join(ROOT, "examples/Foundation/Basic/Basic.ino")
COVERAGE = os.path.join(ROOT, "test/coverage.xml")
REPORT = os.path.join(ROOT, "test/TEST_REPORT.md")


def read(p):
    try:
        return open(p, encoding="utf-8", errors="replace").read()
    except OSError:
        return ""


# ------------------------------------------------------------------ quick start


def render_quick_start():
    """The sketch, minus its license header and @file doc block.

    Everything else is copied byte for byte: the point of this block is that it
    is the compiled file, not a paraphrase of it.
    """
    src = read(SKETCH)
    if not src:
        raise SystemExit(f"gen_readme_intro: cannot read {SKETCH}")

    # drop the two-line SPDX header and the /** @file ... */ block; both are
    # bookkeeping for the repo, not part of what the reader is being taught
    src = re.sub(r"\A(?://[^\n]*\n)+\s*", "", src)
    src = re.sub(r"\A/\*\*.*?\*/\s*", "", src, flags=re.S)
    src = src.strip("\n")

    # A missing marker or a renamed symbol would otherwise emit a block that
    # looks plausible and teaches nothing. Fail loudly instead.
    #
    # These are the four things the quick start has to SHOW - include the header, register a route,
    # start the server, pump it - named by the free functions that do them. They were spelled as
    # `PC server` / `server.on(` / `server.begin(` / `server.handle()` and so pinned this generator
    # to a class that no longer exists: the guard meant to catch a stale README became the reason it
    # could not be refreshed.
    for needed in ("#include", "on_http(", "begin_http(", "handle()"):
        if needed not in src:
            raise SystemExit(
                f"gen_readme_intro: {SKETCH} no longer contains {needed!r}; "
                "refusing to emit a misleading quick start"
            )

    rel = os.path.relpath(SKETCH, ROOT).replace(os.sep, "/")
    return (
        f"> Copied verbatim from [`{rel}`]({rel}), which CI compiles. "
        "If the API changes, that sketch fails to build and this block changes with it.\n"
        "\n"
        "```cpp\n" + src + "\n```\n"
        "\n"
        f"Set `SSID` / `PASSWORD`, flash, and open Serial at 115200 for the IP. "
        "Full walkthrough: [`examples/Foundation/Basic`](examples/Foundation/Basic/README.md)."
    )


# ---------------------------------------------------------------- project status


def coverage_numbers():
    """(line_pct, branch_pct, files) from the committed SonarQube coverage report."""
    s = read(COVERAGE)
    if not s:
        return None
    total = len(re.findall(r"<lineToCover ", s))
    covered = len(re.findall(r'covered="true"', s))
    btot = sum(int(n) for n in re.findall(r'branchesToCover="(\d+)"', s))
    bcov = sum(int(n) for n in re.findall(r'coveredBranches="(\d+)"', s))
    files = len(re.findall(r"<file path=", s))
    if not total:
        return None
    return (
        100.0 * covered / total,
        (100.0 * bcov / btot) if btot else None,
        files,
    )


def test_numbers():
    """(tests_passed, env_count) from the generated test report."""
    s = read(REPORT)
    passed = re.search(r"(\d+)\s+passed", s)
    envs = re.search(r"over\s+(\d+)\s+auto-discovered native envs", s)
    return (int(passed.group(1)) if passed else None, int(envs.group(1)) if envs else None)


def render_status():
    cov = coverage_numbers()
    passed, envs = test_numbers()
    out = []

    rows = []
    if passed is not None and envs is not None:
        rows.append(("Host test suite", f"**{passed:,} tests** pass across {envs} native environments"))
    if cov:
        line, branch, files = cov
        val = f"**{line:.1f}% line**"
        if branch is not None:
            val += f", **{branch:.1f}% branch**"
        rows.append(("Measured coverage", f"{val} over {files} instrumented files"))
    rows.append(("External audit", "**none** - no third party has reviewed this library"))

    if not rows:
        raise SystemExit("gen_readme_intro: no status inputs found; refusing to emit an empty block")

    out.append("| | |")
    out.append("| --- | --- |")
    for k, v in rows:
        out.append(f"| {k} | {v} |")

    out.append("")
    out.append(
        "Coverage is measured over all of `src/`, with nothing excluded. Numbers come from "
        "`test/coverage.xml` and `test/TEST_REPORT.md`, both regenerated by CI."
    )
    return "\n".join(out)


# -------------------------------------------------------------------- injection

TOOL = dr.tool_id(__file__)
REGIONS = {
    dr.Region("README.md", "QUICK START", TOOL): render_quick_start,
    dr.Region("README.md", "PROJECT STATUS", TOOL): render_status,
}


def main():
    check = "--check" in sys.argv[1:]
    return dr.apply(README, {r: fn() for r, fn in REGIONS.items()}, check=check)


if __name__ == "__main__":
    sys.exit(main())
