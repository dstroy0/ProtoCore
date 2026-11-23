#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The shared layer that each tool under tools/repotools reaches, and the only copy of it.

A tool here holds no project name, no license expression, no source root and no file extension. It
asks this package for them. The answer comes from the `repotools.toml` at the root of the repository
that holds the tool.

    from repotools import config

    cfg = config.load()             # walks up from the working directory for repotools.toml
    cfg.project_name()              # "ProtoCore"
    cfg.source_roots()              # absolute paths, from [layout] source
    cfg.code_extensions()           # (".c", ".h")

Import it with the preamble in `boot.py`, which walks up to `lib/` and does not count directories.
"""

__all__ = ["boot", "cli", "config", "findings", "root", "shape"]
