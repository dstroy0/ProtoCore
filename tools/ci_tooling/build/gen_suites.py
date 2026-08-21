#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Audit the suite declarations under test/unit and test/env, or write them the first time.

A SUITE IS A DIRECTORY. One Unity test file, whatever fixtures sit beside it, and the
CMakeLists.txt that declares them - the same rule src/ follows for modules. A parent directory
states only the descent into its children, so adding a suite is a new directory and one line in its
parent, and nothing central holds a list.

    python tools/harness.py build suites            # audit; report where a declaration and the
                                                    # directory disagree
    python tools/harness.py build suites --write    # write them from the tree (the bootstrap, and
                                                    # the way to re-render after a large move)

WHY DECLARED AND NOT SCANNED. gen_cmake.py used to list whatever .c files it found in each suite
directory as it rendered. A directory it could not read yielded no sources, and a suite with no
sources is a target that is simply not written - so the file it hands back is smaller and still
looks like a good one. Its own comment records the day that took 437 targets down to 27. Declaring
the sources cannot fail that way: the file is named, and it is either there or it is not, and
protocore_add_suite() stops the configure with the suite's name when it is not.

The runner is the other half of that. Unity's RUN_TEST driver is generated, not committed, so a
fresh checkout has none. 354 suites need one; the 22 that write their own main() declare OWN_MAIN.
That fact is not derivable from a checkout - both cases look like "no unity_runner.c on disk" - so
it is stated.
"""

import argparse
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
TEST = os.path.join(ROOT, "test")
TREES = ("unit", "env")

RUNNER = "unity_runner.c"
HEADER = """# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
DESCENT = HEADER + """#
# The directories below. A new suite is a new directory and a line here - nothing central holds a
# list of suites, so there is nothing else to keep in step.

"""

DECL = re.compile(r"protocore_add_suite\(\s*(?P<path>\S+)(?P<body>.*?)\n\s*\)", re.S)
DESC = re.compile(r"^\s*add_subdirectory\(\s*([^)\s]+)\s*\)", re.M)


def rel(p, base=TEST):
    return os.path.relpath(p, base).replace("\\", "/")


def read(p):
    with io.open(p, encoding="utf-8", errors="replace", newline="") as f:
        return f.read()


def is_suite(dirpath):
    """A suite directory: named test_*, holding at least one .c."""
    if not os.path.basename(dirpath).startswith("test_"):
        return False
    return any(f.endswith(".c") for f in os.listdir(dirpath))


def discover():
    """Every suite on disk: path -> {sources, own_main}, and every directory that leads to one."""
    suites, leads = {}, set()
    for tree in TREES:
        base = os.path.join(TEST, tree)
        if not os.path.isdir(base):
            continue
        for dirpath, dirnames, files in os.walk(base):
            if is_suite(dirpath):
                # A suite is a leaf. Anything nested under one would be a suite the tree cannot
                # name, so stop the walk here rather than half-read it.
                dirnames[:] = []
                srcs = sorted(f for f in files if f.endswith(".c") and f != RUNNER)
                own = any("int main(" in read(os.path.join(dirpath, f)) for f in srcs)
                suites[rel(dirpath)] = {"sources": srcs, "own_main": own}
                p = os.path.dirname(rel(dirpath))
                while p:
                    leads.add(p)
                    p = os.path.dirname(p)
                leads.add(tree)
    return suites, leads


def suite_text(path, spec):
    body = [HEADER, "#\n# %s\n\n" % describe(path, spec)]
    body.append("protocore_add_suite(%s\n" % path)
    if spec["own_main"]:
        body.append("  OWN_MAIN\n")
    body.append("  SOURCES\n")
    for s in spec["sources"]:
        body.append("    %s\n" % s)
    body.append(")\n")
    return "".join(body)


def describe(path, spec):
    name = path.split("/")[-1]
    what = "%s: %s" % (name, ", ".join(spec["sources"]))
    if spec["own_main"]:
        return what + ".\n# Writes its own main(), so it takes no generated Unity runner."
    return what + ", plus the generated Unity runner."


def descent_text(children):
    return DESCENT + "".join("add_subdirectory(%s)\n" % c for c in children)


def children_of(d, suites, leads):
    """The subdirectories of @p d that hold a suite, or lead to one."""
    full = os.path.join(TEST, *d.split("/"))
    out = []
    for c in sorted(os.listdir(full)):
        if not os.path.isdir(os.path.join(full, c)):
            continue
        p = d + "/" + c
        if p in suites or p in leads:
            out.append(c)
    return out


def wanted(suites, leads):
    """path -> the CMakeLists.txt text that directory should hold."""
    out = {}
    for path, spec in suites.items():
        out[path] = suite_text(path, spec)
    for d in leads:
        kids = children_of(d, suites, leads)
        if kids:
            out[d] = descent_text(kids)
    return out


def declared():
    """What the CMakeLists.txt files under the suite trees say."""
    decls, descents = {}, {}
    for tree in TREES:
        base = os.path.join(TEST, tree)
        if not os.path.isdir(base):
            continue
        for dirpath, _dn, files in os.walk(base):
            if "CMakeLists.txt" not in files:
                continue
            text = read(os.path.join(dirpath, "CMakeLists.txt"))
            for m in DECL.finditer(text):
                body = m.group("body")
                s = re.search(r"\bSOURCES\b((?:\s+[\w./-]+)+)", body)
                srcs = [t for t in (s.group(1).split() if s else []) if t != "OWN_MAIN"]
                decls[m.group("path")] = {
                    "sources": sorted(srcs),
                    "own_main": "OWN_MAIN" in body,
                    "dir": rel(dirpath),
                }
            descents[rel(dirpath)] = DESC.findall(text)
    return decls, descents


def audit(strict):
    suites, leads = discover()
    decls, descents = declared()

    missing = sorted(set(suites) - set(decls))
    extra = sorted(set(decls) - set(suites))
    bad_dir, bad_src, bad_main, bad_descent = [], [], [], []

    # Over every declaration, not only the ones that match a directory: the path IS the target name
    # and the directory is where the sources are, so a declaration naming somewhere else builds a
    # target under a name that points at nothing. Checked here rather than inside the intersection
    # below, where a mis-pathed declaration can never appear by definition.
    for path, said in sorted(decls.items()):
        if said["dir"] != path:
            bad_dir.append((path, "is declared in %s" % said["dir"]))

    for path in sorted(set(suites) & set(decls)):
        on_disk, said = suites[path], decls[path]
        if said["sources"] != on_disk["sources"]:
            bad_src.append((path, "declares %s, the directory holds %s" % (said["sources"], on_disk["sources"])))
        if said["own_main"] != on_disk["own_main"]:
            bad_main.append(
                (
                    path,
                    (
                        "declares OWN_MAIN and no source defines main()"
                        if said["own_main"]
                        else "defines main() and does not declare OWN_MAIN"
                    ),
                )
            )

    # A directory that leads to a suite and does not descend into it takes that whole subtree out
    # of the build, and nothing else reports it.
    for d in sorted(leads):
        kids = set(children_of(d, suites, leads))
        if not kids:
            continue
        got = set(descents.get(d, []))
        gap = sorted(kids - got)
        if d not in descents:
            bad_descent.append((d, "has no CMakeLists.txt; %d child directories are unreachable" % len(kids)))
        elif gap:
            bad_descent.append((d, "does not descend into %s" % ", ".join(gap)))

    print("suites on disk              : %d" % len(suites))
    print("  no declaration            : %d" % len(missing))
    print("  declared, no directory    : %d" % len(extra))
    print("  path is not its directory : %d" % len(bad_dir))
    print("  SOURCES disagree with dir : %d" % len(bad_src))
    print("  OWN_MAIN disagrees        : %d" % len(bad_main))
    print("  descent missing           : %d" % len(bad_descent))

    for label, rows in (
        ("no declaration", [(p, "") for p in missing]),
        ("declared, no directory", [(p, "") for p in extra]),
        ("path", bad_dir),
        ("sources", bad_src),
        ("own_main", bad_main),
        ("descent", bad_descent),
    ):
        for p, detail in rows[:10]:
            print("   %-22s %-46s %s" % (label, p, detail))

    hard = len(missing) + len(extra) + len(bad_dir) + len(bad_src) + len(bad_main) + len(bad_descent)
    if strict and hard:
        print("\n%d suite declaration(s) disagree with the tree" % hard, file=sys.stderr)
        return 1
    return 0


def write():
    suites, leads = discover()
    want = wanted(suites, leads)
    n = 0
    for path in sorted(want):
        p = os.path.join(TEST, *path.split("/"), "CMakeLists.txt")
        cur = read(p) if os.path.isfile(p) else None
        if cur == want[path]:
            continue
        with io.open(p, "w", encoding="utf-8", newline="\n") as f:
            f.write(want[path])
        n += 1
    print(
        "wrote %d of %d CMakeLists.txt under test/unit and test/env (%d suites, %d descents)"
        % (n, len(want), len(suites), len(want) - len(suites))
    )
    return 0


def main():
    ap = argparse.ArgumentParser(
        prog="gen_suites", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--write", action="store_true", help="write the declarations from the tree")
    ap.add_argument("--check", action="store_true", help="exit non-zero on any disagreement")
    a = ap.parse_args()
    return write() if a.write else audit(strict=a.check)


if __name__ == "__main__":
    sys.exit(main())
