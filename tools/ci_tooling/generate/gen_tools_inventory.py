#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Regenerate the derived tables in tools/TOOLS.md.

The tables are what each script under tools/ actually is: the flags it reads, whether it holds a
write primitive, and the external commands it names. tools/harness.py owns that scan, because the
same data is what `harness.py list` prints - one reader, so the doc and the console cannot
disagree. This file is that call under the name CI knows it by, so the region is gated the way
every other generated region is: `ci.py gen --check tools_inventory`.

Usage:  python -m tools.ci_tooling.generate.gen_tools_inventory [--check]
"""

import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)

from tools import harness  # noqa: E402  (path set above)


def main():
    return harness.doc_gen(check="--check" in sys.argv)


if __name__ == "__main__":
    sys.exit(main())
