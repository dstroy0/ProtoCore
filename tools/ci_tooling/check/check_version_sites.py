#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Every place the project version is written is registered in .bumpversion.cfg.

bump2version rewrites exactly the files its config names, using each entry's `search` string. A
version printed anywhere else is not an error it can report - the release simply ships with that
file still reading the previous number, and nothing says so.

Checks:
  1. every [bumpversion:file:X] names a file that exists
  2. each entry's `search` string, with {current_version} substituted, occurs in that file
  3. an entry covers every occurrence of the version in its file - a `search` narrower than the
     file's contents leaves the rest behind (README carries `v{version}` in a badge; a bare
     `{version}` elsewhere in the same file would not be rewritten)
  4. no file outside the config contains the current version

A version is three dotted numbers, a shape protocol constants and spec citations share: SMTP
enhanced status codes, Sparkplug spec versions, RFC section numbers, and every dotted-quad address
in a vendored RFC text. Counting raw substrings makes each of those a version site the day the
project bumps onto its digits. Two conditions narrow a match to the project's own version: the
digits stand alone rather than sitting inside a longer dotted run, and they carry a `v` prefix or
follow the word `version` within three lines, which is the shape of all nine registered sites.

Scanning is over the working tree minus .git, .pio, node_modules, __pycache__, build output and
vendored dependency trees, whose versions are their own. Resolved dependency locks name the
versions of what they pin, so they are skipped by name wherever a package manager writes them.

Exit 0 when every site is registered, 1 otherwise.
"""

import argparse
import bisect
import configparser
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
CFG = os.path.join(ROOT, ".bumpversion.cfg")

SKIP_DIRS = {".git", ".pio", ".pio_cov", "__pycache__", "node_modules", "managed_components",
             "coverage_reports", ".mypy_cache", ".ruff_cache", ".codeql-local"}
# Build output the toolchains write in place, named as .gitignore names them. `build` is only
# output at these paths - tools/ci_tooling/build holds a script and stays in the scan.
SKIP_PATHS = ("build", "docs/html", "docs/favicons/dist")
# Their own version histories, which legitimately name every release this project ever cut.
SKIP_FILES = {".bumpversion.cfg", "docs/CHANGELOG.md", "CHANGELOG.md", "package-lock.json"}
# Resolved dependency locks, which name the versions of what they pin, wherever they are written.
SKIP_NAMES = {"package-lock.json", "dependencies.lock", "sdkconfig", "sdkconfig.old"}
# Vendored spec texts, whose section numbers and example addresses are their own.
SKIP_PREFIXES = ("docs/learn/rfc/",)
# Lines of context before a bare version in which the word `version` marks it as one.
KEY_LOOKBACK = 3
# The per-file stamp (tools/ci_tooling/check/stamp_version.py), which the pre-commit hook moves
# per file rather than per release. It names a version and is not a site a bump rewrites.
STAMP_RE = re.compile(r"ProtoCore v\d+\.\d+\.\d+ - Copyright \(C\) 2026 Douglas Quigg")


def read(path):
    with io.open(path, encoding="utf-8", errors="ignore", newline="") as f:
        return f.read()


def version_hits(text, cur):
    """Count occurrences of `cur` that name the project version rather than a protocol constant."""
    pat = r"(?<![0-9.])" + re.escape(cur) + r"(?![0-9]|\.[0-9])"
    lines = text.split("\n")
    starts, acc = [], 0
    for ln in lines:
        starts.append(acc)
        acc += len(ln) + 1
    n = 0
    for m in re.finditer(pat, text):
        li = bisect.bisect_right(starts, m.start()) - 1
        col = m.start() - starts[li]
        if STAMP_RE.search(lines[li]):
            continue
        if col > 0 and lines[li][col - 1] == "v":
            n += 1
            continue
        if "version" in "\n".join(lines[max(0, li - KEY_LOOKBACK):li + 1]).lower():
            n += 1
    return n


def check():
    bad = []
    cp = configparser.ConfigParser()
    cp.read(CFG, encoding="utf-8")
    if "bumpversion" not in cp:
        return ["%s has no [bumpversion] section" % os.path.basename(CFG)], None
    cur = cp["bumpversion"]["current_version"].strip()

    covered = set()
    for sec in cp.sections():
        if not sec.startswith("bumpversion:file:"):
            continue
        rel = sec[len("bumpversion:file:"):]
        covered.add(rel)
        path = os.path.join(ROOT, *rel.split("/"))
        if not os.path.isfile(path):
            bad.append("%s: named in the config, absent from the tree" % rel)
            continue
        search = cp[sec].get("search", "{current_version}").replace("{current_version}", cur)
        text = read(path)
        n_search = text.count(search)
        n_version = version_hits(text, cur)
        if n_search == 0:
            bad.append("%s: search string %r does not occur - a bump would silently skip it" % (rel, search))
        elif n_search != n_version:
            bad.append("%s: %d occurrence(s) of %s lie outside the search string %r"
                       % (rel, n_version - n_search, cur, search))

    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        rel_dir = os.path.relpath(dirpath, ROOT).replace("\\", "/")
        if rel_dir in SKIP_PATHS or rel_dir.startswith(tuple(p + "/" for p in SKIP_PATHS)):
            dirnames[:] = []
            continue
        for fn in filenames:
            rel = os.path.relpath(os.path.join(dirpath, fn), ROOT).replace("\\", "/")
            if rel in covered or rel in SKIP_FILES or rel.startswith(SKIP_PREFIXES):
                continue
            if fn in SKIP_NAMES:
                continue
            try:
                text = read(os.path.join(dirpath, fn))
            except (OSError, ValueError):
                continue
            if version_hits(text, cur):
                bad.append("%s: carries version %s but is not in .bumpversion.cfg - add a "
                           "[bumpversion:file:%s] entry" % (rel, cur, rel))
    return bad, cur


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-q", "--quiet", action="store_true", help="print nothing when every site is registered")
    a = ap.parse_args()

    bad, cur = check()
    if bad:
        print("version sites out of sync with .bumpversion.cfg (%d):" % len(bad), file=sys.stderr)
        for b in bad:
            print("  " + b, file=sys.stderr)
        return 1
    if not a.quiet:
        print("every site carrying version %s is registered in .bumpversion.cfg." % cur)
    return 0


if __name__ == "__main__":
    sys.exit(main())
