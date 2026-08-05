#!/usr/bin/env python3
"""covmap.py - map native envs <-> source files, and report uncovered branches.

Local coverage-raising helper. Three sub-commands:

  envs   <src-path>...      envs whose build_src_filter includes those sources
  srcs   <env>...           sources an env compiles
  gaps   [--env E] [paths]  uncovered branches (from a SonarQube coverage xml),
                            annotated with the source line and the owning envs

The env table is parsed from platformio.ini, which is generated from
test/test_matrix.json - so this stays in sync with the suite automatically.
"""

from __future__ import annotations

import argparse
import fnmatch
import os
import re
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
INI = os.path.join(ROOT, "platformio.ini")
DEFAULT_COV = os.path.join(ROOT, "test", "coverage.xml")


def parse_envs() -> dict[str, dict]:
    """env name -> {'src': [glob...], 'tests': [suite...]}"""
    envs: dict[str, dict] = {}
    cur = None
    key = None
    with open(INI, "r", encoding="utf-8") as fh:
        for raw in fh:
            line = raw.rstrip("\n")
            m = re.match(r"^\[env:([^\]]+)\]", line)
            if m:
                cur = {"src": [], "tests": []}
                envs[m.group(1)] = cur
                key = None
                continue
            if re.match(r"^\[", line):
                cur = None
                key = None
                continue
            if cur is None:
                continue
            m = re.match(r"^(\w+)\s*=\s*(.*)$", line)
            if m:
                key = m.group(1)
                rest = m.group(2).strip()
                if key == "build_src_filter" and rest:
                    cur["src"].append(rest)
                elif key == "test_filter" and rest:
                    cur["tests"].append(rest)
                continue
            if line.startswith((" ", "\t")) and key:
                val = line.strip()
                if not val:
                    continue
                if key == "build_src_filter":
                    cur["src"].append(val)
                elif key == "test_filter":
                    cur["tests"].append(val)
    # normalize: '+<a/b.cpp>' -> 'a/b.cpp'
    for e in envs.values():
        pats = []
        for s in e["src"]:
            m = re.match(r"^([+-])<(.*)>$", s)
            if m and m.group(1) == "+":
                pats.append(m.group(2))
        e["src"] = pats
    return envs


def env_covers(env: dict, srcpath: str) -> bool:
    """srcpath is repo-relative, e.g. src/services/iot/coap/coap.cpp."""
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


def main() -> int:
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

    a = ap.parse_args()
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
