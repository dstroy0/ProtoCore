#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Write src/CMakeLists.txt: one CMake target per module, with the dependencies it actually has.

A module is one .c under src/ and the .h beside it. What a module depends on is not stated anywhere
by hand - it is what its own sources include, resolved back to whichever module owns that header. So
the graph is read out of the tree rather than maintained next to it.

Two things fall out of having targets instead of a flat source list:

  * include paths and transitive dependencies propagate. A consumer links a module and gets that
    module's headers and everything it in turn needs, so no header has to include another header on
    a consumer's behalf.
  * a disabled feature is a target that is never added, instead of a file that compiles to nothing.
    The `#if PROTOCORE_ENABLE_X` wrapped around a whole file is the build's job; this puts it there.

    python tools/harness.py build modules            # write src/CMakeLists.txt
    python tools/harness.py build modules --check    # fail if stale, write nothing
    python tools/harness.py build modules --graph    # print the dependency graph, write nothing

The gate a module carries is read from its own source: a file whose body is wrapped in
`#if PROTOCORE_ENABLE_X` is emitted inside `if(PROTOCORE_ENABLE_X)`, so the flag selects the target.
"""

import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
SRC = os.path.join(ROOT, "src")
OUT = os.path.join(SRC, "CMakeLists.txt")

INC = re.compile(r'^\s*#\s*include\s+"([^"]+)"')
GATE = re.compile(r"^\s*#\s*if\s+(PROTOCORE_ENABLE_\w+)\s*$")

# Reached by everything and owned by no module: the assembly chain and the primitive types.
ENTRY = {
    "protocore_config.h",
    "config/platform/platform.h",
    "config/platform/types.h",
    "config/platform/platform_defines.h",
    "config/platform/platform_prototypes.h",
    "config/platform/platform_error.h",
    "config/platform/compiler_directives.h",
    "config/hardware_capabilities/hw_caps_en.h",
    "config/hardware_capabilities/hw_caps_en_error.h",
    "config/hardware_capabilities/hw_caps_prototypes.h",
    "config/memory_sizing/buffer_sizing.h",
    "config/features/feature_en_error.h",
    "config/features/feature_dependency_en.h",
    "derived_sizing.h",
}


def rel(p, base=ROOT):
    return os.path.relpath(p, base).replace("\\", "/")


def target_name(csrc):
    """src/crypto/hash/sha256.c -> pc_crypto_hash_sha256."""
    stem = rel(csrc, SRC)[:-2]
    return "pc_" + re.sub(r"[^0-9A-Za-z]+", "_", stem)


def read(p):
    with open(p, encoding="utf-8", errors="replace") as f:
        return f.read()


def discover():
    """Every module: its .c, the .h beside it, the headers it includes, and its enable gate."""
    mods = {}
    owner = {}  # header path (relative to src/) -> target name
    for dirpath, _dn, files in os.walk(SRC):
        for f in sorted(files):
            if not f.endswith(".c"):
                continue
            c = os.path.join(dirpath, f)
            name = target_name(c)
            h = c[:-2] + ".h"
            mods[name] = {
                "c": rel(c),
                "h": rel(h) if os.path.isfile(h) else None,
                "dir": rel(dirpath),
                "includes": set(),
                "gate": None,
            }
            if os.path.isfile(h):
                owner[rel(h, SRC)] = name

    # A header with no .c beside it is still a module - it is just header-only, and a consumer still
    # depends on it. It becomes an INTERFACE target so the dependency resolves and its include
    # directory propagates the same way a compiled module's does.
    for dirpath, _dn, files in os.walk(SRC):
        for f in sorted(files):
            if not f.endswith(".h"):
                continue
            h = os.path.join(dirpath, f)
            key = rel(h, SRC)
            if key in owner or key in ENTRY:
                continue
            name = target_name(h[:-2] + ".c")
            mods[name] = {
                "c": None,
                "h": rel(h),
                "dir": rel(dirpath),
                "includes": set(),
                "gate": None,
            }
            owner[key] = name
    return mods, owner


def scan(mods):
    """What each module includes, kept apart by which file did the including.

    A module's HEADER includes are its interface: a consumer that includes this header gets them too,
    so they propagate and they are what has to stay acyclic. A module's SOURCE includes are its
    implementation: two modules whose .c files call each other's published handle is ordinary C and
    closes no loop, because neither header names the other.
    """
    for name, m in mods.items():
        for p, key in ((m["h"], "iface"), (m["c"], "impl")):
            if not p:
                continue
            text = read(os.path.join(ROOT, p))
            for line in text.split("\n"):
                mi = INC.match(line)
                if mi:
                    m.setdefault(key, set()).add(mi.group(1))
                    m["includes"].add(mi.group(1))
                if m["gate"] is None:
                    g = GATE.match(line)
                    if g and p == m["c"]:
                        m["gate"] = g.group(1)


def resolve(mods, owner):
    """A module's dependencies, split by whether its header or only its source asked for them.

    PUBLIC is what the header includes - a consumer inherits it, and it is the graph that has to be
    acyclic. PRIVATE is what only the .c includes; it is not part of the interface, so it neither
    propagates nor counts as a cycle.
    """
    unowned = {}
    for name, m in mods.items():
        found = {}
        for key in ("iface", "impl"):
            for inc in sorted(m.get(key, ())):
                if inc in ENTRY:
                    continue
                d = owner.get(inc)
                if d is None:
                    # a header named relative to its own directory
                    local = rel(os.path.normpath(os.path.join(ROOT, m["dir"], inc)), SRC)
                    d = owner.get(local)
                if d is None:
                    unowned.setdefault(inc, set()).add(name)
                    continue
                if d == name:
                    continue
                # the header's word wins: an interface dependency stays one even if the .c repeats it
                if found.get(d) != "iface":
                    found[d] = key
        m["public"] = sorted(d for d, k in found.items() if k == "iface")
        m["private"] = sorted(d for d, k in found.items() if k == "impl")
        m["deps"] = sorted(found)
    return unowned


def cycles(mods):
    """Every dependency cycle in the graph, as a list of module names per cycle.

    A cycle is two headers that include each other, directly or through a chain. CMake refuses it
    outright for OBJECT libraries, so it is found here and named rather than surfacing as a generate
    step failure with one edge quoted.

    Tarjan's strongly connected components: a component of more than one module is a cycle.
    """
    index, low, onstack, stack, out = {}, {}, set(), [], []
    counter = [0]

    def strong(v):
        index[v] = low[v] = counter[0]
        counter[0] += 1
        stack.append(v)
        onstack.add(v)
        for w in mods[v].get("public", []):
            if w not in mods:
                continue
            if w not in index:
                strong(w)
                low[v] = min(low[v], low[w])
            elif w in onstack:
                low[v] = min(low[v], index[w])
        if low[v] == index[v]:
            comp = []
            while True:
                w = stack.pop()
                onstack.discard(w)
                comp.append(w)
                if w == v:
                    break
            if len(comp) > 1:
                out.append(sorted(comp))

    sys.setrecursionlimit(10000)
    for v in sorted(mods):
        if v not in index:
            strong(v)
    return out


HEADER = '''# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# GENERATED by tools/ci_tooling/build/gen_modules.py. Do not edit.
# Regenerate with:  python tools/harness.py build modules
#
# One target per module - a .c under src/ and the .h beside it. The dependencies are read out of what
# each module includes, resolved back to whichever module owns that header, so nothing here is
# maintained by hand.
#
# OBJECT libraries: a module contributes its objects to whatever links it, which is the same single
# link the flat source list produced, while still carrying its include directories and its
# dependencies as usage requirements. The header graph is acyclic, which is what OBJECT requires -
# `build modules --cycles` checks it, and only header edges can close a loop. Two .c files calling
# each other's published handle is ordinary C and is PRIVATE, so it is not part of that graph.

# Everything compiles against the entry point and the primitive types it assembles.
add_library(pc_config INTERFACE)
# The same set every env compiles with (tools/ci_tooling/build/gen_cmake.py BASE_INCLUDES): the host
# arm answers the platform seams in software, so a host build of the library reaches it the same way
# a suite does.
target_include_directories(pc_config INTERFACE
  "${PROTOCORE_ROOT}/test/core_setup/hal/host"
  "${PROTOCORE_ROOT}/test/support"
  "${PROTOCORE_ROOT}/src"
  "${PROTOCORE_ROOT}/include"
  "${PROTOCORE_ROOT}")

# One module. DEPS are other modules; each is PUBLIC, so a consumer that links this one also gets
# their headers and their own dependencies without naming them.
function(protocore_module name)
  cmake_parse_arguments(M "" "" "SOURCES;PUBLIC;PRIVATE" ${ARGN})
  add_library(${name} OBJECT ${M_SOURCES})
  target_link_libraries(${name} PUBLIC pc_config ${M_PUBLIC})
  if(M_PRIVATE)
    target_link_libraries(${name} PRIVATE ${M_PRIVATE})
  endif()
endfunction()

# A module with no .c beside its .h: header-only, and a consumer still depends on it. INTERFACE, so
# it contributes no objects but carries its own dependencies onward exactly as a compiled one does.
function(protocore_header_module name)
  cmake_parse_arguments(M "" "" "DEPS" ${ARGN})
  add_library(${name} INTERFACE)
  target_link_libraries(${name} INTERFACE pc_config ${M_DEPS})
endfunction()

'''


def render(mods):
    out = [HEADER]
    by_gate = {}
    for name in sorted(mods):
        by_gate.setdefault(mods[name]["gate"], []).append(name)

    for gate in sorted(by_gate, key=lambda g: (g is not None, g or "")):
        names = by_gate[gate]
        if gate:
            out.append("\n# %s\nif(%s)\n" % (gate, gate))
        pad = "  " if gate else ""
        for name in names:
            m = mods[name]
            if m["c"] is None:
                out.append("%sprotocore_header_module(%s" % (pad, name))
                if m["public"]:
                    out.append(" DEPS %s" % " ".join(m["public"]))
                out.append(")\n")
                continue
            out.append("%sprotocore_module(%s\n" % (pad, name))
            out.append('%s  SOURCES "${PROTOCORE_ROOT}/%s"\n' % (pad, m["c"]))
            if m["public"]:
                out.append("%s  PUBLIC\n" % pad)
                for d in m["public"]:
                    out.append("%s    %s\n" % (pad, d))
            if m["private"]:
                out.append("%s  PRIVATE\n" % pad)
                for d in m["private"]:
                    out.append("%s    %s\n" % (pad, d))
            out.append("%s)\n" % pad)
        if gate:
            out.append("endif()\n")
    return "".join(out)


def main():
    ap = argparse.ArgumentParser(
        prog="gen_modules", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--check", action="store_true", help="fail if src/CMakeLists.txt is stale; write nothing")
    ap.add_argument("--graph", action="store_true", help="print the dependency graph and write nothing")
    ap.add_argument("--unowned", action="store_true", help="list included headers no module owns")
    ap.add_argument("--cycles", action="store_true", help="list dependency cycles and write nothing")
    a = ap.parse_args()

    mods, owner = discover()
    scan(mods)
    unowned = resolve(mods, owner)

    if a.graph:
        for name in sorted(mods):
            m = mods[name]
            print("%s  (%s)%s" % (name, m["c"], "  [" + m["gate"] + "]" if m["gate"] else ""))
            for d in m["deps"]:
                print("      -> %s" % d)
        return 0

    if a.unowned:
        for inc in sorted(unowned):
            print("%-60s wanted by %d module(s)" % (inc, len(unowned[inc])))
        return 0

    cyc = cycles(mods)
    if a.cycles:
        if not cyc:
            print("no dependency cycles")
            return 0
        for c in cyc:
            print("cycle of %d:" % len(c))
            for n in c:
                inside = [d for d in mods[n]["deps"] if d in c]
                print("   %-52s -> %s" % (n, ", ".join(inside)))
        return 1

    text = render(mods)
    if a.check:
        cur = read(OUT) if os.path.isfile(OUT) else ""
        if cur != text:
            print("src/CMakeLists.txt is stale - run `python tools/harness.py build modules`", file=sys.stderr)
            return 1
        print("src/CMakeLists.txt is current (%d modules)" % len(mods))
        return 0

    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    gated = sum(1 for m in mods.values() if m["gate"])
    edges = sum(len(m["deps"]) for m in mods.values())
    print("wrote %s: %d modules, %d gated, %d dependency edges" % (rel(OUT), len(mods), gated, edges))
    if unowned:
        print("%d included header(s) no module owns (--unowned to list)" % len(unowned))
    return 0


if __name__ == "__main__":
    sys.exit(main())
