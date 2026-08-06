#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Harvest symbols from src/ headers, so checkers compare docs against the code.

Written for check_examples.py and reused by the naming-law checker: both need to
know what the code actually declares rather than what a document claims. Keeping
one harvester means the two cannot disagree about, say, which enum a member
belongs to.
"""

import os
import re

from ci_tooling.lib import doc_region as dr

_BLOCK = re.compile(r"/\*.*?\*/", re.S)
_LINE = re.compile(r"//[^\n]*")


def blank_comments(text):
    """Replace comments with spaces, PRESERVING every newline.

    Line numbers are the whole point of a checker's output, and a substitution that
    deletes a block comment silently shifts every line after it - check_symbols
    reported a namespace at line 73 that actually sat at 108. Blanking instead of
    deleting keeps offsets exact, and keeps comment text from matching code patterns.
    """

    def _blank(m):
        return re.sub(r"[^\n]", " ", m.group(0))

    return _LINE.sub(_blank, _BLOCK.sub(_blank, text))


# retained name; same line-preserving behavior
def _decomment(text):
    return blank_comments(text)


_NOISE = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.DOTALL)


def blank_comments_and_strings(text):
    """Blank comments AND string/char literals, PRESERVING every newline.

    What a scanner for code patterns wants: a symbol named in prose or quoted in a message is
    not a definition, and blanking rather than deleting keeps reported line numbers exact.
    """

    def _blank(m):
        return re.sub(r"[^\n]", " ", m.group(0))

    return _NOISE.sub(_blank, text)


def headers(root=None):
    """Every .h under src/, absolute paths."""
    root = root or dr.repo_root(__file__)
    out = []
    for base, dirs, files in os.walk(os.path.join(root, "src")):
        for f in files:
            if f.endswith(".h"):
                out.append(os.path.join(base, f))
    return sorted(out)
