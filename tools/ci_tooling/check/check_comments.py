#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The comment law, mechanically.

docs/SRCBANNED.md: "Code is math. The comment is the plain-language description of it.
What it does and how it does it. Not why it exists, not why it works, not what it is
not, not what it used to be, not a justification, not a tradeoff, not a measurement.
Nothing else ever goes in there."

check_src_banned.py exempts comments by design, so until now nothing enforced this and
72 sites accumulated. Two families dominate and both are regex-shaped: a benchmark
number quoted in a comment, and a sentence about code that is no longer there. A number
goes stale the moment the code or the compiler moves; a history clause describes
something a reader cannot see. Neither describes the code below it.

What is deliberately NOT flagged, because a checker cannot decide it: whether a
description is accurate, whether it is too long, or whether prose without one of these
markers is really a justification. Those stay review items.

Exempt: docs/, test/, examples/, performance_benching/; file-header Doxygen blocks;
@param / @return / @brief lines; SPDX and license headers; the PC_ALLOW_* and NOSONAR
justification markers the other checkers require.

  python -m tools.ci_tooling.check.check_comments --all      # report, honor the baseline
  python -m tools.ci_tooling.check.check_comments --save     # re-record the floor after a sweep
"""

import os
import re
import sys

from tools.ci_tooling.lib import baseline
from tools.ci_tooling.lib import doc_region as dr

ROOT = dr.repo_root(__file__)
SRC = os.path.join(ROOT, "src")

# A measurement: a figure that was true of one build on one die on one day.
#
# A bare time unit is NOT enough. "a ~40-200 ms delayed-ACK stall" is a fact about TCP that
# holds on every target, and "waits 2 ms" is what the code does; neither goes stale when the
# compiler moves. What goes stale is a figure taken off a particular chip, so the pattern wants
# either an explicit measurement word or a unit only a profiler produces (cycles), or a
# comparison against another implementation.
MEASURED = re.compile(
    r"\b(?:measured|benchmark(?:ed)?|bisected on-device)\b"
    r"|\b\d[\d,._]*\s*(?:cycles?|cyc)\b"
    r"|\b\d+(?:\.\d+)?\s*x\s+(?:faster|slower)\b"
    r"|\b(?:faster|slower)\s+than\b"
    r"|\b\d+(?:\.\d+)?%\s*(?:faster|slower|win)\b"
    r"|\b\d[\d,._]*\s*(?:ns|us|ms|MB/s|kB/s)\b[^.]{0,40}?\bon (?:an?|the) (?:ESP32|S3|S2|P4|C3|C6|die|chip)",
    re.I,
)

# History: a sentence about code that is not there to read.
HISTORY = re.compile(
    r"\b(?:used to (?:be|live|hang|call|do)|it used to|that used to|which used to)\b"
    r"|\bwas previously\b|\bformerly\b|\bthe old (?:code|note|path|way|one)\b"
    r"|\boriginally\b|\bno longer\b|\bthis replaced\b|\bit replaces\b"
    r"|\bused to be inferred\b|\bthe loop that used to\b",
    re.I,
)

# Tradeoff: a comparison against a road not taken.
TRADEOFF = re.compile(
    r"\b(?:simpler|easier|cheaper|cleaner|more efficient|less efficient)\s+than\b"
    r"|\bat the cost of\b|\bnot a (?:style )?preference\b|\bnot worth shipping\b"
    r"|\bwasteful\b",
    re.I,
)

# Meta-commentary aimed at a reader rather than describing the code.
META = re.compile(
    r"\b(?:see the handover|for now|read before applying|note to|beware|"
    r"the whole reason|which is why we|the real question)\b",
    re.I,
)

RULES = (("measurement", MEASURED), ("history", HISTORY), ("tradeoff", TRADEOFF), ("meta", META))

# Lines a comment scan must not judge.
EXEMPT_LINE = re.compile(
    r"SPDX-License-Identifier|Copyright \(C\)|@file\b|@brief\b|@param\b|@return\b|"
    r"@author\b|@date\b|PC_ALLOW_[A-Z_]+|NOSONAR|@ref\b|@p\b",
)


def comment_spans(text):
    """(line number, comment text) for every comment, with strings masked out.

    A `//` inside a string literal is not a comment; masking first is what keeps a URL
    or a format string from being read as one.
    """
    out = []
    i, n = 0, len(text)
    line = 1
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
        elif c in "\"'":
            q = c
            i += 1
            while i < n and text[i] != q:
                if text[i] == "\\":
                    i += 1
                if i < n and text[i] == "\n":
                    line += 1
                i += 1
            i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append((line, text[i:j]))
            i = j
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i)
            j = n if j < 0 else j + 2
            out.append((line, text[i:j]))
            line += text.count("\n", i, j)
            i = j
        else:
            i += 1
    return out


def is_file_header(text, start_line):
    """A leading /** ... */ block is the file's documentation, not a code comment."""
    return start_line <= 40 and "@file" in text


def findings():
    out = []
    for d, dirs, fs in os.walk(SRC):
        dirs[:] = [x for x in dirs if x not in (".git", ".pio")]
        for f in sorted(fs):
            if not f.endswith((".c", ".h", ".cpp")):
                continue
            p = os.path.join(d, f)
            rel = os.path.relpath(p, ROOT).replace(os.sep, "/")
            text = open(p, encoding="utf-8", errors="replace").read()
            for line, body in comment_spans(text):
                if is_file_header(body, line):
                    continue
                for raw in body.split("\n"):
                    if EXEMPT_LINE.search(raw):
                        continue
                    for kind, rx in RULES:
                        m = rx.search(raw)
                        if m:
                            out.append((kind, rel, line, raw.strip()[:110], m.group(0)))
                            break
                    line += 0
    return out


def key_of(f):
    """Keyed without the line number, so unrelated edits above a site do not resurrect it."""
    return f"{f[0]}|{f[1]}|{f[3]}"


def main(argv):
    found = findings()
    path = baseline.path_for(__file__, "comments_baseline")

    if "--save" in argv:
        n = baseline.save(path, (key_of(f) for f in found))
        print(f"check_comments: recorded {n} known site(s)")
        return 0

    new, known, fixed = baseline.filter_new(found, key_of, path)
    if new:
        print(f"check_comments: {len(new)} new story comment(s):", file=sys.stderr)
        for kind, rel, line, raw, hit in new[:40]:
            print(f"  {rel}:{line}: [{kind}] {raw}", file=sys.stderr)
        if len(new) > 40:
            print(f"  ... {len(new) - 40} more", file=sys.stderr)
        print(
            "\nA comment states what the code below it does. Not a measurement, not what the\n"
            "code used to be, not why one shape was chosen over another. See docs/SRCBANNED.md.",
            file=sys.stderr,
        )
        return 1

    print(f"check_comments: OK - no new violations ({known} known remain, {fixed} fixed)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
