#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""How a tool under tools/repotools finds this package, and the preamble each tool holds to get here.

THE PREAMBLE

Each runnable script under tools/repotools opens with these lines, before any repotools import:

    import os, sys
    _at = os.path.dirname(os.path.abspath(__file__))
    while _at != os.path.dirname(_at) and not os.path.isdir(os.path.join(_at, "lib", "repotools")):
        _at = os.path.dirname(_at)
    sys.path.insert(0, os.path.join(_at, "lib"))

Each script holds its own copy, because this fragment is the code that finds the shared code. All code
after it comes from one import.

WHY IT WALKS

The other way counts directories:

    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))

A count fixes the distance of a script from the root, so the script stops working when it moves one
level. The walk finds the root from any depth.

THE LOOP GUARD

`_at != os.path.dirname(_at)` stops the walk at the filesystem root. Without it, a script outside the
tree walks to `C:\\` or `/`, and then loops on a directory that is its own parent. The guard makes that
an import failure at a known line.
"""

import os

# A directory is the root of tools/repotools when it holds both of these. `lib` alone matches most Python
# projects, and `code` alone matches many trees, so the root needs both.
TOOLKIT_MARKERS = ("lib/repotools", "code")


def toolkit_root(start=None):
    """Absolute path to the tools/repotools directory that holds this package.

    The walk goes up from this file, or from `start` when one is given, to a directory that holds each
    entry in TOOLKIT_MARKERS. It raises when it reaches the filesystem root, so a tree that cannot find
    itself fails at import, and not at the first read of a template.
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
