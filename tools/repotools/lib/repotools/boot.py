#!/usr/bin/env python3
# repotools-stamp: lib/repotools/boot.py 413320f955250bcb
# repo_tools - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
"""Finding the toolkit from inside it, and the preamble every tool copies to get here.

THE PREAMBLE

Every runnable script under this toolkit opens with these four lines, before any repotools import:

    import os, sys
    _at = os.path.dirname(os.path.abspath(__file__))
    while _at != os.path.dirname(_at) and not os.path.isdir(os.path.join(_at, "lib", "repotools")):
        _at = os.path.dirname(_at)
    sys.path.insert(0, os.path.join(_at, "lib"))

This is the single duplicated fragment in the toolkit, and it is duplicated because it is the code
that finds the shared code. Everything after it is imported once.

WHY IT WALKS

The alternative is counting:

    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))

Counting fixes a script's distance from the root, so the script breaks the day it moves one level.
Thirty nine scripts in the anchor_sift tree computed their root by counting, and sorting that tree
into categories moved every one of them and broke all thirty nine at once.

THE LOOP GUARD

`_at != os.path.dirname(_at)` stops the walk at the filesystem root. Without it, a script launched
from outside any checkout walks to `C:\\` or `/` and then loops on a directory that is its own
parent. The guard turns that into a clean import failure at a known line.
"""

import os

# A directory is the toolkit root when it holds all of these. Three markers instead of one, because
# `lib` alone matches most Python projects and `code` alone matches half the trees on this machine.
TOOLKIT_MARKERS = ("lib/repotools", "repo/repo_template", "code")


def toolkit_root(start=None):
    """Absolute path to the repo_tools checkout holding this package.

    Walks up from this file, or from `start` when one is given, until a directory carries every
    entry in TOOLKIT_MARKERS. Raises when the walk reaches the filesystem root, because a toolkit
    that cannot find itself has to fail at import and not at the first template read.
    """
    at = os.path.dirname(os.path.abspath(start or __file__))
    while True:
        if all(os.path.exists(os.path.join(at, marker.replace("/", os.sep))) for marker in TOOLKIT_MARKERS):
            return at
        parent = os.path.dirname(at)
        if parent == at:
            raise SystemExit(
                "repotools: no toolkit root above %s (want %s)" % (start or __file__, " + ".join(TOOLKIT_MARKERS))
            )
        at = parent


def toolkit_at(*parts):
    """Absolute path to `parts` under the toolkit root."""
    return os.path.join(toolkit_root(), *parts)
