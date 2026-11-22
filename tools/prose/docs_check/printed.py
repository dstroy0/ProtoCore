#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# The standard applied to the text a tool prints, which prose_only blanks along with the code.
#
# WHY A SEPARATE STAGE. prose_only reads the comments and docstrings of a source file and blanks
# everything else. For a library that is right. For a checker it is not: the tools in
# examples/00_blob_viz_tools each print several paragraphs of argument about what they measured, and
# a person reads those paragraphs the way they read a page. None of that text sits in a comment, so
# none of it reaches the scan, and a clean run says nothing about it.
#
# docs-check: quoting
# `quotient_coherence` prints 'which is the average of those two' and `reading_rank` prints
# 'which is why 64 distinct signatures out of 64 rounds is not evidence'.
# docs-check: end quoting
#
# Both phrases are on the banned table and both sit in text a reader sees.
#
# IT IS READ OFF THE PARSE TREE AND NOT OFF THE LINES. A string built across several lines is one
# literal to ast and is read once, and a sentence inside a comment is not read twice, because a
# comment is not in the tree at all. That is the whole reason this does not use a regex over the
# source.
#
# A PRINTED FINDING IS A PROSE FINDING and never a breaking one. The text reaches a reader the same
# way a page does, so it belongs in the same column under the same rule: a person decides each site.

import ast

from .grammar import CONFIRM
from .index import _COMPILED, candidates

# The calls whose arguments reach a reader. `write` covers sys.stdout.write without this having to
# know what the module was imported as, and `say` is what the view builders name their own printer.
REPORTING = ("say", "print", "write")

# How much of the offending sentence to quote beside the finding. Enough to recognize it by.
QUOTED = 72


def printed_strings(path, lines):
    """Every string literal reaching a reader, as (line, text), each reported once.

    A literal nested inside a formatting expression is reached once through the call it sits in.
    Walking the call would reach it again for every enclosing node, so the positions already seen
    are held. By POSITION and not by value: two identical sentences on two lines are two findings.

    Returns nothing for a file that does not parse. An unparseable .py is a Python error and this
    stage has no located to report one; the interpreter says it better.
    """
    try:
        tree = ast.parse("\n".join(lines), path)
    except (SyntaxError, ValueError):
        return []

    seen = set()
    found = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        if isinstance(node.func, ast.Name):
            name = node.func.id
        elif isinstance(node.func, ast.Attribute):
            name = node.func.attr
        else:
            continue
        if name not in REPORTING:
            continue
        for piece in ast.walk(node):
            if not isinstance(piece, ast.Constant) or not isinstance(piece.value, str):
                continue
            where = (piece.lineno, piece.col_offset)
            if where in seen:
                continue
            seen.add(where)
            found.append((piece.lineno, piece.value))
    return sorted(found)


def printed_hits(path, lines):
    """Every banned token in the text this file prints, as (line, complaint) findings.

    One site is one finding. Several patterns in the table overlap and match the same span, and
    reporting each of them prints the same line twice with nothing to tell the two apart. The first
    hit names the site and the writer fixes the sentence.

    The literal index narrows the table to the patterns that can match this string. It is the same
    necessary condition banned_hits uses, so it cannot add a finding or lose one.
    """
    if not path.endswith(".py"):
        return []
    found = []
    for at, text in printed_strings(path, lines):
        for pattern in candidates(text):
            held = CONFIRM.get(pattern)
            hit = next(
                (one for one in _COMPILED[pattern].finditer(text) if held is None or held(one)),
                None,
            )
            if hit:
                found.append(
                    (
                        at,
                        "printed '%s' in: %s"
                        % (hit.group(0), " ".join(text.split())[:QUOTED]),
                    )
                )
                break
    return found
