#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

Scanning is over the working tree minus .git, .pio, node_modules, __pycache__ and vendored
dependency trees, whose versions are their own.

Exit 0 when every site is registered, 1 otherwise.
"""

import argparse
import configparser
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
CFG = os.path.join(ROOT, ".bumpversion.cfg")

SKIP_DIRS = {".git", ".pio", ".pio_cov", "__pycache__", "node_modules", "managed_components",
             "coverage_reports", ".mypy_cache", ".ruff_cache"}
# Their own version histories, which legitimately name every release this project ever cut.
SKIP_FILES = {".bumpversion.cfg", "docs/CHANGELOG.md", "CHANGELOG.md", "package-lock.json"}


def read(path):
    with io.open(path, encoding="utf-8", errors="ignore", newline="") as f:
        return f.read()


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
        n_version = text.count(cur)
        if n_search == 0:
            bad.append("%s: search string %r does not occur - a bump would silently skip it" % (rel, search))
        elif n_search != n_version:
            bad.append("%s: %d occurrence(s) of %s lie outside the search string %r"
                       % (rel, n_version - n_search, cur, search))

    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for fn in filenames:
            rel = os.path.relpath(os.path.join(dirpath, fn), ROOT).replace("\\", "/")
            if rel in covered or rel in SKIP_FILES:
                continue
            try:
                text = read(os.path.join(dirpath, fn))
            except (OSError, ValueError):
                continue
            if cur in text:
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
