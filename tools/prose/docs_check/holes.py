#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# The hole a repair leaves behind: a word taken out and the sentence left ungrammatical.
#
# WHY THIS IS NOT PART OF THE SCAN. Every rule in BANNED is bounded tightly enough to report on any
# file in the tree. These are not: `the which` and a doubled article appear in quoted grammar
# examples, in test fixtures and in generated tables, and a tree-wide run of them reports mostly
# those. What makes them usable is scope. Read against only the lines a pass ADDED they report
# almost nothing but real holes, because a hole is made by an edit and a tree holds few of them.
#
# So this takes a unified diff and reads the added lines only. The deletions are what the pass meant
# to remove and the context is what it did not touch; neither can carry a hole the pass made.
#
# IT IS A SUSPICION AND NEVER A FINDING. Nothing here fails anything. utils/maint/prose/check_fixes.py names
# this fault, and the output is a list of lines to read.

import re

# A pattern and the fault it suspects. Each one is a shape English does not make, which is what lets
# a list this short are in for reading every edited line.
HOLE = (
    (re.compile(r"\bis the (to|that|which|whose|who)\b"), "article with no noun"),
    (re.compile(r"\bthe (whose|which|that) \w+ing\b"), "article with no noun"),
    (re.compile(r"\b(a|an|the) (is|are|was|were|and|or|but)\b"), "article with no noun"),
    (re.compile(r"\b(is|are|was|were) (is|are|was|were)\b"), "doubled verb"),
    (re.compile(r"\b(the|a|an) (the|a|an)\b"), "doubled article"),
    (re.compile(r"\b(and|or|but) (and|or|but)\b"), "doubled conjunction"),
    (re.compile(r"^\s*([#*]|//|--)?\s*[,;:]\s"), "line opening on punctuation"),
    (re.compile(r"\b(instead|rather) (of|than) (of|than)\b"), "doubled hinge"),
    (re.compile(r"\bThat is\s*[.]"), "pronoun with no predicate"),
    (re.compile(r"[a-z]\s+[.]\s"), "space before a full stop"),
    # The hinge wants a noun or a gerund after it. A bare infinitive there is the verb left located
    # where its subject came out.
    (re.compile(r"\binstead of (report|draw|write|render|pick|assume|sample|support|place|count"
                r"|replace|start|sign|generate|carry|return)\b"), "bare verb after the hinge"),
    (re.compile(r"[.]\s+[a-z]{2,}\s"), "sentence opening lower case"),
)

# How much of a suspect line to quote. Enough to read the fault in, short enough that a long line
# does not push the next one off the screen.
QUOTED = 160


def diff_holes(path):
    """Every added line in a unified diff that carries a suspected hole.

    Returns (name, why, line) rows in the order the diff gives them. One row per line even where
    two patterns match it, since the line is what gets read and read once.
    """
    found = []
    name = None
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if line.startswith("+++ "):
                # `+++ b/path`, and the b/ prefix is git's and not part of the name.
                name = line[4:].rstrip("\r\n")
                if name.startswith(("a/", "b/")):
                    name = name[2:]
                continue
            if not line.startswith("+") or line.startswith("+++"):
                continue
            body = line[1:].rstrip("\r\n")
            for want, why in HOLE:
                if want.search(body):
                    found.append((name, why, body[:QUOTED]))
                    break
    return found
