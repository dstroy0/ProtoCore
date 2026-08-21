#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Write one CMakeLists.txt per module directory, each declaring the target that directory holds.

A module is a directory: one .c, the .h beside it, and the CMakeLists.txt that states them. A parent
directory states only the descent into its children, so adding or removing a module is a change to
one directory rather than to a file listing the whole tree. What a module depends on is not stated anywhere
by hand - it is what its own sources include, resolved back to whichever module owns that header. So
the graph is read out of the tree rather than maintained next to it.

Two things fall out of having targets instead of a flat source list:

  * include paths and transitive dependencies propagate. A consumer links a module and gets that
    module's headers and everything it in turn needs, so no header has to include another header on
    a consumer's behalf.
  * a disabled feature is a target that is never added, instead of a file that compiles to nothing.
    The `#if PROTOCORE_ENABLE_X` wrapped around a whole file is the build's job; this puts it there.

    python tools/harness.py build modules            # write every CMakeLists.txt under src/
    python tools/harness.py build modules --check    # fail if any is stale, write nothing
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
    """src/crypto/hash/sha256/sha256.c -> pc_crypto_hash_sha256.

    A module is a directory named after itself, so the last two path parts repeat; the repetition is
    dropped rather than carried into every target name and every dependency that states it.
    """
    parts = rel(csrc, SRC)[:-2].split("/")
    if len(parts) > 1 and parts[-1] == parts[-2]:
        parts = parts[:-1]
    return "pc_" + re.sub(r"[^0-9A-Za-z]+", "_", "/".join(parts))


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


ROOT_BODY = """# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

"""


DIR_HEADER = """# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# GENERATED by tools/ci_tooling/build/gen_modules.py. Do not edit.
# Regenerate with:  python tools/harness.py build modules
"""


def module_block(m, name, pad):
    """The one call that declares this module."""
    out = []
    if m["c"] is None:
        out.append("%sprotocore_header_module(%s" % (pad, name))
        if m["public"]:
            out.append(" DEPS %s" % " ".join(m["public"]))
        out.append(")\n")
        return "".join(out)
    out.append("%sprotocore_module(%s\n" % (pad, name))
    out.append('%s  SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/%s"\n' % (pad, os.path.basename(m["c"])))
    if m["public"]:
        out.append("%s  PUBLIC\n" % pad)
        for d in m["public"]:
            out.append("%s    %s\n" % (pad, d))
    if m["private"]:
        out.append("%s  PRIVATE\n" % pad)
        for d in m["private"]:
            out.append("%s    %s\n" % (pad, d))
    out.append("%s)\n" % pad)
    return "".join(out)


def render_tree(mods):
    """One CMakeLists.txt per directory: its own modules, then a descent into each child that has any.

    A module is a directory now, so its build statement lives in it. A parent states only the descent,
    which is what makes adding or removing a module a change to one directory instead of to a file
    listing the whole tree.
    """
    by_dir = {}
    for name in sorted(mods):
        by_dir.setdefault(mods[name]["dir"], []).append(name)

    # every directory that has to carry a file: one with modules, and every parent up to src/
    needed = set()
    for d in by_dir:
        cur = d
        while True:
            needed.add(cur)
            if cur == "src":
                break
            cur = "/".join(cur.split("/")[:-1]) or "src"

    kids = {d: sorted(c for c in needed if c != d and "/".join(c.split("/")[:-1]) == d) for d in needed}

    files = {}
    for d in sorted(needed):
        # src/ carries the shared declarations, so it brings its own header; every other directory
        # states only its own modules and the descent into its children.
        out = [ROOT_BODY] if d == "src" else [DIR_HEADER, chr(10)]
        for c in kids[d]:
            out.append("add_subdirectory(%s)\n" % c.split("/")[-1])
        if kids[d] and by_dir.get(d):
            out.append("\n")
        names = by_dir.get(d, [])
        by_gate = {}
        for name in names:
            by_gate.setdefault(mods[name]["gate"], []).append(name)
        for gate in sorted(by_gate, key=lambda g: (g is not None, g or "")):
            if gate:
                out.append("\nif(%s)\n" % gate)
            pad = "  " if gate else ""
            for name in by_gate[gate]:
                out.append(module_block(mods[name], name, pad))
            if gate:
                out.append("endif()\n")
        files[os.path.join(ROOT, d, "CMakeLists.txt")] = "".join(out)
    return files


def main():
    ap = argparse.ArgumentParser(
        prog="gen_modules", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--check", action="store_true", help="fail if any CMakeLists.txt is stale; write nothing")
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

    return audit(mods, unowned, strict=a.check)


DECL = re.compile(r"protocore_add_module\(\s*(?P<path>\S+)(?P<body>.*?)\n\s*\)", re.S)
DECL_KEYWORDS = ("SOURCES", "DEPS", "PRIVATE_DEPS", "GATE", "HEADER_ONLY")


def declared():
    """What each module's own CMakeLists.txt says: path -> {gate, deps}."""
    out = {}
    for dirpath, _dn, files in os.walk(SRC):
        if "CMakeLists.txt" not in files:
            continue
        text = read(os.path.join(dirpath, "CMakeLists.txt"))
        for m in DECL.finditer(text):
            body = m.group("body")
            g = re.search(r"GATE\s+(\S+)", body)
            deps = []
            for key in ("DEPS", "PRIVATE_DEPS"):
                d = re.search(r"\b%s\b((?:\s+\w[\w/]*)+)" % key, body)
                if not d:
                    continue
                for tok in d.group(1).split():
                    if tok in DECL_KEYWORDS:
                        break
                    deps.append(tok)
            out[m.group("path")] = {"gate": g.group(1) if g else "", "deps": sorted(set(deps))}
    return out


def audit(mods, unowned, strict):
    """Compare what each module declares against what its sources actually do.

    The declaration is the contract the build reads. The includes are what the compiler acts on. A
    module that reaches into another without declaring it links today only because something else
    pulled that dependency in, and stops linking the moment that changes.
    """
    decl = declared()
    # discover() keys modules by target name; the declarations key them by path. Both derive from
    # the same .c, so map through it rather than trying to invert one name into the other.
    by_path = {}
    for name, m in mods.items():
        # A header-only module has no .c; its identity comes from the header instead. rel() is
        # already repo-relative here, so strip the src/ prefix rather than re-relativising.
        f = m["c"] or m["h"]
        p = (f[len("src/") :] if f.startswith("src/") else f)[:-2]
        parts = p.split("/")
        if len(parts) > 1 and parts[-1] == parts[-2]:
            parts = parts[:-1]
        by_path["/".join(parts)] = (name, m)

    missing_decl = sorted(set(by_path) - set(decl))
    extra_decl = sorted(set(decl) - set(by_path))
    bad_gate, undeclared, unused = [], [], []

    for path, (name, m) in sorted(by_path.items()):
        d = decl.get(path)
        if d is None:
            continue
        if m["gate"] and not d["gate"]:
            bad_gate.append((path, "source gates on %s, declaration states none" % m["gate"]))
        elif d["gate"] and m["gate"] and not d["gate"].startswith(m["gate"]):
            bad_gate.append((path, "declares %s, source gates on %s" % (d["gate"], m["gate"])))
        # discover() names dependencies by target; map each back to the path the declaration uses.
        inferred = set()
        for t in m["deps"]:
            if t in mods:
                ft = mods[t]["c"] or mods[t]["h"]
                q = (ft[len("src/") :] if ft.startswith("src/") else ft)[:-2].split("/")
                if len(q) > 1 and q[-1] == q[-2]:
                    q = q[:-1]
                inferred.add("/".join(q))
        gap = sorted(inferred - set(d["deps"]))
        stale = sorted(set(d["deps"]) - inferred)
        if gap:
            undeclared.append((path, gap))
        if stale:
            unused.append((path, stale))

    print("modules declared            : %d" % len(decl))
    print("  no declaration            : %d" % len(missing_decl))
    print("  declared, no module       : %d" % len(extra_decl))
    print("  gate disagrees with source: %d" % len(bad_gate))
    print("  includes an undeclared dep: %d" % len(undeclared))
    print("  declares an unused dep    : %d" % len(unused))
    if unowned:
        print("  headers no module owns    : %d (--unowned to list)" % len(unowned))

    for label, rows in (
        ("no declaration", [(p, "") for p in missing_decl]),
        ("declared but no module", [(p, "") for p in extra_decl]),
        ("gate", bad_gate),
        ("undeclared dependency", undeclared),
    ):
        for p, detail in rows[:10]:
            print("   %-22s %-46s %s" % (label, p, detail))

    hard = len(missing_decl) + len(extra_decl) + len(bad_gate) + len(undeclared)
    if strict and hard:
        print("\n%d declaration(s) disagree with the sources" % hard, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
