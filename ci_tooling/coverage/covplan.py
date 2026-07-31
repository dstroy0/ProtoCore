#!/usr/bin/env python3
"""covplan.py - partition the branch-coverage backlog into independent work units.

Sources and envs form a bipartite graph (env compiles source). Two work units that
share an env would also share that env's test files and its build dir, so they
cannot be worked in parallel. The connected components of that graph are therefore
the natural unit of parallel work: each one owns its sources, its envs, and its
test suites outright.

    covplan.py                 # list every component with an uncovered branch
    covplan.py --json out.json # machine-readable, for dispatching workers
"""
from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import covmap  # noqa: E402

SKIP_ENVS = {"native_pentest", "native_codeql", "native_tsan"}


def build_units(cov_path: str):
    envs = covmap.parse_envs()
    cov = covmap.load_cov(cov_path)

    missing: dict[str, int] = {}
    for path, rows in cov.items():
        m = sum(b - c for _, b, c in rows)
        if m:
            missing[path] = m

    # union-find over sources + envs
    parent: dict[str, str] = {}

    def find(x):
        parent.setdefault(x, x)
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb

    src_envs: dict[str, list[str]] = {}
    for path in missing:
        owners = [e for e in covmap.owners(envs, path) if e not in SKIP_ENVS]
        src_envs[path] = owners
        find("S:" + path)
        for e in owners:
            union("S:" + path, "E:" + e)

    groups: dict[str, dict] = {}
    for path, owners in src_envs.items():
        g = groups.setdefault(find("S:" + path), {"srcs": [], "envs": set(), "missing": 0})
        g["srcs"].append(path)
        g["envs"].update(owners)
        g["missing"] += missing[path]

    units = []
    for g in groups.values():
        suites = set()
        for e in g["envs"]:
            for t in envs[e]["tests"]:
                suites.add(t)
        units.append({
            "missing": g["missing"],
            "srcs": sorted(g["srcs"], key=lambda p: -missing[p]),
            "envs": sorted(g["envs"]),
            "suites": sorted(suites),
            "per_src": {p: missing[p] for p in sorted(g["srcs"], key=lambda p: -missing[p])},
        })
    units.sort(key=lambda u: -u["missing"])
    return units


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cov", default=covmap.DEFAULT_COV)
    ap.add_argument("--json")
    a = ap.parse_args()
    units = build_units(a.cov)

    if a.json:
        with open(a.json, "w", encoding="utf-8") as fh:
            json.dump(units, fh, indent=1)

    total = sum(u["missing"] for u in units)
    print(f"{len(units)} independent units, {total} uncovered branches\n")
    for i, u in enumerate(units):
        noenv = " !! NO ENV" if not u["envs"] else ""
        print(f"[{i:3d}] {u['missing']:5d} branches  {len(u['srcs']):3d} srcs  "
              f"{len(u['envs']):3d} envs{noenv}")
        for p, m in list(u["per_src"].items())[:6]:
            print(f"        {m:5d}  {p}")
        if len(u["srcs"]) > 6:
            print(f"        ... {len(u['srcs']) - 6} more")
    return 0


if __name__ == "__main__":
    sys.exit(main())
