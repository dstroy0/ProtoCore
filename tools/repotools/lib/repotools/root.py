#!/usr/bin/env python3
# repotools-stamp: lib/repotools/root.py 677bb1f27a459dba
# repo_tools - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
"""Where the repository under maintenance is, for a tool that was handed a path and nothing else.

Every repository that adopts this toolkit carries `repotools.toml` at its root, and that file is
the marker. A tool asks for the root by walking up from wherever it was pointed:

    from repotools import root

    root.find()                     # walks up from the working directory
    root.find("src/mmgr/carcer.c")  # walks up from a path the caller was given
    root.at(where, "src")           # absolute path under a found root
    root.rel(where, path)           # root-relative POSIX path, for report lines and keys

Four repositories on this machine each invented a different marker: `library.json` plus `src` in
ProtoCore, `src/engine` in anchor_sift, a counted parent depth in thirty nine other scripts. One
marker across every repository means a tool moved between them keeps working, and a repository that
has not adopted the toolkit fails with a message naming what to add.

REPOTOOLS_ROOT overrides the walk, for a checkout laid out in a way the marker cannot reach.
"""

import os

CONFIG_NAME = "repotools.toml"


def find(start=None, required=True):
    """Absolute path to the repository root above `start`.

    `start` is a file or a directory, defaulting to the working directory. Returns None when no root
    is above it and `required` is false. Raises otherwise, naming the file to add, because a tool
    that silently takes the filesystem root as the repository reads zero files and reports success.
    """
    override = os.environ.get("REPOTOOLS_ROOT")
    if override:
        if os.path.isfile(os.path.join(override, CONFIG_NAME)):
            return os.path.abspath(override)
        raise SystemExit("repotools: REPOTOOLS_ROOT is %s and holds no %s" % (override, CONFIG_NAME))

    at = os.path.abspath(start or os.getcwd())
    if os.path.isfile(at):
        at = os.path.dirname(at)

    while True:
        if os.path.isfile(os.path.join(at, CONFIG_NAME)):
            return at
        parent = os.path.dirname(at)
        if parent == at:
            if not required:
                return None
            raise SystemExit(
                "repotools: no %s above %s. A repository joins the toolkit by carrying one at its "
                "root; copy repo/repo_template/repotools.toml and fill in [project]." % (CONFIG_NAME, start or os.getcwd())
            )
        at = parent


def at(where, *parts):
    """Absolute path to `parts` under the root `where`."""
    return os.path.join(where, *parts)


def rel(where, path):
    """Root-relative POSIX path, for report lines and lock keys.

    Forward slashes on every platform, so a lock written on Windows reads on Linux.
    """
    return os.path.relpath(os.path.abspath(path), where).replace(os.sep, "/")
