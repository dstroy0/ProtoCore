#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""gen_dep_graph.py - build test/dep_graph.json from the CMake build's own dependency records.

Every env target in build/native records, per translation unit, the exact set of headers the
compiler read. Ninja keeps it in .ninja_deps and prints it with `ninja -t deps`, so the graph is
already there once the tree has been built:

    CMakeFiles/native_tcp.dir/.../tcp.c.obj: #deps 74, deps mtime ... (VALID)
        C:/.../src/network_drivers/transport/tcp/tcp.c
        C:/.../src/protocore_config.h
        ...

The object path names the env, the indented lines are its dependencies, and inverting the whole dump
gives:

    { "src/services/iot/coap/coap.h": ["native_coap", "native_coap_observe"], ... }

`harness.py env select` consults this to map a changed HEADER to exactly the envs whose include
closure contains it, instead of the conservative "any header -> FULL". A file absent from the map -
a brand-new one, or one no env compiles - still falls back to FULL.

WHY NOT PlatformIO. This used to run `pio run -t compiledb -e <env>` once per env - about 420
processes - and then `g++ -MM` once per translation unit inside each, to recover dependencies the
build had already worked out and written down. One `ninja -t deps` reads the same data, needs no
second toolchain, and cannot disagree with what was actually compiled.

WHAT IT NEEDS. A current build/native. Dependencies exist for objects that have been built, so an
env that has never been compiled is absent rather than empty, and the tool says which those are
instead of writing a graph with holes in it.

Usage:
    tools/ci_tooling/generate/gen_dep_graph.py                     # every built env -> test/dep_graph.json
    tools/ci_tooling/generate/gen_dep_graph.py --envs native_coap  # a subset (for validation)
    tools/ci_tooling/generate/gen_dep_graph.py --build-dir build/native --out test/dep_graph.json
"""

import argparse
import json
import os
import re
import subprocess
import sys

from tools.ci_tooling.lib import doc_region as dr

ROOT = dr.repo_root(__file__)
MATRIX = os.path.join(ROOT, "test", "test_matrix.json")
# Tooling-only envs that never carry a test suite (mirror select_envs.NEVER_SELECT + codeql).
SKIP_ENVS = {"native_codeql"}

# `CMakeFiles/<env>.dir/<anything>.obj: #deps N, ...` - the header line of one object's dep record.
OBJ = re.compile(r"^CMakeFiles[\\/](?P<env>[^\\/]+)\.dir[\\/].*?:\s+#deps\s+\d+", re.I)


def base_env(target):
    """The env a CMake target belongs to.

    An env with more than one test suite becomes one target per suite - native_ssh builds as
    native_ssh__transport_test_transport and a dozen siblings - but the graph is consumed by
    `harness.py env select`, which hands what it finds to `harness.py run`, and that takes the env.
    Emitting the target names would name things no one can run.
    """
    return target.split("__", 1)[0]


def declared_envs():
    """Every native env the matrix defines, so a missing one can be named.

    From test_matrix.json, not platformio.ini: the ini no longer renders the native envs - CMake
    builds them - so counting `[env:native_*]` headers there would find none and report that
    nothing is missing.
    """
    with open(MATRIX, encoding="utf-8") as fh:
        table = json.load(fh)
    # An entry with no suite is a stack base, extended by others and never built on its own.
    return [n for n, e in table.get("envs", {}).items() if e.get("tests") and n not in SKIP_ENVS]


def rel_dep(path):
    """Repo-relative POSIX path, or None for a toolchain header outside the repo."""
    ap = os.path.normpath(path)
    try:
        rel = os.path.relpath(ap, ROOT)
    except ValueError:
        return None
    if rel.startswith(".."):
        return None
    return rel.replace("\\", "/")


def ninja_deps(build_dir):
    """env -> the set of repo files its objects recorded as dependencies."""
    exe = os.environ.get("NINJA", "ninja")
    try:
        r = subprocess.run([exe, "-t", "deps"], cwd=build_dir, capture_output=True, text=True, check=True)
    except FileNotFoundError:
        sys.exit("ninja not found; set NINJA or put it on PATH")
    except subprocess.CalledProcessError as e:
        sys.exit("ninja -t deps failed in %s: %s" % (build_dir, (e.stderr or "").strip()[:300]))

    graph, env = {}, None
    for line in r.stdout.splitlines():
        if not line:
            continue
        if line[0].isspace():
            if env is None:
                continue
            rel = rel_dep(line.strip())
            # Sources count as well as headers: a changed .c selects its own env the same way.
            if rel and rel.endswith((".h", ".hpp", ".inc", ".c", ".cpp", ".ino")):
                graph.setdefault(env, set()).add(rel)
            continue
        m = OBJ.match(line)
        env = base_env(m.group("env")) if m else None
    return graph


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--envs", nargs="*", help="subset of envs (default: every env with recorded deps)")
    ap.add_argument("--build-dir", default=os.path.join(ROOT, "build", "native"))
    ap.add_argument("--out", default=os.path.join(ROOT, "test", "dep_graph.json"))
    ap.add_argument(
        "--merge",
        action="store_true",
        help="update only --envs in an existing --out: drop those envs everywhere, then re-add their "
        "fresh deps (keeps the committed graph current after an affected run without a full rebuild)",
    )
    args = ap.parse_args()

    if not os.path.isdir(args.build_dir):
        sys.exit("no build at %s - configure and build it first" % args.build_dir)

    per_env = ninja_deps(args.build_dir)
    if args.envs:
        per_env = {e: v for e, v in per_env.items() if e in set(args.envs)}
    else:
        for e in SKIP_ENVS:
            per_env.pop(e, None)
        # An env that has never been built records nothing. Saying so beats writing a graph that is
        # silently short of it, because a missing env reads as "nothing depends on that file".
        missing = [e for e in declared_envs() if e not in per_env]
        if missing:
            print(
                "%d env(s) have no recorded dependencies - build them or the graph is short of them: %s"
                % (len(missing), ", ".join(sorted(missing)[:8]) + (" ..." if len(missing) > 8 else "")),
                file=sys.stderr,
            )

    graph = {}
    for env, files in per_env.items():
        for f in files:
            graph.setdefault(f, set()).add(env)

    if args.merge and os.path.exists(args.out):
        with open(args.out, encoding="utf-8") as fh:
            base = {f: set(v) for f, v in json.load(fh).items()}
        targets = set(per_env)
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
    print("wrote %s: %d files over %d env(s)" % (args.out, len(out), len(per_env)), file=sys.stderr)


if __name__ == "__main__":
    main()
