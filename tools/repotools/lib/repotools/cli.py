#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""One entry point for the tools under tools/repotools, so a tool is findable without knowing where it sits.

    repotools sets      the tool sets under tools/repotools, and how many files each holds
    repotools list      every tool, by set, with the line its docstring opens with
    repotools config    what this repository's repotools.toml resolves to

Every tool stays runnable by its own path. This entry point makes a tool findable. A tool that only this
entry point can reach stops working when somebody moves it.
"""

import os
import sys

_at = os.path.dirname(os.path.abspath(__file__))
while _at != os.path.dirname(_at) and not os.path.isdir(os.path.join(_at, "lib", "repotools")):
    _at = os.path.dirname(_at)
sys.path.insert(0, os.path.join(_at, "lib"))

from repotools import boot, config  # noqa: E402

# The tool sets under tools/repotools. A set with no files shows a zero, so that each set stays visible.
SETS = (
    "lib/repotools",
    "lib/numerics",
    "code/code_verify",
    "media_tools",
)

NONE_SHOWN = "(none)"


def _first_line(path):
    """What this file says it does, for the listing. Empty where it says nothing.

    The first comment line past the license header comes first, and the first docstring only after it. A
    file with a comment header thus shows that header, and not the docstring of a function inside it.
    """
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            text = handle.read(4000)
    except OSError:
        return ""

    lines = text.split("\n")
    at = 1 if lines and lines[0].startswith("#!") else 0
    # Past the license header, to the first comment line that says something.
    for line in lines[at:]:
        stripped = line.strip()
        if not stripped or not stripped.startswith("#"):
            break
        said = stripped.lstrip("#").strip()
        if said and "Copyright (C)" not in said and "SPDX-License-Identifier" not in said:
            return said

    found = text.find('"""')
    if found < 0:
        return ""
    rest = text[found + 3 :].splitlines()
    return rest[0].strip() if rest else ""


def cmd_sets(_argv):
    toolkit = boot.toolkit_root()
    for one in SETS:
        where = os.path.join(toolkit, one.replace("/", os.sep))
        count = 0
        if os.path.isdir(where):
            for _here, dirs, names in os.walk(where):
                dirs[:] = [name for name in dirs if name != "__pycache__"]
                count += sum(1 for name in names if not name.endswith(".pyc"))
        print("  %-22s %d file(s)" % (one, count))
    return 0


def cmd_list(_argv):
    toolkit = boot.toolkit_root()
    for one in SETS:
        where = os.path.join(toolkit, one.replace("/", os.sep))
        if not os.path.isdir(where):
            continue
        print("\n%s" % one)
        for here, dirs, names in os.walk(where):
            dirs[:] = sorted(name for name in dirs if name != "__pycache__")
            for name in sorted(names):
                if name.endswith((".pyc", ".json")):
                    continue
                print("    %-26s %s" % (name, _first_line(os.path.join(here, name))))
    return 0


def cmd_config(_argv):
    cfg = config.load()
    print("  root      %s" % cfg.where)
    print("  project   %s %s" % (cfg.project_name(), cfg.version()))
    print("  spdx      %s" % (cfg.spdx() or NONE_SHOWN))
    print("  source    %s" % ", ".join(cfg.rel(one) for one in cfg.source_roots()))
    print("  docs      %s" % ", ".join(cfg.rel(one) for one in cfg.docs_roots()))
    print("  code ext  %s" % " ".join(cfg.code_extensions()))
    print("  gates     %s" % (", ".join(cfg.gates()) or NONE_SHOWN))
    return 0


COMMANDS = {
    "sets": cmd_sets,
    "list": cmd_list,
    "config": cmd_config,
}


def main(argv):
    if not argv or argv[0] in ("-h", "--help", "help"):
        print(__doc__)
        return 0
    name = argv[0]
    if name not in COMMANDS:
        raise SystemExit("repotools: no command named %s. `repotools help` lists them." % name)
    return COMMANDS[name](argv[1:])


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
