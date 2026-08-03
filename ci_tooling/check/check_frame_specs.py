#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Verify every PC_FK_LIT field in a frame spec carries its literal's true length.

A frame spec is a `static const pc_field[]` whose literal fields are written
`{PC_FK_LIT, 0, <len>, "<text>"}`. The length is spelled out because computing it would take a
function-like macro, which AUTOSAR A16-0-1 forbids, and `constexpr` is banned here as well. A wrong
length is not a compile error: too small silently truncates the frame, too large reads past the
literal, and both look correct in review.

This gate closes that. It scans src/, examples/ and penetration_testing/ for PC_FK_LIT initializers, decodes
the C escape sequences in each literal, and compares the byte count against the declared value.
`--fix` rewrites the declared value in place.

Exit 0 when every spec agrees, 1 otherwise.

Usage:
    check_frame_specs.py [--fix] [--verbose]
"""

import os
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
TREES = ("src", "examples", "penetration_testing", "test")
EXTS = (".h", ".c", ".cpp", ".ino")

# {PC_FK_LIT, <width>, <len>, "text"} - the literal may be several adjacent string tokens, which C
# concatenates, so the whole run is captured and measured together.
FIELD = re.compile(
    r"\{\s*PC_FK_LIT\s*,\s*(?P<width>[0-9]+)\s*,\s*(?P<len>[0-9]+)\s*,\s*"
    r"(?P<lit>\"(?:[^\"\\]|\\.)*\"(?:\s*\"(?:[^\"\\]|\\.)*\")*)\s*\}")

STRING_TOKEN = re.compile(r"\"((?:[^\"\\]|\\.)*)\"")

# C escapes that stand for one byte. \xNN and \NNN are handled separately.
SIMPLE_ESCAPES = {"n": 1, "t": 1, "r": 1, "0": 1, "\\": 1, "\"": 1, "'": 1,
                  "a": 1, "b": 1, "f": 1, "v": 1, "?": 1}


def literal_length(tok_run):
    """Byte length of one or more adjacent C string tokens, after escape decoding."""
    total = 0
    for body in STRING_TOKEN.findall(tok_run):
        i = 0
        while i < len(body):
            if body[i] != "\\":
                total += 1
                i += 1
                continue
            nxt = body[i + 1] if i + 1 < len(body) else ""
            if nxt == "x":
                j = i + 2
                while j < len(body) and body[j] in "0123456789abcdefABCDEF":
                    j += 1
                total += 1
                i = j
            elif nxt.isdigit():
                j = i + 1
                while j < len(body) and j < i + 4 and body[j] in "01234567":
                    j += 1
                total += 1
                i = j
            else:
                total += SIMPLE_ESCAPES.get(nxt, 1)
                i += 2
    return total


def main(argv):
    fix = "--fix" in argv
    verbose = "--verbose" in argv
    bad, checked, fixed = [], 0, 0

    for tree in TREES:
        base = ROOT / tree
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in EXTS or ".pio" in path.parts or "build" in path.parts:
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            if "PC_FK_LIT" not in text:
                continue
            edits = []
            for m in FIELD.finditer(text):
                checked += 1
                want = literal_length(m.group("lit"))
                got = int(m.group("len"))
                if want == got:
                    continue
                line = text.count("\n", 0, m.start()) + 1
                rel = str(path.relative_to(ROOT)).replace(os.sep, "/")
                bad.append((rel, line, got, want, m.group("lit")[:44]))
                if fix:
                    edits.append((m.start("len"), m.end("len"), str(want)))
            if fix and edits:
                for start, end, rep in reversed(edits):
                    text = text[:start] + rep + text[end:]
                path.write_text(text, encoding="utf-8", newline="\n")
                fixed += len(edits)

    if bad and not fix:
        print(f"check_frame_specs: {len(bad)} frame literal(s) with a wrong length:", file=sys.stderr)
        for rel, line, got, want, lit in bad:
            print(f"  {rel}:{line}: declared {got}, literal is {want} bytes  {lit}", file=sys.stderr)
        print("\nRe-run with --fix to correct them.", file=sys.stderr)
        return 1

    if fix:
        print(f"check_frame_specs: corrected {fixed} length(s) of {checked} checked")
        return 0

    print(f"check_frame_specs: OK - {checked} frame literal(s), every declared length matches")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
