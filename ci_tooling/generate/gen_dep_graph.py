#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""gen_dep_graph.py - build test/dep_graph.json from the compiler's own dependency output.

For every native env, run `pio run -t compiledb -e <env>` to capture the exact compile command
(flags, -I, -D, -std) of each translation unit, then ask the compiler for that TU's transitive
include list with `g++ -MM`. The union over an env's TUs is every repo file (source AND header)
that env compiles or includes; inverting that gives:

    { "src/services/iot/coap/coap.h": ["native_coap", "native_coap_observe"], ... }

select_envs.py consults this to map a changed HEADER to exactly the envs whose include closure
contains it - replacing the conservative "any header -> FULL" heuristic with the real graph.
A file absent from the map (a brand-new file, or one no env compiles) still falls back to FULL.

Deterministic output (sorted keys + sorted env lists) so it only changes when the include
structure actually changes; regenerated on FULL CI runs and committed.

Usage:
    ci_tooling/generate/gen_dep_graph.py                       # all native envs -> test/dep_graph.json
    ci_tooling/generate/gen_dep_graph.py --envs native_coap native_smtp   # a subset (for validation)
    ci_tooling/generate/gen_dep_graph.py --jobs 8 --out test/dep_graph.json
"""

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
INI = os.path.join(ROOT, "platformio.ini")
# Tooling-only envs that never carry a test suite (mirror select_envs.NEVER_SELECT + codeql).
SKIP_ENVS = {"native_codeql"}


def native_envs():
    text = open(INI, encoding="utf-8").read()
    envs = re.findall(r"^\[env:(native[A-Za-z0-9_]*)\]", text, re.M)
    return [e for e in envs if e not in SKIP_ENVS]


def pio_compiledb(env):
    """Run compiledb for one env; return its parsed compile_commands.json entries."""
    cc = os.path.join(ROOT, "compile_commands.json")
    if os.path.exists(cc):
        os.remove(cc)
    subprocess.run(
        ["pio", "run", "-t", "compiledb", "-e", env],
        cwd=ROOT,
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    with open(cc, encoding="utf-8") as fh:
        db = json.load(fh)
    os.remove(cc)
    return db


def mm_command(entry):
    """Turn a compile entry into a `g++ -MM ...` command (drop -c / -o / -M* output flags)."""
    args = entry.get("arguments") or shlex.split(entry["command"])
    out = [args[0], "-MM"]
    i = 1
    while i < len(args):
        a = args[i]
        if a == "-o":
            i += 2
            continue
        if a in ("-c", "-MMD", "-MD", "-MP", "-MG"):
            i += 1
            continue
        if a == "-MF":
            i += 2
            continue
        out.append(a)
        i += 1
    return out, entry.get("directory", ROOT)


def rel_dep(path, directory):
    """Repo-relative POSIX path for a dependency, or None if outside the repo (toolchain header)."""
    ap = path if os.path.isabs(path) else os.path.normpath(os.path.join(directory, path))
    try:
        rel = os.path.relpath(ap, ROOT)
    except ValueError:
        return None
    if rel.startswith(".."):
        return None
    return rel.replace("\\", "/")


def deps_of(entry):
    cmd, directory = mm_command(entry)
    r = subprocess.run(cmd, cwd=directory, capture_output=True, text=True)
    if r.returncode != 0 or ":" not in r.stdout:
        return []
    body = r.stdout.replace("\\\n", " ").split(":", 1)[1]
    files = []
    for tok in body.split():
        rel = rel_dep(tok, directory)
        if rel and (rel.endswith((".h", ".hpp", ".inc", ".cpp", ".c", ".ino"))):
            files.append(rel)
    return files


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--envs", nargs="*", help="subset of envs (default: all native envs)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--out", default=os.path.join(ROOT, "test", "dep_graph.json"))
    ap.add_argument(
        "--merge",
        action="store_true",
        help="update only --envs in an existing --out: drop those envs everywhere, then re-add their "
        "fresh deps (keeps the committed graph current after an affected run without a full rebuild)",
    )
    args = ap.parse_args()

    envs = args.envs or native_envs()
    graph = {}
    for n, env in enumerate(envs, 1):
        try:
            db = pio_compiledb(env)
        except subprocess.CalledProcessError:
            print(f"[{n}/{len(envs)}] {env}: compiledb failed, skipping", file=sys.stderr)
            continue
        seen = set()
        with ThreadPoolExecutor(max_workers=args.jobs) as ex:
            for deps in ex.map(deps_of, db):
                seen.update(deps)
        for f in seen:
            graph.setdefault(f, set()).add(env)
        print(f"[{n}/{len(envs)}] {env}: {len(db)} TUs -> {len(seen)} repo deps", file=sys.stderr)

    if args.merge and os.path.exists(args.out):
        with open(args.out, encoding="utf-8") as fh:
            base = {f: set(v) for f, v in json.load(fh).items()}
        targets = set(envs)
        for f in list(base):  # the recomputed envs own their membership afresh
            base[f] -= targets
            if not base[f]:
                del base[f]
        for f, v in graph.items():
            base.setdefault(f, set()).update(v)
        graph = base

    out = {f: sorted(v) for f, v in sorted(graph.items())}
    with open(args.out, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(out, fh, indent=0, sort_keys=True)
        fh.write("\n")
    print(f"wrote {args.out}: {len(out)} files", file=sys.stderr)


if __name__ == "__main__":
    main()
