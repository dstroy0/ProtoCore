#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Where the repository is, which roots hold its prose, and how git is asked.
#

import os
import subprocess

# Every place this project retains prose. A README beside the code makes the same claims a page under
# docs makes, and is read by the same people.
#
# Held against the repository and not against the working directory. These were plain relative names
# once, and from anywhere but the root they matched nothing: the run reported zero files, zero
# findings and success. A commit hook calling it that way lets every commit through and reports the
# prose as checked.
REPOSITORY = os.path.dirname(os.path.abspath(__file__))
# Walks up to the repository instead of counting directories to it. A count breaks every path here
# the moment anything moves.
while (REPOSITORY != os.path.dirname(REPOSITORY)) and not os.path.isfile(os.path.join(REPOSITORY, "repotools.toml")):
    REPOSITORY = os.path.dirname(REPOSITORY)

# Every directory holding writing of this project's own.
#
# A ROOT THAT MOVES IS THE FAILURE THIS LIST RETAINS HAVING, and it is silent: a stale name reads
# fewer files and still exits 0. The guard below turns a root that no longer exists into an error.
#
# IT CANNOT CATCH A ROOT THAT EMPTIED. A directory that still exists because one file stayed in it
# passes the guard while everything under it moves elsewhere and goes unread. The file count at the
# foot of the report is the only thing that shows that, so watch the count after anything moves.
DEFAULT_ROOTS = tuple(
    os.path.join(REPOSITORY, one)
    for one in (
        "README.md",
        "CMakeLists.txt",
        "docs",
        "src",
        "test",
        "tools",
        "vendor",
        "examples",
        "cmake",
        os.path.join("include", "protocore.h"),
        os.path.join("include", "CMakeLists.txt"),
    )
)

for one in DEFAULT_ROOTS:
    if not os.path.exists(one):
        raise SystemExit(
            "docs_check: %s is listed as a prose root and does not exist. A missing "
            "root reads as zero findings and exits 0, which passes every commit." % one
        )


# The environment a git query runs under, with the caller's own repository passed off.
#
# Git EXPORTS GIT_DIR and GIT_WORK_TREE to a hook. A rev-parse that inherits them answers about that
# repository instead of about the directory it was asked from. --show-toplevel returns the hook's
# own checkout as the root of whatever tree this tool was pointed at. Clearing the variables and
# picking the right rev-parse flag are two halves of one repair: either alone leaves a correct
# query giving a wrong answer under a hook. Every git query in this package goes through here, so
# there is one place to add the next variable to.
GIT_HANDOFF = (
    "GIT_DIR",
    "GIT_WORK_TREE",
    "GIT_INDEX_FILE",
    "GIT_PREFIX",
    "GIT_COMMON_DIR",
    "GIT_OBJECT_DIRECTORY",
    "GIT_ALTERNATE_OBJECT_DIRECTORIES",
    "GIT_NAMESPACE",
)


def git_env():
    """A copy of the environment with every variable naming somebody else's repository removed."""
    retained = dict(os.environ)
    for one in GIT_HANDOFF:
        retained.pop(one, None)
    return retained


def git_say(where, args):
    """One git command's stdout from `where`, stripped, or None where git cannot answer.

    None covers three cases a caller treats alike: git is not installed, the directory is not a
    checkout, and the command failed. A report that cannot name a revision says so; it does not
    guess one.
    """
    if not os.path.isdir(where):
        return None
    try:
        answer = subprocess.check_output(["git"] + list(args), cwd=where, stderr=subprocess.PIPE, env=git_env())
    except (OSError, subprocess.CalledProcessError):
        return None
    return answer.decode("utf-8", "replace").strip()


def main_checkout():
    """The main working tree.

    A linked worktree can live inside the main checkout, and a sibling path computed from
    REPOSITORY lands inside that checkout and finds nothing. --git-common-dir names the shared .git for
    the main tree and every linked worktree alike, and its parent is the main checkout. Falls back
    to REPOSITORY where git cannot answer, which is an exported tree with no history.
    """
    common = git_say(REPOSITORY, ("rev-parse", "--git-common-dir"))
    if not common:
        return REPOSITORY
    if not os.path.isabs(common):
        common = os.path.join(REPOSITORY, common)
    base = os.path.dirname(os.path.abspath(common))
    return base if os.path.isdir(base) else REPOSITORY
