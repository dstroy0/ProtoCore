#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# Count every grammar pattern per 100k words of the reference papers, and of any text named beside them.
#
#   Usage:  python utils/maint/prose/grammar_evidence.py [TEXT ...]
#
# The papers column, for ISMS, is GRAMMAR_RATE in docs_check/grammar.py, printed as it is written.
# Each TEXT named gets a column of its own: the machine's prose from session_prose.py, or the plain
# text of a tree from docs_check --plain. A pattern earns a place in ISMS when its rate in the
# machine's prose is well above its rate in the papers.
#
# Every text is read the way the checker reads a file: lines joined into runs at blank lines, each
# pattern compiled with IGNORECASE. A rate counted another way is a rate for a different pattern.
#
# The papers are read whole and ungated, glosses and orthography included, and GRAMMAR_CORPUS says
# so wherever a rate is printed.

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from docs_check import CONFIRM, GRAMMAR, ISMS, runs  # noqa: E402

PAPERS = os.path.join(HERE, "..", "..", "..", "build", "papers")


def lines_of(path):
    with open(path, encoding="utf-8", errors="replace") as handle:
        return handle.read().splitlines()


def papers_lines():
    """Every line of every paper's text under build/papers, a blank line between two papers."""
    held = []
    for name in sorted(os.listdir(PAPERS)):
        if name.endswith(".txt"):
            held.extend(lines_of(os.path.join(PAPERS, name)))
            held.append("")
    return held


def rates(lines):
    """Each pattern's count per 100k words of the lines, and the word count."""
    texts = [text for text, _ in runs(lines)]
    words = max(1, sum(len(text.split()) for text in texts))
    compiled = [(pattern, re.compile(pattern, re.IGNORECASE)) for pattern in GRAMMAR]
    found = {}
    for pattern, rule in compiled:
        held = CONFIRM.get(pattern, lambda hit: True)
        count = sum(1 for text in texts for hit in rule.finditer(text) if held(hit))
        found[pattern] = count * 100000.0 / words
    return found, words


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if not os.path.isdir(PAPERS):
        print("  no papers at %s" % os.path.normpath(PAPERS))
        return 4
    human, total = rates(papers_lines())
    beside = [(path, rates(lines_of(path))) for path in sys.argv[1:]]
    print("# %d words of papers" % total)
    for path, (_, words) in beside:
        print("# %d words of %s" % (words, path))
    print("GRAMMAR_RATE = dict(zip(ISMS, (%s)))" % ", ".join("%.2f" % human[pattern] for pattern in ISMS))
    if beside:
        print()
        print("%9s %s  pattern" % ("papers", " ".join("%9s" % ("text %d" % (at + 1)) for at in range(len(beside)))))
        for pattern in GRAMMAR:
            print("%9.2f %s  %s" % (
                human[pattern],
                " ".join("%9.2f" % found[pattern] for _, (found, _) in beside),
                pattern[:90],
            ))
    return 0


if __name__ == "__main__":
    sys.exit(main())
