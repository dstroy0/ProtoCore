#!/usr/bin/env python3
r"""Fail if a call hands an integer literal to a parameter that is a pimpl context pointer.

Every namespace call in the tree takes ``struct <X>Internal *``. A call site left over from before
the conversion still writes the old scalar argument - ``protocore_ssh_channel_init(0)``. C converts
the literal ``0`` to a null pointer constant, so the call is well formed, the compiler emits no
diagnostic at any warning level, and the body dereferences ``ctx->ns`` on its first line. The
result is a segfault the moment that code runs, with nothing at build time to point at it.

That is not hypothetical: it is how ``native_ssh_forward`` came to abort with no Unity output at
all, ``rip`` sitting four instructions into ``protocore_ssh_channel_init``.

What it checks: for every non-``static`` function in ``src/`` whose first parameter is
``struct <Something>Internal *``, every call to that name whose first argument is an integer
literal or ``NULL``. A ``&something``, a ``<Ns>.internal`` or a forwarded ``ctx`` is a real pointer
and is not reported - only a literal is provably wrong.

Usage::

    python -m tools.ci_tooling.check.check_null_ctx            # scan src/ and test/
    python -m tools.ci_tooling.check.check_null_ctx src        # scan one tree
    python -m tools.ci_tooling.check.check_null_ctx --baseline # record today's count as the floor

Exit status is 1 when a call site is above the recorded floor, else 0.
"""

import json
import os
import re
import sys

from tools.ci_tooling.lib import doc_region as dr

_ROOT = dr.repo_root(__file__)
BASELINE = os.path.join(_ROOT, "tools", "ci_tooling", "check", "null_ctx_baseline.json")

# A non-static definition or declaration whose first parameter is a pimpl context pointer.
DEF = re.compile(r"^(?!static)[A-Za-z_][\w \*]*?\b(\w+)\s*\(\s*struct\s+(\w+Internal)\s*\*", re.M)
# An integer literal, decimal or hex, with an optional unsigned suffix and optional parentheses -
# or NULL, which the old signature took as "no callback" and the new one dereferences just the same.
LITERAL = re.compile(r"\(?\s*(?:0[xX][0-9a-fA-F]+|\d+|NULL|nullptr)\s*[uU]?\)?")

EXTS = {".c", ".h", ".cpp", ".hpp"}


def _rel(path):
    return os.path.relpath(path, _ROOT).replace("\\", "/")


def _read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def _walk(top):
    for d, _dirs, files in os.walk(top):
        for f in files:
            if os.path.splitext(f)[1] in EXTS:
                yield os.path.join(d, f)


def context_functions():
    """{name: (file, internal struct)} for every function taking a pimpl context pointer."""
    out = {}
    for p in _walk(os.path.join(_ROOT, "src")):
        for m in DEF.finditer(_read(p)):
            out[m.group(1)] = (_rel(p), m.group(2))
    return out


def findings(trees):
    fns = context_functions()
    out = []
    for tree in trees:
        top = os.path.join(_ROOT, tree)
        if not os.path.isdir(top):
            continue
        for p in _walk(top):
            rel = _rel(p)
            for i, line in enumerate(_read(p).split("\n"), 1):
                for name, (where, internal) in fns.items():
                    for m in re.finditer(r"\b" + re.escape(name) + r"\s*\(([^();]*)\)", line):
                        arg = m.group(1).split(",")[0].strip()
                        if not LITERAL.fullmatch(arg):
                            continue
                        out.append((rel, i, name, arg, where, internal))
    return fns, out


def load_floor():
    if not os.path.isfile(BASELINE):
        return 0
    with open(BASELINE, "r", encoding="utf-8") as f:
        return int(json.load(f).get("count", 0))


def main(argv):
    save = "--baseline" in argv
    trees = [a for a in argv if not a.startswith("-")] or ["src", "test"]
    fns, hits = findings(trees)
    print("%d function(s) take a pimpl context pointer" % len(fns))
    for rel, i, name, arg, where, internal in hits:
        print("%s:%d  %s(%s)   [%s takes struct %s *]" % (rel, i, name, arg, where, internal))
    print("\n%d call site(s) pass an integer literal to a context parameter" % len(hits))

    if save:
        with open(BASELINE, "w", encoding="utf-8", newline="\n") as f:
            json.dump({"count": len(hits)}, f, indent=2)
            f.write("\n")
        print("recorded %d as the floor" % len(hits))
        return 0

    floor = load_floor()
    if len(hits) > floor:
        print("ABOVE THE FLOOR of %d - a new one was added" % floor)
        return 1
    if len(hits) < floor:
        print("below the floor of %d - re-record with --baseline" % floor)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
