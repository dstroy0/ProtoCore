#!/usr/bin/env python3
r"""Fail if two ``src/`` translation units define the same file-scope symbol.

The native test envs compile an explicit list of sources, so a duplicated .cpp is invisible to
them: only a build that compiles the WHOLE tree links both copies. Arduino is exactly that build -
it compiles every .cpp under a library's ``src/`` - so a stray duplicate breaks all 152 example
sketches at link time while the host suite stays green. That is how a byte-identical copy of
``web_assets.cpp`` survived under a second name.

What it checks: a variable defined at file scope in more than one .cpp **and** given external
linkage. The second half is what makes this precise rather than noisy: in C++ a file-scope ``const``
has INTERNAL linkage by default, so four codecs may each define their own ``const DIST_BASE[]``
without ever colliding. It becomes external only when a header declares it ``extern`` - which is
exactly the shape of the case this exists for, since ``web_assets.h`` declares
``extern const char PC_DASHBOARD_PAGE[]`` and two .cpp files then defined it.

So a symbol is reported only when it is both defined in several files and declared ``extern`` in a
header, or defined non-``const`` (externally linked with no declaration needed).

Usage::

    python -m ci_tooling.check.check_duplicate_symbols          # scan src/

Exit status is 1 if any symbol is defined twice, else 0.
"""

import collections
import pathlib
import re
import sys

SRC = pathlib.Path("src")

# A file-scope definition of an array or scalar with external linkage. Anchored at column 0, which
# is what makes it file scope: this codebase indents every function body, so a match at the margin
# cannot be a local. `extern` is a declaration and `static` is internal linkage - neither collides.
_DEF = re.compile(
    r"^(?!extern\b|static\b|typedef\b|struct\b|class\b|union\b|enum\b|namespace\b|template\b|return\b)"
    r"(?:const\s+|volatile\s+|unsigned\s+|signed\s+)*"
    r"[A-Za-z_]\w*(?:\s*::\s*\w+)*\s*\**\s*"
    r"([A-Za-z_]\w*)\s*(?:\[[^\]]*\])*\s*=",
    re.M,
)

# Comments and string literals are blanked first so a symbol merely named in prose is not a match.
_STRIP = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.DOTALL)


def _blank(match):
    return re.sub(r"[^\n]", " ", match.group(0))


_EXTERN_DECL = re.compile(
    r"^\s*extern\s+(?:const\s+|volatile\s+|unsigned\s+|signed\s+)*"
    r"[A-Za-z_]\w*(?:\s*::\s*\w+)*\s*\**\s*([A-Za-z_]\w*)\s*(?:\[[^\]]*\])*\s*;",
    re.M,
)


def externs(root=SRC):
    """Names some header declares `extern` - the ones that carry external linkage into a .cpp."""
    names = set()
    for path in sorted(root.rglob("*.h")):
        clean = _STRIP.sub(_blank, path.read_text(encoding="utf-8", errors="replace"))
        names.update(m.group(1) for m in _EXTERN_DECL.finditer(clean))
    return names


def scan(root=SRC):
    """Return {symbol: [(path, line, is_const), ...]} for every file-scope definition."""
    seen = collections.defaultdict(list)
    for path in sorted(p for p in root.rglob("*") if p.suffix in (".c", ".cpp")):
        clean = _STRIP.sub(_blank, path.read_text(encoding="utf-8", errors="replace"))
        for m in _DEF.finditer(clean):
            line_no = clean.count("\n", 0, m.start()) + 1
            is_const = m.group(0).lstrip().startswith("const")
            seen[m.group(1)].append((str(path).replace("\\", "/"), line_no, is_const))
    return seen


def main():
    declared = externs()
    dupes = {}
    for name, sites in scan().items():
        if len({s[0] for s in sites}) < 2:
            continue
        # A const definition is internal linkage unless a header declared it extern.
        if all(s[2] for s in sites) and name not in declared:
            continue
        dupes[name] = [(s[0], s[1]) for s in sites]
    if dupes:
        print("check_duplicate_symbols: the same symbol is defined in more than one src/ file.", file=sys.stderr)
        print("A whole-tree build (Arduino compiles every .cpp under src/) fails to link on these;", file=sys.stderr)
        print("the native envs cannot see it, because they compile an explicit source list.", file=sys.stderr)
        for name, sites in sorted(dupes.items()):
            print(f"  {name}:", file=sys.stderr)
            for path, line_no in sites:
                print(f"      {path}:{line_no}", file=sys.stderr)
        print(f"check_duplicate_symbols: {len(dupes)} duplicated symbol(s).", file=sys.stderr)
        return 1
    print("check_duplicate_symbols: OK - every file-scope definition in src/ is unique.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
