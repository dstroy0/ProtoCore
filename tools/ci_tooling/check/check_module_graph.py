#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The module dependency graph is acyclic, and src/CMakeLists.txt states it.

Two things, because they fail together and are fixed together:

  * no cycle among the HEADERS. A header that includes another header hands that dependency to every
    consumer, so a loop between two of them is a loop every consumer inherits, and CMake refuses to
    generate OBJECT libraries over one. Only header edges count: two .c files calling each other's
    published handle is ordinary C and closes nothing, because neither header names the other.
  * src/CMakeLists.txt is current. It is generated from the tree, so a module added or an include
    changed without regenerating leaves the build naming sources that no longer describe it.

    python tools/harness.py ci check module_graph
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "ci_tooling", "build"))

import gen_modules  # noqa: E402


def main():
    mods, owner = gen_modules.discover()
    gen_modules.scan(mods)
    gen_modules.resolve(mods, owner)
    cyc = gen_modules.cycles(mods)

    rc = 0
    if cyc:
        print("check_module_graph: %d dependency cycle(s) among headers:" % len(cyc))
        for c in cyc:
            print("  cycle of %d:" % len(c))
            for n in c:
                inside = [d for d in mods[n]["public"] if d in c]
                print("     %-50s -> %s" % (n, ", ".join(inside)))
        print(
            "\nA header must not include a header that includes it back. Move the shared declaration "
            "into whichever module owns it - the owner publishes the seam, the other implements it."
        )
        rc = 1

    files = gen_modules.render_tree(mods)
    stale = [p for p, t in files.items() if (gen_modules.read(p) if os.path.isfile(p) else "") != t]
    if stale:
        print("check_module_graph: %d CMakeLists.txt stale - run `python tools/harness.py build modules`" % len(stale))
        for p in sorted(stale)[:8]:
            print("   " + gen_modules.rel(p))
        rc = 1

    if rc == 0:
        print("check_module_graph: OK - %d modules, no cycles, %d CMakeLists.txt current" % (len(mods), len(files)))
    return rc


if __name__ == "__main__":
    sys.exit(main())
