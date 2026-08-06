#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Enforce the mechanical half of docs/SYMBOLS.md.

SYMBOLS.md section 7 defines this script's remit exactly, so it is implemented to
that line and no further: "prefix and casing, macro scope and length, include-guard
form, file naming, and the absence of namespace / using namespace". Judgment calls -
is a name descriptive, should this enum de-prefix - are review, and this script must
not pretend to decide them.

Checks:
  1. include guard is PC_<PATH>_H derived from the file's own path (s4)
  2. include guard is <= 31 characters (s2, the C89 significant-identifier limit)
  3. no `#pragma once` (s4: not standard, and the target list includes toolchains
     where its behavior across duplicated/symlinked headers is unspecified)
  4. exported macros are PC_UPPER_SNAKE and <= 31 characters (s2)
  5. enum types are pc_snake_case (s3)
  6. no NAMED namespace and no `using namespace` (s1). Anonymous `namespace {` is
     explicitly fine - it is how file-local owner-context state is scoped, which is
     a different rule (check_owned_context.py) pulling in the same direction.
  7. source file and directory names are snake_case (s4; markdown is the documented
     exception and is not scanned here)
  8. no type/function collision after normalization - `PCConnCounters` and
     `pc_conn_counters()` collapsed onto each other during the rename and only a
     link error caught it.

BASELINE. The prefix sweep is not finished (roadmap "Now"), so a bare run reports
thousands of pre-existing violations and could never gate anything. `--baseline`
writes the current set; CI runs `--check` and fails only on violations NOT in that
file, so the count can ratchet down and never up.

Usage:
    python -m ci_tooling.check.check_symbols            # report everything
    python -m ci_tooling.check.check_symbols --summary  # counts only
    python -m ci_tooling.check.check_symbols --baseline # record current state
    python -m ci_tooling.check.check_symbols --check    # CI: fail on NEW violations
"""

import os
import re
import sys

from ci_tooling.lib import baseline as bl
from ci_tooling.lib import doc_region as dr
from ci_tooling.lib import src_symbols

ROOT = dr.repo_root(__file__)
SRC = os.path.join(ROOT, "src")
BASELINE = bl.path_for(__file__, "symbols_baseline")

MACRO_LIMIT = 31

# Foreign names that must never be "corrected": vendor parts and silicon registers whose
# spelling is fixed by a datasheet. The rename pass learned this the hard way - a blanket
# PC-prefix sweep would have renamed PCA9685 and the PCR_* register block.
FOREIGN = re.compile(r"^(PCB|PCA\d|PCF\d|PCR_|PCNT|PCIE|PC_?LCD)", re.I)


def decomment(t):
    # line-preserving: deleting a block comment shifts every reported line number
    # after it (a namespace at 108 was reported at 73). One implementation, in the lib.
    return src_symbols.blank_comments(t)


# Headers whose filename-derived guard exceeds MACRO_LIMIT. The filename form is
# authoritative WHEN IT FITS; when it does not, the guard still has to convey intent,
# so a whole word is elided rather than the name being chopped mid-word
# (PROTOCORE_PROVISIONING_SERVIC_H names nothing). Which word carries the meaning is a
# judgment call, so these are recorded, not computed. Mirrored in docs/SYMBOLS.md s4 -
# keep the two in step.
GUARD_EXCEPTIONS = {
    "ntrip_caster_listener.h": "PROTOCORE_NTRIP_LISTENER_H",  # "caster" is implied by "ntrip"
    "provisioning_service.h": "PROTOCORE_PROVISIONING_H",  # a _service header is a service
}


def guard_for(rel):
    """PROTOCORE_<FILE>_H, or the recorded exception when that exceeds MACRO_LIMIT.

    Filename-derived, not path-derived: see docs/SYMBOLS.md s4. The path form demanded
    a name that 75% of headers could not fit inside the 31-character limit, and a rule
    that cannot be satisfied gets ignored.

    The limit is what makes this a guarantee rather than a hope: past 31 characters C89
    leaves an external identifier's significance UNSPECIFIED, so a conforming toolchain
    may merge two guards and silently drop an #include.
    """
    base = os.path.basename(rel)
    if base in GUARD_EXCEPTIONS:
        return GUARD_EXCEPTIONS[base]
    stem = re.sub(r"\.h$", "", base)
    stem = re.sub(r"[^A-Za-z0-9]+", "_", stem).upper()
    # protocore.h must not guard with PROTOCORE_PROTOCORE_H; the prefix is already there
    guard = (stem + "_H") if stem.startswith("PROTOCORE") else ("PROTOCORE_" + stem + "_H")
    if len(guard) > MACRO_LIMIT:
        # No silent chop: an over-long guard needs a human-chosen elision recorded above.
        raise SystemExit(
            f"check_symbols: {rel}: guard {guard} is {len(guard)} chars, over {MACRO_LIMIT}, "
            f"and has no entry in GUARD_EXCEPTIONS. Add one that conveys intent "
            f"(elide a whole word) and mirror it in docs/SYMBOLS.md s4."
        )
    return guard


def sources():
    for base, dirs, files in os.walk(SRC):
        dirs[:] = [d for d in dirs if not d.startswith(".")]
        for f in sorted(files):
            if f.endswith((".h", ".c", ".cpp")):
                yield os.path.join(base, f)


def check():
    findings = []

    def add(kind, rel, line, msg):
        findings.append({"kind": kind, "file": rel, "line": line, "msg": msg})

    types_seen, funcs_seen, guards_seen = {}, {}, {}

    for path in sources():
        rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
        raw = open(path, encoding="utf-8", errors="replace").read()
        text = decomment(raw)
        is_header = rel.endswith(".h")

        # 7. file / directory naming
        base = os.path.basename(rel)
        if not re.fullmatch(r"[a-z0-9_]+\.(h|c|cpp)", base):
            add("file-name", rel, 1, f"file name is not snake_case: {base}")
        for part in os.path.dirname(rel).split("/"):
            if part and not re.fullmatch(r"[a-z0-9_]+", part):
                add("dir-name", rel, 1, f"directory is not snake_case: {part}")

        # 3. #pragma once  (no capture group here, so offset from the whole match;
        # m.start(1) would raise IndexError the first time this ever fired)
        for m in re.finditer(r"^[ \t]*#[ \t]*pragma[ \t]+once", text, re.M):
            add(
                "pragma-once",
                rel,
                text[: m.start()].count("\n") + 1,
                "#pragma once is not used; write the PROTOCORE_<FILE>_H guard",
            )

        # 1 + 2. include guard
        if is_header:
            g = re.search(r"^\s*#\s*ifndef\s+([A-Za-z_]\w*)\s*\n\s*#\s*define\s+\1\b", text, re.M)
            want = guard_for(rel)
            # Truncation can merge two guards, so uniqueness is checked on the FINAL
            # name rather than on the filename it was derived from.
            if want in guards_seen and guards_seen[want] != rel:
                add(
                    "guard-collision",
                    rel,
                    1,
                    f"guard {want} is also produced by {guards_seen[want]}; " "rename one header",
                )
            guards_seen.setdefault(want, rel)
            if not g:
                add("guard-missing", rel, 1, f"no include guard; expected {want}")
            else:
                got = g.group(1)
                ln = text[: g.start()].count("\n") + 1
                if got != want:
                    add("guard-form", rel, ln, f"guard {got} should be {want}")

        # 6. named namespace / using namespace
        for m in re.finditer(r"^\s*namespace\s+(\w+)", text, re.M):
            add(
                "namespace",
                rel,
                text[: m.start(1)].count("\n") + 1,
                f"named namespace `{m.group(1)}`; the library is one flat pc_ scope",
            )
        for m in re.finditer(r"^\s*using\s+namespace\s+([\w:]+)", text, re.M):
            add("using-namespace", rel, text[: m.start(1)].count("\n") + 1, f"using namespace {m.group(1)}")

        # 4. macros (headers only: a .cpp macro is not exported)
        if is_header:
            # the file's own include guard is a macro, but s4 gives it PROTOCORE_,
            # not PC_ - checking it here would contradict the guard rule above
            own_guard = guard_for(rel)
            for m in re.finditer(r"^\s*#\s*define\s+([A-Za-z_]\w*)", text, re.M):
                name = m.group(1)
                ln = text[: m.start(1)].count("\n") + 1
                if FOREIGN.match(name) or name == own_guard or name.startswith("PROTOCORE_"):
                    continue
                if not name.startswith("PC_"):
                    add("macro-prefix", rel, ln, f"macro {name} lacks the PC_ prefix")
                elif not re.fullmatch(r"PC_[A-Z0-9_]+", name):
                    add("macro-case", rel, ln, f"macro {name} is not PC_UPPER_SNAKE")
                if len(name) > MACRO_LIMIT:
                    add("macro-length", rel, ln, f"macro {name} is {len(name)} chars, over {MACRO_LIMIT}")

        # 5. enum types
        #
        # The declaration carries the packing attribute between `enum` and the name
        # (`typedef enum PROTO_ENUM_PACKED { ... } pc_foo;`), so the attribute is stepped over
        # rather than read as the tag. Every enum in the tree carries it, and reading it as the
        # name reported one violation per enum against a macro no rename could fix.
        #
        # That form is also ANONYMOUS: the type's name is the typedef's trailing identifier, not a
        # tag. When no tag is present the brace is matched to find it.
        for m in re.finditer(r"\benum\s+(?:PROTO_ENUM_PACKED\s+)?(\w+|(?=\{))", text):
            name = m.group(1)
            at = m.start(1)
            if not name:  # anonymous: the typedef name follows the closing brace
                depth, i = 0, text.find("{", m.end())
                while i < len(text):
                    if text[i] == "{":
                        depth += 1
                    elif text[i] == "}":
                        depth -= 1
                        if depth == 0:
                            break
                    i += 1
                tail = re.match(r"\s*(\w+)\s*;", text[i + 1 :]) if i < len(text) else None
                if not tail:
                    continue  # an inline anonymous enum names no type
                name, at = tail.group(1), i + 1 + tail.start(1)
            ln = text[:at].count("\n") + 1
            if not re.fullmatch(r"pc_[a-z0-9_]+", name):
                add("enum-name", rel, ln, f"enum type {name} is not pc_snake_case")
            types_seen.setdefault(name.lower(), rel)

        # 8. collision material: exported function names
        if is_header:
            for m in re.finditer(r"\b([a-z_]\w*)\s*\([^;{)]*\)\s*;", text):
                funcs_seen.setdefault(m.group(1).lower(), rel)
            for m in re.finditer(r"\b(?:struct|class)\s+(\w+)", text):
                types_seen.setdefault(m.group(1).lower(), rel)

    # 8. type/function collisions after case normalization
    for name in sorted(set(types_seen) & set(funcs_seen)):
        add(
            "collision",
            types_seen[name],
            1,
            f"type and function both normalize to `{name}` " f"(function declared in {funcs_seen[name]})",
        )

    return findings


def key(f):
    return f"{f['kind']}|{f['file']}|{f['msg']}"


def main():
    args = sys.argv[1:]
    findings = check()
    counts = {}
    for f in findings:
        counts[f["kind"]] = counts.get(f["kind"], 0) + 1

    if "--baseline" in args:
        n = bl.save(BASELINE, (key(f) for f in findings))
        print(f"check_symbols: baseline written, {n} known violations")
        for k in sorted(counts, key=lambda k: -counts[k]):
            print(f"  {counts[k]:>5}  {k}")
        return 0

    if "--check" in args:
        if not os.path.exists(BASELINE):
            print("check_symbols: no baseline; run --baseline first", file=sys.stderr)
            return 1
        new, still, fixed = bl.filter_new(findings, key, BASELINE)
        if new:
            print(f"check_symbols: {len(new)} NEW naming-law violation(s) " f"(see docs/SYMBOLS.md)", file=sys.stderr)
            for f in new[:40]:
                print(f"  {f['file']}:{f['line']}: [{f['kind']}] {f['msg']}", file=sys.stderr)
            return 1
        print(
            f"check_symbols: OK - no new violations "
            f"({still} known remain{f', {fixed} fixed since baseline' if fixed > 0 else ''})"
        )
        return 0

    print(f"check_symbols: {len(findings)} violations of docs/SYMBOLS.md\n")
    for k in sorted(counts, key=lambda k: -counts[k]):
        print(f"  {counts[k]:>5}  {k}")
    if "--summary" not in args:
        print()
        for f in findings[:60]:
            print(f"  {f['file']}:{f['line']}: [{f['kind']}] {f['msg']}")
        if len(findings) > 60:
            print(f"  ... {len(findings) - 60} more (--summary for counts only)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
