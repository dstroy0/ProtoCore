#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The Doxygen input filter of the configuration headers. It writes the file named on the command line to stdout,
with each #ifndef PROTOCORE_ENABLE_ guard changed to #if 1, and each other line as it is.

docs/Doxyfile sets each PROTOCORE_ENABLE_ flag with :=, so each gate of a module reads 1 and the site documents
every module. A := flag stays 1 through a later #define. A guard of #ifndef on that flag reads false, and then
Doxygen does not see the default. With the guard changed, Doxygen documents the #define at its default value.
The line count stays the same, so each line number of a warning is the line of the file.

    python3 tools/ci_tooling/build/doxygen_config_filter.py src/protocore_config.h
"""

import re
import sys

GUARD = re.compile(r"^(\s*)#(\s*)ifndef\s+PROTOCORE_ENABLE_\w+\s*$")


def main(argv):
    with open(argv[0], encoding="utf-8", newline="") as fh:
        lines = fh.read().split("\n")
    out = [GUARD.sub(r"\1#\2if 1", line.rstrip("\r")) + ("\r" if line.endswith("\r") else "") for line in lines]
    sys.stdout.buffer.write("\n".join(out).encode("utf-8"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
