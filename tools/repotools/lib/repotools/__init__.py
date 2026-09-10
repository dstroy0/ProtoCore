#!/usr/bin/env python3
# repotools-stamp: lib/repotools/__init__.py 0b8196993556ca3f
# repo_tools - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
"""The shared layer every tool in this toolkit reaches, and the only copy of it.

A tool here holds no project name, no license expression, no source root and no file extension. It
asks this package for them, and the answer comes from the `repotools.toml` sitting at the root of
whichever repository the tool was pointed at. Those four settings were the only difference between
the eight copies of `codemask.py` this replaces.

    from repotools import config

    cfg = config.load()             # walks up from the working directory for repotools.toml
    cfg.project_name()              # "MMgr"
    cfg.source_roots()              # absolute paths, from [layout] source
    cfg.code_extensions()           # (".c", ".h")

Import it with the walk-up preamble documented in `boot.py`. Counting directories to `lib/` fixes a
tool's distance from the toolkit root, and thirty nine scripts in anchor_sift broke at once the last
time a directory moved under a tree that counted.
"""

__all__ = ["boot", "cli", "config", "fetch", "findings", "root", "shape"]
