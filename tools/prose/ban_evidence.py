#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Score every banned pattern against human technical writing, and against both arms of this tree.
#
#   Usage:  python tools/prose/ban_evidence.py [--worst N]
#
# docs_check carries 242 patterns and every one of them got there because somebody noticed it.
# That makes the list a record of what was noticed. This asks what the list is claiming: that these
# phrases are the machine's and not a person's.
#
# prose_distance.py asked the same question with a distribution and could not answer it. The gap
# between the arms was the same sign at all four symbol widths and cleared no floor at any of them,
# because a banned phrase is a rare event and total variation is carried by the bulk. A rate is the
# right instrument for a rare event.
#
# THE TWO ARMS, AND THE ONE THAT WAS WRONG
#
# papers      154 research papers under build/papers, 7.3 MB, human, same register
# repository  every comment, docstring and page in this tree
#
# An earlier arrangement split the repository by whether this session opened a file, and called
# the untouched half a person's prose. That split is void: the text of this repository is almost all
# machine-written, across many sessions. Both halves had the same author. It explains the null
# prose_distance.py returned, where the gap between those halves was the same sign at four symbol
# widths and cleared no floor at any of them. There was no contrast in it to find.
#
# The papers are the only human arm available, and they are the whole control here.
#
# WHAT A ROW MEANS
#
# Rates are per hundred thousand words, because the majority of these phrases are rare enough that per
# thousand rounds everything to zero. A pattern that fires in the papers is a pattern describing
# ordinary technical English, and banning it costs a writer a phrase they are entitled to. A pattern
# that never fires in 154 papers and fires in the machine arm does the job the list claims.
#
# This does not decide anything on its own. A pattern can be absent from the papers because the
# papers are linguistics and the phrase belongs to systems programming. The output is evidence for
# reading, and the last column says which way each pattern leans.

import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
PAPERS = os.path.join(ROOT, "build", "papers")

sys.path.insert(0, HERE)

import docs_check  # noqa: E402
import prose_distance  # noqa: E402

PER = 100000.0


def words_in(text):
    """How many words a body of text holds, for the denominator of a rate."""
    return max(1, len(text.split()))


def papers_text():
    """Every extracted research paper, joined.

    Each one whole instead of a slice. A rate needs the denominator it was counted over, and there
    is no reason to sample when the corpus is 7.3 MB and already on disk.
    """
    if not os.path.isdir(PAPERS):
        return ""
    held = []
    for name in sorted(os.listdir(PAPERS)):
        if not name.endswith(".txt"):
            continue
        with open(os.path.join(PAPERS, name), encoding="utf-8", errors="replace") as handle:
            held.append(handle.read())
    return "\n".join(held)


def repository_text():
    """Every comment, docstring and page in this tree, joined.

    One arm instead of two. Splitting it by author is not available: the text here is almost all
    machine-written across many sessions, and the session-boundary split that looked like an
    author split was measuring nothing.
    """
    held = []
    for path in prose_distance.repository_files():
        held.append(prose_distance.prose_of(path))
    return " ".join(one for one in held if one)


def main():
    out = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace", newline="")
    worst = 40
    if "--worst" in sys.argv:
        worst = int(sys.argv[sys.argv.index("--worst") + 1])

    arms = {
        "papers": papers_text(),
        "repository": repository_text(),
    }
    sizes = {name: words_in(text) for name, text in arms.items()}

    out.write("\n  the arms, in words\n")
    for name in ("papers", "repository"):
        out.write("    %-11s %9d words\n" % (name, sizes[name]))

    rows = []
    for pattern in docs_check.BANNED:
        counts = {}
        for name, text in arms.items():
            counts[name] = len(re.findall(pattern, text, re.IGNORECASE))
        rates = {name: (counts[name] * PER / sizes[name]) for name in arms}
        rows.append((pattern, counts, rates))

    human = [one for one in rows if one[1]["papers"] > 0]
    absent = [one for one in rows if one[1]["papers"] == 0]

    out.write("\n  %d of %d patterns fire in the human papers, %d never do\n" % (len(human), len(rows), len(absent)))

    out.write("\n  patterns the humans use max, per %d words\n" % int(PER))
    out.write("    %-46s %9s %9s %9s\n" % ("pattern", "papers", "repository", ""))
    for pattern, counts, rates in sorted(human, key=lambda one: -one[2]["papers"])[:worst]:
        out.write("    %-46s %9.1f %9.1f %9.1f\n" % (pattern[:46], rates["papers"], rates["repository"], 0.0))

    out.write("\n  patterns absent from every paper but present in this tree\n")
    out.write("    %-46s %9s %9s %9s\n" % ("pattern", "papers", "repository", ""))
    caught = [one for one in absent if one[1]["repository"] > 0]
    for pattern, counts, rates in sorted(caught, key=lambda one: -one[2]["repository"]):
        out.write("    %-46s %9.1f %9.1f %9.1f\n" % (pattern[:46], rates["papers"], rates["repository"], 0.0))
    if not caught:
        out.write("    none\n")

    # The summary rate over the whole list. This is the number the ban list is worth as one figure.
    for label, retain in (("every pattern", rows), ("only the ones no paper uses", absent)):
        totals = dict.fromkeys(arms, 0)
        for pattern, counts, rates in retain:
            for name in arms:
                totals[name] += counts[name]
        out.write("\n  total hits per %d words, %s\n" % (int(PER), label))
        for name in ("papers", "repository"):
            out.write("    %-10s %8.1f\n" % (name, totals[name] * PER / sizes[name]))

    out.write("\n")
    out.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
