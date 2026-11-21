#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# The revision a measurement was taken at, so a number can be re-derived.
#

import os

from .manifest import MANIFEST_NAMES, manifest_home
from .repository import git_say



# ====================================================================
# REPORTING: A COUNT IS NOT A RESULT UNTIL IT SAYS WHAT IT COUNTED
# ====================================================================
#
# Three lines, and each one prevented a real confusion.
#
# PRINT THE REVISION MEASURED. One repository read 58 British sites on one branch and 15 on another,
# and the difference looked like a defect in this tool to two separate readers for the majority of a day. A
# count without its revision is a count about an unspecified tree. And a revision is only authority
# while it is REACHABLE FROM SOMEWHERE DURABLE: a local-only commit is a tree of one, and a fixture
# that lived only inside a linked worktree dies with the session that made it. So the revision is
# printed with its reachability, and "NOT PUSHED" is printed in those words when nothing on a remote
# contains it.
#
# PRINT THE ROOTS SCANNED AND THE ROOTS LOOKED FOR AND NOT FOUND. The closed-repository scan this
# tool once ran taught the reason, and it generalizes: "scanned none" and "there are none" must
# never be the same output, for any root and not only a closed one.
#
# PRINT THE ROOTS IT WAS CONFIGURED WITH, and this is the line that matters max. One repository
# declares roots = ["README.md", "test"] for both its prose gate and its commit hook. Its sixty
# translation units and its CMakeLists.txt are outside that declared scope, and they stay outside it
# after every repair this tool has had. A run reporting "0 findings" over two configured roots reads
# exactly like a clean tree, and that is how a repository carrying dozens of British definitions reads
# as green. The roots go at the top of the report, and a reader meets the scope before the count.
#
# AND SAY WHAT THIS TOOL DOES NOT ANSWER FOR. There are four independent reasons a finding survives
# a gate and each is sufficient alone: the hook is not installed, the declared roots exclude the
# file, the rule cannot match it, and the extension cannot be opened. This file owns the last two.
# The first two are per-repository decisions that change what every committer has to satisfy, they
# belong to each repository's captain, and a tool implying it has settled them is worse than one
# that says nothing. A gate that is correct, installed nowhere, and scoped to two paths still
# catches nothing. The footer says so in one line.

_REF_CACHE = {}


def tree_ref(path):
    """(repository, revision, reachability, dirty) for the tree a scanned root sits in, or None.

    Reachability is answered by asking which remote-tracking ref contains the revision. That is the
    only durable test available locally: a branch name proves nothing, since a local branch that was
    never pushed has one. Cached per repository, because a run reads hundreds of files out of a
    handful of checkouts.
    """
    where = path if os.path.isdir(path) else os.path.dirname(os.path.abspath(path))
    top = git_say(where, ("rev-parse", "--show-toplevel"))
    if not top:
        return None
    top = os.path.abspath(top)
    if top in _REF_CACHE:
        return _REF_CACHE[top]

    revision = git_say(top, ("rev-parse", "--short", "HEAD"))
    if not revision:
        _REF_CACHE[top] = None
        return None
    remote = git_say(
        top,
        (
            "for-each-ref",
            "--contains",
            revision,
            "--format=%(refname:short)",
            "refs/remotes",
        ),
    )
    if remote:
        reach = "reachable from %s" % remote.splitlines()[0].strip()
    else:
        reach = "NOT PUSHED, no remote-tracking ref contains it"
    dirty = bool(git_say(top, ("status", "--porcelain")))
    answer = (top, revision, reach, dirty)
    _REF_CACHE[top] = answer
    return answer


def refs_for(roots):
    """One (repository, revision, reachability, dirty) per distinct checkout among the roots."""
    held = []
    for one in roots:
        answer = tree_ref(one)
        if answer and answer not in held:
            held.append(answer)
    return held


def manifests_covering(roots):
    """Every signed manifest that attests anything under the roots being scanned.

    Printed after a run that offered to write. Nobody has to already know the corpus is hashed to
    find out that it is. A manifest is a repository-level fact and there are one or two of them.
    This asks once per root and not once per file.
    """
    found = []
    for one in roots:
        home = manifest_home(one)
        if not home:
            continue
        for name in MANIFEST_NAMES:
            where = os.path.join(home, name)
            if os.path.isfile(where) and where not in found:
                found.append(where)
    return found
