#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# Which lines a change added, so a finding on one can be told from a finding it inherited.
#
# A file carrying a hundred older findings buries the three a change just wrote, and the three are
# the only ones whose author is still at the keyboard. --changed reads the files the diff touches
# and retains the findings whose line the diff added. A file new to the tree counts every line.
#
# THIS NARROWS THE REPORT AND NEVER THE READ. Every finding in every touched file is still computed
# and the ratchet still sees all of them, because a count that depended on which lines a diff
# happened to touch would ratchet down on a commit that only moved code around.

import os
import re
import subprocess

from .repository import REPOSITORY, git_env

# A hunk header in unified diff output: the new file's first line, and how many lines follow.
HUNK = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")


def git_lines(args):
    """One git command's stdout as lines, or none where git cannot answer."""
    try:
        said = subprocess.check_output(
            ["git"] + list(args),
            cwd=REPOSITORY,
            stderr=subprocess.PIPE,
            env=git_env(),
        )
    except (OSError, subprocess.CalledProcessError):
        return None
    return said.decode("utf-8", "replace").splitlines()


def added_lines(diff_arguments):
    """Each changed file mapped to the line numbers the diff added in it.

    -U0 asks for no context, so every line inside a hunk is a line the change wrote. A file the
    diff deletes outright has no new side and is left out: /dev/null is not a file to check.
    """
    said = git_lines(["diff", "-U0", "--no-color"] + list(diff_arguments))
    if said is None:
        return None
    added = {}
    name = None
    for line in said:
        if line.startswith("+++ "):
            given = line[4:]
            name = None if given == "/dev/null" else given[2:] if given.startswith("b/") else given
            if name is not None:
                added.setdefault(name, set())
            continue
        found = HUNK.match(line)
        if found and name is not None:
            first = int(found.group(1))
            count = 1 if found.group(2) is None else int(found.group(2))
            added[name].update(range(first, first + count))
    return added


def untracked_lines(added):
    """Every line of every file git neither tracks nor ignores, folded into `added`.

    A new file carries no diff and would otherwise report nothing, which is the wrong answer for
    the file likely to be the one just written.
    """
    said = git_lines(["ls-files", "--others", "--exclude-standard"])
    for name in said or ():
        if not name:
            continue
        where = os.path.join(REPOSITORY, name)
        if not os.path.isfile(where):
            continue
        with open(where, encoding="utf-8", errors="replace") as handle:
            added[name] = set(range(1, len(handle.read().splitlines()) + 1))
    return added


def changed_scope(argv, option_value):
    """The files a change touched and the lines it added in each, or None where git cannot say.

    --changed reads the working tree against HEAD and counts an untracked file whole. --staged
    narrows it to the index, where an untracked file is not part of the commit being written.
    --base=<rev> reads the working tree against another revision, for a branch read in one go.
    """
    base = option_value("--base")
    if base:
        arguments = [base]
        untracked = False
    elif "--staged" in argv:
        arguments = ["--cached"]
        untracked = False
    else:
        arguments = []
        untracked = True

    added = added_lines(arguments)
    if added is None:
        return None
    if untracked:
        added = untracked_lines(added)
    return dict((name, lines) for name, lines in added.items() if lines)


def changed_paths(added):
    """The absolute path of every changed file this tool could read, in a stable order."""
    return sorted(os.path.join(REPOSITORY, name) for name in added)


def on_changed(name, added, at):
    """Whether a finding at line `at` of `name` sits on a line the change added."""
    return at in added.get(name.replace("\\", "/"), ())
