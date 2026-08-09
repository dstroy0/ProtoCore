#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Where the repo root is, for every script under src/, examples/, core_setup/, tools/ and test/.

Searches upward from this file for `library.json` + `src`. This file sits at the root, so the
answer does not depend on the caller's depth, its working directory, or how it was launched.

    import findroot

    findroot.root()             # absolute path to the repo root
    findroot.at("src", "mmgr")  # absolute path under it, native separators
    findroot.rel(abs_path)      # root-relative POSIX path, for keys and report lines
    findroot.here(__file__)     # the caller's own root-relative POSIX path

`rel` and `here` return forward slashes on every platform.

Run it directly to print the root:  python -m tools.findroot
"""

import os

# A directory is the root when it holds both.
ROOT_MARKERS = ("library.json", "src")


def _find():
    d = os.path.dirname(os.path.abspath(__file__))
    while True:
        if all(os.path.exists(os.path.join(d, m)) for m in ROOT_MARKERS):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            raise SystemExit("findroot: no root above %s (want %s)" % (__file__, " + ".join(ROOT_MARKERS)))
        d = parent


_ROOT = _find()


def root():
    """Absolute path to the repo root."""
    return _ROOT


def at(*parts):
    """Absolute path to `parts` under the root."""
    return os.path.join(_ROOT, *parts)


def rel(path):
    """Root-relative POSIX path."""
    return os.path.relpath(os.path.abspath(path), _ROOT).replace(os.sep, "/")


def here(caller_file):
    """Root-relative POSIX path of the calling script, from its `__file__`."""
    return rel(caller_file)


if __name__ == "__main__":
    print(_ROOT)
