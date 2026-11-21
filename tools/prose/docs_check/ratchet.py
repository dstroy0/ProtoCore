#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# The per-file ceilings a count may fall under and never rise above.
#

import os

from .files import SKIP_DIRS
from .repository import REPOSITORY, git_say



# The ratchet this repository retains, for the readings that want it without being told where it is.
# Beside the package rather than inside it: the ceilings are this repository's record and the
# checker is a tool, and a tool does not carry one repository's numbers around in it.
DEFAULT_RATCHET = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "prose_ratchet.tsv"
)


RATCHET_HEADER = (
    "# Prose findings per file, the max each file may carry. docs_check --ratchet reads it.\n"
    "# A count may fall and never rise: a commit that raises one is stopped, and a commit that\n"
    "# lowers one writes the lower number here. Regenerate by hand with --ratchet-write only when a\n"
    "# file moves, since a moved file starts again at zero under its new name.\n"
)


def ratchet_read(path):
    """The per-file ceilings in a ratchet file, or None where the file is absent."""
    if not os.path.isfile(path):
        return None
    ceilings = {}
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if not line.strip() or line.startswith("#"):
                continue
            name, count = line.rstrip("\n").rsplit("\t", 1)
            ceilings[name] = int(count)
    return ceilings


def ratchet_write(path, ceilings):
    """Write the ceilings sorted by path, leaving out every file that carries none."""
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(RATCHET_HEADER)
        for name in sorted(ceilings):
            if ceilings[name]:
                handle.write("%s\t%d\n" % (name, ceilings[name]))


def ratchet_slack(ceilings, counts):
    """Every ceiling sitting above what its file now carries, widest gap first.

    A ceiling nobody reaches cannot stop anything: a file at three findings under a ceiling of forty
    may take thirty-seven more and still pass. The gap is the measure of how much of this file is
    ungoverned, and the answer to a wide one is to lower the entry.

    Rows are (slack, ceiling, carries, name). An entry naming a file no longer in the tree is left
    out, because that one wants deleting rather than lowering and belongs to a different repair.
    """
    rows = []
    for name in ceilings:
        ceiling = ceilings[name]
        here = counts.get(name, 0)
        if here < ceiling and os.path.exists(os.path.join(REPOSITORY, name)):
            rows.append((ceiling - here, ceiling, here, name))
    rows.sort(reverse=True)
    return rows


def staged_paths():
    """Repository-relative paths the commit being written adds, copies, modifies or renames."""
    said = git_say(REPOSITORY, ("diff", "--cached", "--name-only", "--diff-filter=ACMR"))
    return set(one.strip() for one in (said or "").splitlines() if one.strip())


def staged_under(roots):
    """The absolute path of every staged file that sits under one of the given roots.

    A pre-commit hook holds what the commit carries, and the read is scoped to the staged files and
    not the whole tree. A staged file outside every root has no ceiling here and is not this gate's
    to hold. It is left out. A root is a directory or a single file, and the test answers both.

    A staged file under a directory the walk skips is left out too. Renaming a fixture stages it,
    and the gate holds the commit to what the walk reads.
    """
    here = []
    bounds = [os.path.abspath(root) for root in roots]
    for name in staged_paths():
        if any(part in SKIP_DIRS for part in name.split("/")[:-1]):
            continue
        path = os.path.abspath(os.path.join(REPOSITORY, name))
        for root in bounds:
            if path == root or path.startswith(root + os.sep):
                here.append(path)
                break
    return here
