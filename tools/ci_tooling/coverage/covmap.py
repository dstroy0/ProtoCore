#!/usr/bin/env python3
"""covmap.py - map native envs <-> source files, and report uncovered branches.

Local coverage-raising helper. Three sub-commands:

  envs   <src-path>...      envs whose build_src_filter includes those sources
  srcs   <env>...           sources an env compiles
  gaps   [--env E] [paths]  uncovered branches (from a SonarQube coverage xml),
                            annotated with the source line and the owning envs

The env table is read from test/test_matrix.json, which defines the suite. It used to be parsed
out of platformio.ini; the native envs are not rendered there any more, since CMake builds them.
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import os
import re
import sys
import xml.etree.ElementTree as ET
from tools.ci_tooling.lib import doc_region as dr

ROOT = dr.repo_root(__file__)
MATRIX = os.path.join(ROOT, "test", "test_matrix.json")
DEFAULT_COV = os.path.join(ROOT, "test", "coverage.xml")


def parse_envs() -> dict[str, dict]:
    """env name -> {'src': [path...], 'tests': [suite...]}, from test/test_matrix.json."""
    with open(MATRIX, "r", encoding="utf-8") as fh:
        table = json.load(fh)
    envs: dict[str, dict] = {}
    for name, e in table.get("envs", {}).items():
        # The matrix writes a source as `+<path>`; some entries carry the src/ prefix and some do
        # not, and env_covers() below compares against a repo-relative path with it stripped.
        pats = []
        for entry in e.get("src") or []:
            mm = re.match(r"^([+-])<(.*)>$", entry.strip())
            if not mm or mm.group(1) != "+":
                continue
            q = mm.group(2)
            pats.append(q[len("src/") :] if q.startswith("src/") else q)
        envs[name] = {"src": pats, "tests": list(e.get("tests") or [])}
    return envs


def env_covers(env: dict, srcpath: str) -> bool:
    """srcpath is repo-relative, e.g. src/services/iot/coap/coap.c."""
    rel = srcpath[4:] if srcpath.startswith("src/") else srcpath
    for pat in env["src"]:
        if fnmatch.fnmatch(rel, pat) or rel == pat:
            return True
        # a directory entry ('+<services/iot/coap/>') covers everything beneath it
        if pat.endswith("/") and rel.startswith(pat):
            return True
    return False


def header_owners(envs: dict[str, dict], srcpath: str) -> list[str]:
    """Headers are not in build_src_filter; attribute them to envs compiling a
    sibling .cpp (same directory), which is where their inline code lands."""
    d = os.path.dirname(srcpath)
    out = []
    for name, e in envs.items():
        for pat in e["src"]:
            if os.path.dirname("src/" + pat) == d:
                out.append(name)
                break
    return out


def owners(envs: dict[str, dict], srcpath: str) -> list[str]:
    direct = [n for n, e in envs.items() if env_covers(e, srcpath)]
    if direct:
        return sorted(direct)
    if srcpath.endswith((".h", ".hpp")):
        return sorted(header_owners(envs, srcpath))
    return []


def load_cov(path: str) -> dict[str, list[tuple[int, int, int]]]:
    """file -> [(line, branchesToCover, coveredBranches)] for branch lines."""
    root = ET.parse(path).getroot()
    out: dict[str, list[tuple[int, int, int]]] = {}
    for f in root.findall("file"):
        rows = []
        for l in f.findall("lineToCover"):
            b = l.get("branchesToCover")
            if b:
                rows.append((int(l.get("lineNumber")), int(b), int(l.get("coveredBranches", "0"))))
        out[f.get("path").replace("\\", "/")] = rows
    return out


def load_lines(path: str) -> dict[str, list[tuple[int, bool]]]:
    """file -> [(line, covered)] for EVERY executable line (not just branch lines).

    Line and branch coverage fail in different places: a wholly untested function shows up as a
    run of uncovered lines with no branch gap at all, so a branch-only report calls it clean.
    """
    root = ET.parse(path).getroot()
    out: dict[str, list[tuple[int, bool]]] = {}
    for f in root.findall("file"):
        out[f.get("path").replace("\\", "/")] = [
            (int(l.get("lineNumber")), l.get("covered") == "true") for l in f.findall("lineToCover")
        ]
    return out


def runs(nums: list[int]) -> list[tuple[int, int]]:
    """Collapse sorted line numbers into (first, last) runs, so a 40-line dead function
    prints as one range instead of forty lines."""
    out: list[tuple[int, int]] = []
    for n in nums:
        if out and n == out[-1][1] + 1:
            out[-1] = (out[-1][0], n)
        else:
            out.append((n, n))
    return out


def src_line(path: str, n: int) -> str:
    try:
        with open(os.path.join(ROOT, path), "r", encoding="utf-8", errors="replace") as fh:
            for i, line in enumerate(fh, 1):
                if i == n:
                    return line.rstrip()
    except OSError:
        pass
    return "<unavailable>"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("envs")
    p.add_argument("paths", nargs="+")

    p = sub.add_parser("srcs")
    p.add_argument("envs", nargs="+")

    p = sub.add_parser("gaps")
    p.add_argument("paths", nargs="*")
    p.add_argument("--cov", default=DEFAULT_COV)
    p.add_argument("--summary", action="store_true", help="per-file totals only")
    p.add_argument("--limit", type=int, default=0)

    p = sub.add_parser("lines", help="uncovered LINES (a wholly untested function has no branch gap)")
    p.add_argument("paths", nargs="*")
    p.add_argument("--cov", default=DEFAULT_COV)
    p.add_argument("--summary", action="store_true")
    p.add_argument("--limit", type=int, default=0)

    a = ap.parse_args(argv)
    envs = parse_envs()

    if a.cmd == "envs":
        for pth in a.paths:
            pth = pth.replace("\\", "/")
            print(f"{pth}: {' '.join(owners(envs, pth)) or '<none>'}")
        return 0

    if a.cmd == "srcs":
        for name in a.envs:
            e = envs.get(name)
            if not e:
                print(f"{name}: <unknown env>")
                continue
            print(f"{name}:")
            for s in e["src"]:
                print(f"  src/{s}")
        return 0

    if a.cmd == "lines":
        lines = load_lines(a.cov)
        sel = [p.replace("\\", "/") for p in a.paths] or sorted(lines)
        tot = cov_n = 0
        rows = []
        for pth in sel:
            entries = lines.get(pth)
            if entries is None:
                print(f"{pth}: <not in coverage report>", file=sys.stderr)
                continue
            miss = sorted(ln for ln, c in entries if not c)
            tot += len(entries)
            cov_n += len(entries) - len(miss)
            if miss:
                rows.append((len(miss), pth, len(entries), miss))
        rows.sort(reverse=True)
        if a.limit:
            rows = rows[: a.limit]
        for nmiss, pth, ntot, miss in rows:
            own = " ".join(owners(envs, pth)) or "<no env>"
            print(f"\n=== {pth}  {ntot - nmiss}/{ntot} lines, {nmiss} uncovered  [envs: {own}]")
            if a.summary:
                continue
            for lo, hi in runs(miss):
                if lo == hi:
                    print(f"  {pth}:{lo}  | {src_line(pth, lo)}")
                else:
                    print(f"  {pth}:{lo}-{hi}  ({hi - lo + 1} lines) | {src_line(pth, lo)}")
        pct = 100.0 * cov_n / tot if tot else 100.0
        print(f"\nTOTAL {cov_n}/{tot} lines = {pct:.3f}%  ({tot - cov_n} uncovered)")
        return 0

    cov = load_cov(a.cov)
    sel = [p.replace("\\", "/") for p in a.paths] or sorted(cov)
    total_b = total_c = 0
    rows = []
    for pth in sel:
        entries = cov.get(pth)
        if entries is None:
            print(f"{pth}: <not in coverage report>", file=sys.stderr)
            continue
        fb = sum(e[1] for e in entries)
        fc = sum(e[2] for e in entries)
        total_b += fb
        total_c += fc
        if fb != fc:
            rows.append((fb - fc, pth, fb, fc, entries))
    rows.sort(reverse=True)
    if a.limit:
        rows = rows[: a.limit]

    for miss, pth, fb, fc, entries in rows:
        own = " ".join(owners(envs, pth)) or "<no env>"
        print(f"\n=== {pth}  {fc}/{fb} branches, {miss} uncovered  [envs: {own}]")
        if a.summary:
            continue
        for ln, b, c in entries:
            if c < b:
                print(f"  {pth}:{ln}  {c}/{b}  | {src_line(pth, ln)}")

    pct = 100.0 * total_c / total_b if total_b else 100.0
    print(f"\nTOTAL {total_c}/{total_b} branches = {pct:.3f}%  ({total_b - total_c} uncovered)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
