#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# An SPDX or copyright block is the license's wording and not this project's.
#

import re

from .scan import MARKER



# --------------------------------------------------------------------
# LEGAL BLOCKS: SPDX AND COPYRIGHT
# --------------------------------------------------------------------
#
# An SPDX identifier and a copyright grant are legal artifacts, and the register of a license grant
# is the license's business. This tree carries a three-way LicenseRef line on every file of its
# own. Rewriting a word inside one edits the artifact.
#
# BLANKED PER BLOCK AND NEVER PER LINE. A GPL grant runs fifteen lines and two of them hold anything
# a regex can find. Blanking the two that matched would leave thirteen lines of legal text located
# in front of the register scan.
#
# WHERE A BLOCK ENDS IS THE WHOLE RULE, and getting it wrong in either direction is expensive.
# Splitting blocks on blank source lines is wrong: a `#` alone on a line is not blank, so a whole
# comment header reads as one block and every finding under it goes quiet. A block is split instead
# on MARKER-stripped emptiness, the same test runs() already makes, and again wherever the comment
# form changes or closes.
#
# WITH THAT RULE IT SILENCES NOTHING, because every legal block in every tree here is already clean
# prose, which is the outcome to want from a rule whose job is to protect an artifact. Re-derive
# that zero after any change to the block walker: a walker reaching too far reports the same zero.
#
# THE SHARPEST FIXTURE HAS BOTH HALVES IN ONE FILE A FEW LINES APART. A real copyright and SPDX
# pair is not ours to edit. Prose ABOUT a license block, sitting directly under it with no blank
# line between, says `licence` in ordinary British convention and a person may fix it. A block rule
# reaching one line too far takes the second with the first. What separates them is the change of
# comment form, from `#` to a docstring.
# the same shape with a blank line to help, and all four sites survive this rule.
LEGAL = re.compile(
    r"SPDX-(?:License-Identifier|FileCopyrightText)"
    r"|\bCopyright\b\s*(?:\(c\)|©|(?:19|20)\d\d)"
    r"|\(c\)\s*(?:19|20)\d\d"
    r"|©\s*(?:19|20)\d\d"
    r"|All rights reserved"
    r"|Licensed under"
    r"|LicenseRef-"
    r"|GNU (?:Affero |Lesser )?General Public License"
    r"|This (?:program|file) is free software"
    r"|WITHOUT ANY WARRANTY"
    r"|MERCHANTABILITY",
    re.IGNORECASE,
)

# The comment forms a block can be written in. Tested in this order. `/*` is read before `*` and
# a docstring before a bare quote. `plain` is a continuation line inside a block opened above it.
COMMENT_FORMS = (
    ("cblock", ("/*", "*")),
    ("docstring", ('"""', "'''")),
    ("slash", ("//",)),
    ("hash", ("#",)),
    ("percent", ("%",)),
)


def comment_form(line):
    """Which comment form a line is written in, or `plain` for a continuation."""
    body = line.strip()
    for name, markers in COMMENT_FORMS:
        if body.startswith(markers):
            return name
    return "plain"


def form_closes(form, line, opening):
    """Whether this line ends the block it is in.

    A C block ends at its `*/` and a docstring at its closing triple quote, and both can be followed
    on the next line by a second block of the same form. The @file block sits directly under the
    SPDX block in every header the comment standard specifies. Without this test the two read as
    one and every @file brief in the tree would go unchecked.
    """
    body = line.strip()
    if form == "cblock":
        return "*/" in body
    if form == "docstring":
        marks = body.count('"""') + body.count("'''")
        return marks >= 2 if opening else marks >= 1
    return False


def comment_blocks(said):
    """(start, stop) for each contiguous comment block in a prose view, as 0-based half-open spans.

    Emptiness is measured on the MARKER-stripped text, which is what makes a lone `#` a separator.
    A lone ` */` strips to empty too, and it closes the C block above it: it belongs to that block
    and ends it. Read as a separator it was left behind when a license block was blanked.
    """
    spans = []
    at = 0
    while at < len(said):
        if not MARKER.sub("", said[at]).strip():
            at += 1
            continue
        form = comment_form(said[at])
        stop = at
        while stop < len(said):
            if stop > at:
                if not MARKER.sub("", said[stop]).strip():
                    if (comment_form(said[stop]) == form) and form_closes(form, said[stop], False):
                        stop += 1
                    break
                if comment_form(said[stop]) != form:
                    break
            done = form_closes(form, said[stop], stop == at)
            stop += 1
            if done:
                break
        spans.append((at, stop))
        at = stop
    return spans


def legal_blank(said, path=None, ledger=None):
    """The same prose view with every comment block holding a legal line blanked out.

    Line numbers are preserved, the way every other view in this file preserves them. A finding
    still names a line a reader can open.
    """
    retained = list(said)
    for start, stop in comment_blocks(said):
        if not any(LEGAL.search(one) for one in retained[start:stop]):
            continue
        if ledger is not None and path is not None:
            ledger.note(
                "legal block",
                "an SPDX or copyright block is a legal artifact and its wording is the "
                "license's, not this project's",
                "%s:%d-%d" % (path.replace("\\", "/"), start + 1, stop),
            )
        for at in range(start, stop):
            retained[at] = ""
    return retained
