#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# A tree that carries somebody else's words, marked as theirs and left alone.
#

import os



# --------------------------------------------------------------------
# VERBATIM THIRD-PARTY TEXT
# --------------------------------------------------------------------
#
# Somebody else's words, reproduced byte for byte. A register finding inside one is a finding
# against its author, and a rewrite inside one corrupts a document this project does not own.
#
#   docs/learn/RFC                    IETF documents as the RFC Editor published them. RFC 2119
#                                     MUST and SHOULD are normative keywords in that corpus.
#   docs/learn/rfc                    the same corpus, lowercased.
#   docs/learn/datasheets             vendor datasheets and the .txt extracted from them.
#
# NAMED AS A CONCEPT AND NOT AS A PATH LIST, and that difference is the whole reason this is written
# as a rule. A path list is a chore somebody has to remember to extend, and the way a chore fails is
# that a fourth corpus arrives and nobody adds it. So there are two answers to the question and
# either one is sufficient: the path sits under one of the defaults below, or the directory declares
# itself by holding a VERBATIM_MARKER file. Nothing in any tree here carries that marker today. It
# exists so the default list is not the only way to answer, and a tree vendoring a corpus this tool
# has never heard of can say so without editing this tool.
#
# WHAT IT COSTS, named here. An index page of this project's own prose inside a verbatim root goes
# quiet with the corpus it indexes, and the ledger names it on every run that reads past it.
#
# THE RFC .txt FILES ARE NOT READ TODAY ANYWAY, because .txt is not in CHECKED. That is true and it
# is not the reason this rule exists: the rule has to hold when somebody adds .txt, and a rule whose
# correctness depends on an unrelated tuple staying short is not a rule.
VERBATIM_MARKER = ".verbatim"

VERBATIM_ROOTS = (
    (
        "docs/learn/RFC",
        "IETF documents as the RFC Editor published them",
    ),
    ("docs/learn/rfc", "the same IETF corpus, lowercased, as published"),
    ("docs/learn/datasheets", "vendor datasheets and the .txt extracted from them"),
    (
        "Salishan/oracles",
        "tables transcribed by hand from other people's published papers",
    ),
)

# Two components at least for every entry, because a bare directory name is not distinctive enough
# to be a rule. `oracles` alone would match any directory anywhere with that name, and this tree has
# a build/papers holding a different thing from the corpus papers/.
_VERBATIM_CEILING = 24
_VERBATIM_CACHE = {}


def verbatim_root(path):
    """(root, reason) where this path holds text reproduced from a third party, else None.

    Two answers, either sufficient. A default root matched on the path, or a directory at or above
    the file holding the marker. The marker walk stops at a repository boundary and again at a depth
    ceiling. A junction pointing at its own parent cannot make this walk forever.
    """
    posix = os.path.abspath(path).replace(os.sep, "/")
    for one, why in VERBATIM_ROOTS:
        if ("/%s/" % one.strip("/")) in posix:
            return one, why

    here = os.path.dirname(os.path.abspath(path))
    walked = []
    for _ in range(_VERBATIM_CEILING):
        if here in _VERBATIM_CACHE:
            answer = _VERBATIM_CACHE[here]
            break
        walked.append(here)
        if os.path.isfile(os.path.join(here, VERBATIM_MARKER)):
            answer = (
                here.replace(os.sep, "/"),
                "declared by a %s marker in that directory" % VERBATIM_MARKER,
            )
            break
        if os.path.isdir(os.path.join(here, ".git")) or os.path.isfile(
            os.path.join(here, ".git")
        ):
            answer = None
            break
        up = os.path.dirname(here)
        if up == here:
            answer = None
            break
        here = up
    else:
        answer = None

    for one in walked:
        _VERBATIM_CACHE[one] = answer
    return answer
