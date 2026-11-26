#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The ASD-STE100 rules over a source file's comments. The rules are the ones ste_check.py applies to
# the technical documents. This module reads them through that file. The tree holds one rule set.
# This module writes the comments of a file to build/gate/ste_comments.txt, one line a line with the
# code lines empty, and the checker reads that file. A finding then names the line a reader opens.
#

import importlib.util
import os
import re

from .repository import REPOSITORY
from .scan import MARKER

PROSE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STE_CHECK = os.path.join(PROSE, "ste_check.py")
STE_NAMES = os.path.join(PROSE, "ste_names.txt")
COMMENTS = os.path.join(REPOSITORY, "build", "gate", "ste_comments.txt")

_CHECKER = []
# A Doxygen tag at the start of a comment line, with @ before its name. Only the names Doxygen gives its commands
# count, because a style sheet in a comment also opens a line with @, as @media does. The backslash form is not
# read, because TeX in a comment opens a line with \ref or \par too.
# The mark of a Doxygen comment on the member before it, in either of its forms
MEMBER_MARK = re.compile(r"/\*\*<|//[/!]<")
DOXYGEN_TAG = re.compile(
    r"^@(?:param|tparam|return|returns|retval|brief|details|note|warning|attention|remark|remarks|see|sa|pre|post|"
    r"throws|throw|exception|invariant|todo|deprecated|since|file|defgroup|ingroup|addtogroup|struct|enum|def|"
    r"typedef|fn|var|class|union|namespace|example|code|endcode|bug|par|li|ref|copydoc|internal|private|public)\b"
)


def checker():
    """The ste_check module, loaded once from the prose folder."""
    if not _CHECKER:
        spec = importlib.util.spec_from_file_location("ste_check", STE_CHECK)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        _CHECKER.append(module)
    return _CHECKER[0]


def comment_text(said):
    """The prose view with each comment marker taken off. A code line stays empty. A comment block
    ends where the code starts, and the line numbers stay the file's.

    A Doxygen tag, such as @param, @return or @note, opens an entry of its own. The checker starts a
    sentence only before a capital letter, a digit, a back quote or a bracket. This view gives each
    tag line the mark of a list item, and the checker then reads the tag and its lines as one item. A
    comment on a member after its code, /**< or ///<, is an item of its own too, without the code."""
    text = []
    for line in said:
        member = MEMBER_MARK.search(line)
        if member:
            # a member's own comment, after the code it documents, is an item of its own
            text.append("- " + line[member.end() :].replace("*/", "").strip())
            continue
        stripped = MARKER.sub("", line).strip()
        text.append("- " + stripped if DOXYGEN_TAG.match(stripped) else stripped)
    return text


def ste_findings(said):
    """(line number, what) for each ASD-STE100 rule a comment breaks."""
    ste = checker()
    os.makedirs(os.path.dirname(COMMENTS), exist_ok=True)
    with open(COMMENTS, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(comment_text(said)) + "\n")
    names = ste.word_list(STE_NAMES) if os.path.isfile(STE_NAMES) else set()
    return [(number, "ste %s: %s" % (rule, what)) for number, rule, what in ste.findings(COMMENTS, set(), names)]
