#!/usr/bin/env python3
# repotools-stamp: lib/repotools/cli.py 7706456fc9bb5f2c
# repo_tools - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
"""One entry point for the toolkit, so a tool is findable without knowing where it sits.

    repotools sets                 the tool sets this toolkit offers
    repotools list                 every tool, by set, with the line its docstring opens with
    repotools fetch [set ...]      copy this repository's sets in from the toolkit, write the lock
    repotools check                report a fetched file edited here, or one the toolkit moved past
    repotools adopt <path> <set>   promote one of this repository's tools into the toolkit
    repotools inventory <dir> ...  which tool is written more than once across these trees
    repotools hooks install        point git at the toolkit's hook driver
    repotools run <tool> [args]    run one tool by name, wherever it sits
    repotools config               what this repository's repotools.toml resolves to

Every tool stays runnable by its own path. This entry point exists to make a tool findable, and a
tool reachable only through here would break the moment somebody moved it.
"""

import os
import sys

from repotools import boot, config, fetch, findings

# Every set the toolkit offers, including the importable ones under lib/. code/code_verify was
# missing, so the best-graded tool in the tree was invisible to sets, list and fetch. An empty set
# is listed with a zero beside it instead of being hidden, because a set nobody can see is a set
# somebody rebuilds.
SETS = (
    "lib/repotools",
    "code/code_maint",
    "code/code_verify",
    "data/data_fetch",
    "docs/docs_gen",
    "docs/docs_maint",
    "docs/docs_setup",
    "lib/numerics",
    "lib/retrieval",
    "media_tools",
    "repo/repo_maint",
)

# A set that cannot work without another one. Fetching media_tools alone gave viewers whose import
# of numerics failed at run time, and the README documented the dependency while nothing enforced it.
SET_NEEDS = {
    "media_tools": ("lib/numerics",),
    "data/data_fetch": ("lib/retrieval",),
}


def _first_line(path):
    """What this file says it does, for the listing. Empty where it says nothing.

    A file whose header is a `#` comment block instead of a docstring used to be described by the
    first docstring anywhere in it, which is some inner function's. `write_survey.py` listed itself
    as "A path expression reduced to text". Taking the header comment first, and only then a
    docstring, describes the file instead of a random part of it.
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
        if not stripped:
            break
        if not stripped.startswith("#"):
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


def cmd_fetch(argv):
    cfg = config.load()
    dry = "--dry" in argv
    sets = [one for one in argv if not one.startswith("-")]
    done = fetch.fetch(cfg, sets or None, dry=dry)
    for source, installed, action in done:
        if action != "same":
            print("  %-8s %s" % (action, installed))
    moved = sum(1 for _s, _i, action in done if action != "same")
    print("  %d file(s) in the lock, %d changed%s" % (len(done), moved, " (dry run)" if dry else ""))
    return 0


def cmd_check(_argv):
    cfg = config.load()
    # A repository with no lock fetched nothing, so there is nothing here to be wrong about. That is
    # a different state from a lock with entries none of which could be read, and only the second
    # one is a failure. Collapsing them made the toolkit's own gate refuse its own commit.
    if not fetch.read_lock(cfg):
        print("  %s fetches nothing from the toolkit, so no fetched tool was checked." % cfg.project_name())
        return findings.EXIT_OK
    report = findings.Report("fetched tools")
    fetch.check(cfg, report)
    return report.done()


def cmd_adopt(argv):
    rest = [one for one in argv if not one.startswith("-")]
    if len(rest) < 2:
        raise SystemExit("repotools adopt <path> <set>. `repotools sets` lists the sets.")
    source, into = rest[0], rest[1]
    dry = "--dry" in argv
    target, traces = fetch.adopt(source, into, dry=dry)
    print("  %s -> %s%s" % (source, target, " (dry run)" if dry else ""))
    if traces:
        print("  still answers for itself what a Config should answer:")
        for line, what in traces:
            print("    %s:%d: %s" % (source.replace("\\", "/"), line, what))
        print("  The promotion is not finished until those read from a Config.")
    return 0


def cmd_inventory(argv):
    tool = os.path.join(boot.toolkit_root(), "repo", "repo_maint", "inventory.py")
    sys.argv = [tool, "candidates"] + list(argv)
    namespace = {"__file__": tool, "__name__": "__main__"}
    with open(tool, encoding="utf-8") as handle:
        code = handle.read()
    try:
        exec(compile(code, tool, "exec"), namespace)
    except SystemExit as stop:
        return stop.code or 0
    return 0


def cmd_prose(argv):
    """Run the prose checker over this repository, resolving it without naming a path.

    This is the invocation a writing standard cites. The checker has moved twice, and each move left
    a stale path inside a skill that nobody noticed until somebody tried to run it. A stable name
    resolved at run time is what stops that happening a third time.
    """
    import subprocess

    from repotools import config as _config

    cfg = _config.load(required=False)
    where = cfg.where if cfg else boot.toolkit_root()
    settings = cfg.gate("docs_check") if cfg else {}

    tried = []
    named = settings.get("tool")
    if named:
        tried.append(named if os.path.isabs(named) else os.path.join(where, named))
    from_env = os.environ.get("REPOTOOLS_DOCS_CHECK")
    if from_env:
        tried.append(from_env)
    tried.append(os.path.join(boot.toolkit_root(), "no_replicate_", "prose_detection", "docs_check.py"))

    for candidate in tried:
        if os.path.isfile(candidate):
            return subprocess.call([sys.executable, candidate] + list(argv))

    print("  repotools prose: no checker found. Looked at:")
    for candidate in tried:
        print("    %s" % candidate)
    print("  Name one under [hooks.docs_check] tool, or set REPOTOOLS_DOCS_CHECK.")
    print("  An unreachable checker is an unchecked commit, so this is a refusal.")
    return findings.EXIT_BREAKING


def cmd_config(_argv):
    cfg = config.load()
    print("  root      %s" % cfg.where)
    print("  project   %s %s" % (cfg.project_name(), cfg.version()))
    print("  spdx      %s" % (cfg.spdx() or "(none)"))
    print("  source    %s" % ", ".join(cfg.rel(one) for one in cfg.source_roots()))
    print("  docs      %s" % ", ".join(cfg.rel(one) for one in cfg.docs_roots()))
    print("  code ext  %s" % " ".join(cfg.code_extensions()))
    print("  gates     %s" % (", ".join(cfg.gates()) or "(none)"))
    print("  sets      %s" % (", ".join(cfg.fetch_sets()) or "(none)"))
    return 0


def cmd_hooks(argv):
    if not argv or argv[0] != "install":
        raise SystemExit("repotools hooks install")
    cfg = config.load()
    where = os.path.join(cfg.tools_root(), cfg.data.get("fetch", {}).get("into", "repotools"))
    hooks = os.path.join(where, "repo", "repo_maint", "hooks")
    if not os.path.isdir(hooks):
        raise SystemExit(
            "repotools: no hook directory at %s. Run `repotools fetch repo/repo_maint` first." % hooks
        )
    print("  git config core.hooksPath %s" % cfg.rel(hooks))
    print("  Run that in %s. Pointing git at the tracked directory means what runs is what is tracked." % cfg.where)
    return 0


COMMANDS = {
    "sets": cmd_sets,
    "list": cmd_list,
    "fetch": cmd_fetch,
    "check": cmd_check,
    "adopt": cmd_adopt,
    "inventory": cmd_inventory,
    "config": cmd_config,
    "hooks": cmd_hooks,
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
