#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
include_footprint.py - map the std/Arduino header dependency surface of src/.

Grabs every ``#include`` across ``src/``, works out which standard-library /
Arduino symbols each file actually references, and reports the resulting
dependency footprint: per file (what each file needs and what it declares) and
in aggregate (which headers are pulled in, by how many files). It also flags
include-what-you-use gaps - a symbol used with none of its providing headers
reachable, or a system header included but unused here - so the header surface
stays honest now that the std includes live at their natural per-file location
(no umbrella header).

Symbol usage is matched against a table of the specific symbols this codebase
uses from each portable header. A used symbol counts as satisfied if ANY header
that can provide it is reachable, either included directly or transitively
through the file's own project headers (one project-include graph, cycle-safe).

Pure stdlib, no external deps. Run from anywhere:

    python tools/include_footprint.py              # aggregate + IWYU issues
    python tools/include_footprint.py --per-file   # full per-file breakdown
    python tools/include_footprint.py --issues     # only files with IWYU gaps
    python tools/include_footprint.py --json        # machine-readable dump

Exit code is 1 when --issues (or --check) finds any missing include, else 0.
"""

import argparse
import json
import re
import sys
from pathlib import Path

# Portable headers and the specific symbols this library uses from each. A
# symbol may be provided by several headers (size_t lives in stddef.h but also
# string.h / stdio.h / time.h); the first header listed is the canonical/primary
# one reported in a file's footprint, the rest are accepted as satisfying it.
SYMBOL_PROVIDERS = [
    # (regex matching a use of the symbol, [providers, primary first]).
    # Types match as bare words; functions require a following call "(" and a
    # non-member, non-identifier prefix, so a struct field named `exp`, an AES
    # "round" counter, or `Serial.printf` do not get counted as libc calls.
    (r"\b(?:u?int(?:8|16|32|64)_t|u?int_(?:least|fast)(?:8|16|32|64)_t|u?intptr_t|u?intmax_t)\b", ["stdint.h"]),
    (r"\bsize_t\b", ["stddef.h", "string.h", "stdio.h", "time.h"]),
    (r"\bptrdiff_t\b", ["stddef.h"]),
    (r"\bNULL\b", ["stddef.h", "string.h", "stdio.h", "time.h"]),
    (
        r"(?<![.\w])(?:memcpy|memmove|memset|memcmp|memchr|strlen|strnlen|strcmp|strncmp"
        r"|strcasecmp|strncasecmp|strchr|strrchr|strstr|strncpy|strcpy|strncat"
        r"|strcat|strtok|strdup)\s*\(",
        ["string.h"],
    ),
    (r"(?<![.\w])(?:snprintf|vsnprintf|sscanf|vsscanf|sprintf|printf|vprintf|fprintf|puts)\s*\(", ["stdio.h"]),
    (r"\bva_list\b|(?<![.\w])(?:va_start|va_end|va_arg|va_copy)\s*\(", ["stdarg.h"]),
    (
        r"(?<![.\w])(?:sqrtf|sqrt|powf|pow|fabsf|fabs|roundf|round|floorf|floor|ceilf|ceil"
        r"|expf|exp|logf|log|log10f|sinf|cosf|tanf|atan2f|fmodf|fmaxf|fminf)\s*\(",
        ["math.h"],
    ),
    (r"(?<![.\w])assert\s*\(", ["assert.h"]),
    (r"\btime_t\b|\bstruct\s+tm\b" r"|(?<![.\w])(?:gmtime_r|localtime_r|strftime|mktime)\s*\(", ["time.h"]),
    # Arduino.h - guarded by ARDUINO in the sources; still part of the footprint.
    (
        r"\bSerial\b|\bString\b|(?<![.\w])(?:millis|micros|delayMicroseconds|delay"
        r"|pinMode|digitalWrite|digitalRead|analogRead|analogWrite)\s*\(",
        ["Arduino.h"],
    ),
]
SYMBOL_PROVIDERS = [(re.compile(pat), heads) for pat, heads in SYMBOL_PROVIDERS]

# Every header this tool reasons about (used to classify "unused system include"
# vs. a header we simply do not model, e.g. a layer header like lwip/tcp.h).
KNOWN_STD_HEADERS = {
    "stddef.h",
    "stdint.h",
    "string.h",
    "stdio.h",
    "stdarg.h",
    "math.h",
    "assert.h",
    "time.h",
    "Arduino.h",
}

# Platform umbrella headers that transitively pull in the C std headers below.
# Both the ESP32 Arduino core and the native test mock (test/mocks/Arduino.h)
# provide these, so a file that reaches Arduino.h has them without a direct
# include. Modeled so a symbol satisfied only via Arduino.h is not a false
# "missing include" (it still shows up as an unused direct std include if the
# file over-declares one).
PLATFORM_UMBRELLAS = {
    "Arduino.h": {"stddef.h", "stdint.h", "string.h", "stdio.h"},
}

SRC_EXTS = {".h", ".hpp", ".c", ".cpp", ".cc", ".ino"}

INCLUDE_RE = re.compile(r'^\s*#\s*include\s+(?:"([^"]+)"|<([^>]+)>)')
# Strip // and /* */ comments and "..." / '...' literals so a symbol mentioned in
# prose or a string does not count as a real dependency.
STRIP_RE = re.compile(
    r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
    re.DOTALL,
)


def strip_noise(code):
    return STRIP_RE.sub(" ", code)


def parse_file(path):
    """Return (system_includes, project_includes, symbol_uses) for one file.

    symbol_uses maps a used symbol's provider-list (as a tuple) to the set of
    concrete tokens seen, so we can report what drove each header requirement.
    """
    raw = path.read_text(encoding="utf-8", errors="replace")
    system_incs = []
    project_incs = []
    for line in raw.splitlines():
        m = INCLUDE_RE.match(line)
        if not m:
            continue
        if m.group(1) is not None:
            project_incs.append(m.group(1))
        else:
            system_incs.append(m.group(2))

    body = strip_noise(raw)
    uses = {}  # header (primary) -> set of matched tokens
    for rx, providers in SYMBOL_PROVIDERS:
        found = rx.findall(body)
        if found:
            # normalize match tuples/strings to a flat token set
            toks = set()
            for f in found:
                toks.add(f if isinstance(f, str) else next((x for x in f if x), ""))
            uses[providers[0]] = (tuple(providers), uses.get(providers[0], (tuple(providers), set()))[1] | toks)
    return system_incs, project_incs, uses


def resolve_project_include(inc, including_file, src_root):
    """Map a "..." include to a real file: try relative to the includer, then src/."""
    for base in (including_file.parent, src_root):
        cand = base / inc
        if cand.is_file():
            return cand.resolve()
    return None


def build_index(src_root):
    files = sorted(p for p in src_root.rglob("*") if p.suffix in SRC_EXTS and p.is_file())
    index = {}
    for p in files:
        sysi, proji, uses = parse_file(p)
        index[p.resolve()] = {
            "path": p,
            "system": sysi,
            "project": proji,
            "uses": uses,  # primary_header -> (providers_tuple, tokens)
        }
    return index


def reachable_system_headers(start, index, src_root):
    """System headers available to `start` directly or via its project includes."""
    seen = set()
    stack = [start]
    avail = set()
    while stack:
        cur = stack.pop()
        if cur in seen:
            continue
        seen.add(cur)
        info = index.get(cur)
        if not info:
            continue
        avail.update(info["system"])
        for inc in info["project"]:
            tgt = resolve_project_include(inc, info["path"], src_root)
            if tgt and tgt not in seen:
                stack.append(tgt)
    for umbrella, provided in PLATFORM_UMBRELLAS.items():
        if umbrella in avail:
            avail |= provided
    return avail


def analyze(index, src_root):
    report = {}
    for key, info in index.items():
        avail = reachable_system_headers(key, index, src_root)
        own_system = set(info["system"])
        missing = []  # symbol-driven header not reachable at all
        for primary, (providers, tokens) in sorted(info["uses"].items()):
            if not any(h in avail for h in providers):
                missing.append(
                    {
                        "header": primary,
                        "alt": [h for h in providers if h != primary],
                        "tokens": sorted(tokens),
                    }
                )
        # A directly-included std header whose symbols this file never uses.
        used_primaries = set(info["uses"].keys())
        used_any = set()
        for primary, (providers, _t) in info["uses"].items():
            used_any.update(providers)
        unused = sorted(h for h in own_system if h in KNOWN_STD_HEADERS and h not in used_any)
        report[key] = {
            "path": str(info["path"]).replace("\\", "/"),
            "footprint": sorted(used_primaries),
            "system_includes": sorted(own_system),
            "project_includes": list(info["project"]),
            "missing": missing,
            "unused_system": unused,
        }
    return report


def rel(path_str, src_root):
    try:
        return str(Path(path_str).resolve().relative_to(src_root.parent)).replace("\\", "/")
    except ValueError:
        return path_str


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", default=None, help="src/ root (default: <repo>/src)")
    ap.add_argument("--per-file", action="store_true", help="full per-file breakdown")
    ap.add_argument("--issues", action="store_true", help="list only files with IWYU gaps; exit 1 if any missing")
    ap.add_argument("--check", action="store_true", help="exit 1 if any file has a missing include (CI gate)")
    ap.add_argument("--json", action="store_true", help="machine-readable dump")
    args = ap.parse_args(argv)

    repo_root = Path(__file__).resolve().parent.parent
    src_root = Path(args.src).resolve() if args.src else (repo_root / "src")
    if not src_root.is_dir():
        print(f"src root not found: {src_root}", file=sys.stderr)
        return 2

    index = build_index(src_root)
    report = analyze(index, src_root)
    rows = [report[k] for k in sorted(report, key=lambda k: report[k]["path"])]

    if args.json:
        print(json.dumps(rows, indent=2))
        return 0

    n_missing = sum(1 for r in rows if r["missing"])
    n_unused = sum(1 for r in rows if r["unused_system"])

    # Aggregate: for each modeled header, how many files use its symbols and how
    # many include it directly.
    use_count = {}
    inc_count = {}
    for r in rows:
        for h in r["footprint"]:
            use_count[h] = use_count.get(h, 0) + 1
        for h in r["system_includes"]:
            if h in KNOWN_STD_HEADERS:
                inc_count[h] = inc_count.get(h, 0) + 1

    if args.issues or args.check:
        for r in rows:
            if not (r["missing"] or (args.issues and r["unused_system"])):
                continue
            print(rel(r["path"], src_root))
            for m in r["missing"]:
                alt = f" (or {', '.join(m['alt'])})" if m["alt"] else ""
                print(f"    MISSING  {m['header']}{alt}  <- {', '.join(m['tokens'][:8])}")
            if args.issues:
                for h in r["unused_system"]:
                    print(f"    unused   {h}")
        print(f"\n{n_missing} file(s) with a missing include, " f"{n_unused} with an unused std include.")
        return 1 if n_missing else 0

    if args.per_file:
        for r in rows:
            print(rel(r["path"], src_root))
            print(f"    footprint : {', '.join(r['footprint']) or '(none)'}")
            print(f"    includes  : {', '.join(r['system_includes']) or '(none)'}")
            for m in r["missing"]:
                print(f"    MISSING   : {m['header']}  <- {', '.join(m['tokens'][:8])}")
            for h in r["unused_system"]:
                print(f"    unused    : {h}")
        print()

    print("=== std/Arduino header footprint across src/ ===")
    print(f"{'header':<12} {'used-by':>8} {'included':>9}")
    for h in sorted(KNOWN_STD_HEADERS):
        print(f"{h:<12} {use_count.get(h, 0):>8} {inc_count.get(h, 0):>9}")
    print(f"\nfiles scanned: {len(rows)}   " f"missing-include: {n_missing}   unused-std-include: {n_unused}")
    if n_missing:
        print("run with --issues to see the missing includes.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
