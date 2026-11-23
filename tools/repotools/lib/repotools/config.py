#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The per-repository settings a tool reads instead of carrying a copy of them.

`repotools.toml` sits at the root of the repository. It answers four questions: what the project is
called, what license line its files carry, where its source lives, and which file extensions count.

    from repotools import config

    cfg = config.load()                   # walks up from the working directory
    cfg = config.load("src/carcer.c")     # walks up from a path

    cfg.project_name()                    # "ProtoCore"
    cfg.header()                          # the two comment lines a generated file opens with
    cfg.source_roots()                    # absolute paths
    cfg.code_extensions()                 # (".c", ".h")
    cfg.is_excluded(path)                 # build, site, vendor and whatever else the repo lists

Every setting has a default here, so a repository states what differs from the defaults and stays
short. `defaults.toml` beside this file holds them, and a reader comparing a repository's file
against it sees the repository's decisions on one screen.

WHAT DOES NOT BELONG IN THIS FILE

A setting only one repository will ever set is a setting that repository keeps for itself. The line
between them is whether a second repository would answer the question differently. `[project] name`
qualifies. A Salishan paper's font repair table does not, and it stays in the repository that reads
it.
"""

import os
import tomllib

from repotools import root

CONFIG_NAME = "repotools.toml"


# Lists a repository is EXTENDING rather than replacing. Naming them, instead of appending every
# list, because the two intents are genuinely different: a repository adding one directory to the
# exclusions means "as well as the defaults", and a repository stating [layout] source means
# "these, not the defaults". Guessing either way is wrong half the time.
EXTENDED_LISTS = (("code", "exclude"), ("code", "skip_files"))


def _merge(base, over, path=()):
    """`over` wins key by key, a nested table merges, and the named lists extend.

    A repository that sets one key under `[layout]` keeps the defaults for the rest of that table. A
    merge that replaces the table loses `source` and `tests` when `[layout] docs` is set, and the tool
    that reads it then finds zero files and exits 0.

    A list has the same hazard. One entry added to `[code] exclude` must not discard the defaults, or a
    walk goes into `build` and `node_modules`. The lists in EXTENDED_LISTS take the union with the
    defaults, in order, and each other list replaces its default.
    """
    out = dict(base)
    for key, value in over.items():
        here = path + (key,)
        if isinstance(value, dict) and isinstance(out.get(key), dict):
            out[key] = _merge(out[key], value, here)
        elif here in EXTENDED_LISTS and isinstance(value, list) and isinstance(out.get(key), list):
            seen = list(out[key])
            seen.extend(one for one in value if one not in seen)
            out[key] = seen
        else:
            out[key] = value
    return out


def _read(path):
    with open(path, "rb") as handle:
        return tomllib.load(handle)


class Config:
    """One repository's settings, already merged over the toolkit defaults.

    `where` is the repository root, so a tool holding a Config needs no second path to work from.
    """

    def __init__(self, where, data):
        self.where = where
        self.data = data

    # --- identity -------------------------------------------------------------

    def project_name(self):
        """The name a generated header line carries. Required; there is no sensible default."""
        name = self.data.get("project", {}).get("name")
        if not name:
            raise SystemExit("repotools: %s/%s sets no [project] name" % (self.where, CONFIG_NAME))
        return name

    def version(self):
        """The version string, or an empty string where the repository does not stamp one."""
        return self.data.get("project", {}).get("version", "")

    def spdx(self):
        """The SPDX license expression, exactly as it goes into a file header."""
        return self.data.get("project", {}).get("spdx", "")

    def copyright_holder(self):
        return self.data.get("project", {}).get("copyright", "")

    def prefix(self):
        """The macro and public symbol prefix, upper case, without a trailing underscore."""
        return self.data.get("project", {}).get("prefix", "")

    def header(self):
        """The comment lines a file generated into this repository opens with.

        Returns a list of lines without their comment marker, so a caller writing Python prefixes
        each with `# ` and a caller writing C wraps them in the block form. The name and version
        travel together because a stamped file with no version reads as current forever.
        """
        stamped = self.project_name()
        if self.version():
            stamped = "%s v%s" % (stamped, self.version())
        lines = ["%s - %s" % (stamped, self.copyright_holder())] if self.copyright_holder() else [stamped]
        if self.spdx():
            lines.append("SPDX-License-Identifier: %s" % self.spdx())
        return lines

    # --- layout ---------------------------------------------------------------

    def _roots(self, key):
        listed = self.data.get("layout", {}).get(key, [])
        return tuple(os.path.join(self.where, one.replace("/", os.sep)) for one in listed)

    def source_roots(self):
        return self._roots("source")

    def docs_roots(self):
        return self._roots("docs")

    def example_roots(self):
        return self._roots("examples")

    def test_roots(self):
        """Where checks live. A collection of zero from these is a defect, never a clean run."""
        return self._roots("tests")

    def fixture_roots(self):
        """Where test DATA lives. Read by a check, never collected as one.

        Held apart from `tests` so a zero collection under `tests` means something. Under one key, a
        directory of fixtures only reports no tests and exits 5, and nothing in it is wrong to fix.
        """
        return self._roots("fixtures")

    def scratch_root(self):
        """The throwaway directory, absolute. Anything generated that nobody edits goes under it."""
        return os.path.join(self.where, self.data.get("layout", {}).get("scratch", "build").replace("/", os.sep))

    def tools_root(self):
        """The tools directory of this repository, from [layout] tools, absolute."""
        return os.path.join(self.where, self.data.get("layout", {}).get("tools", "tools").replace("/", os.sep))

    def existing(self, roots):
        """The subset of `roots` present on disk, and a refusal when every one of them is absent.

        A root that does not exist gives zero files and lets a run exit 0, so a check reads fewer files
        than it must and still reports success.
        """
        found = [one for one in roots if os.path.exists(one)]
        if roots and not found:
            raise SystemExit(
                "repotools: none of these roots exist, so a run over them would check nothing:\n    %s"
                % "\n    ".join(roots)
            )
        return tuple(found)

    # --- file selection -------------------------------------------------------

    def code_extensions(self):
        return tuple(self.data.get("code", {}).get("extensions", []))

    def prose_extensions(self):
        return tuple(self.data.get("prose", {}).get("extensions", []))

    def excluded_names(self):
        """Directory names skipped anywhere in a walk, by name and not by path.

        By name, because `build/` appears at the root and again under `test/`, and a repository
        listing both paths forgets the third one.
        """
        return tuple(self.data.get("code", {}).get("exclude", []))

    def is_excluded(self, path):
        parts = os.path.abspath(path).replace(os.sep, "/").split("/")
        return any(name in parts for name in self.excluded_names())

    def walk(self, roots, extensions):
        """Every file under `roots` carrying one of `extensions`, sorted, exclusions applied.

        Sorted, so two runs over an unchanged tree print their findings in the same order and a
        difference between two reports is a difference in the tree.
        """
        out = []
        for one in self.existing(roots):
            if os.path.isfile(one):
                if one.endswith(extensions):
                    out.append(one)
                continue
            for here, dirs, names in os.walk(one):
                dirs[:] = sorted(name for name in dirs if name not in self.excluded_names())
                for name in sorted(names):
                    if name.endswith(extensions):
                        out.append(os.path.join(here, name))
        return sorted(out)

    def walk_all(self, roots=None):
        """Every file under `roots`, with no extension filter at all.

        For any check whose question is coverage: did a rename reach every site, is this reference
        still valid, did I look everywhere. Naming the extensions to read decides in advance where a
        defect cannot be, and that decision is the defect.

        A sweep that filters on `*.py`, `*.md`, `*.tex` and `*.yml` misses a git hook, because a hook
        such as `pre-commit` has no extension. A reference in it then goes unchanged.

        `[code] extensions` selects a parser. It must never select a scope.
        """
        out = []
        for one in self.existing(roots or (self.where,)):
            if os.path.isfile(one):
                out.append(one)
                continue
            for here, dirs, names in os.walk(one):
                dirs[:] = sorted(
                    name
                    for name in dirs
                    if name not in self.excluded_names()
                    # A directory holding its own `.git` is another repository checked out inside
                    # this one: a submodule, or a vendored dependency. Its content is maintained
                    # upstream and its claims are not this tree's. A citation scan without this rule
                    # reported a vendored submodule's reference as something this repository needed.
                    and not os.path.exists(os.path.join(here, name, ".git"))
                )
                for name in sorted(names):
                    out.append(os.path.join(here, name))
        return sorted(out)

    # --- gates ----------------------------------------------------------------

    def gates(self):
        """The checks this repository's pre-commit hook runs, in the order it runs them."""
        return tuple(self.data.get("hooks", {}).get("gates", []))

    def gate(self, name):
        """One gate's settings table, empty where the repository states none."""
        return self.data.get("hooks", {}).get(name, {})

    # --- paths ----------------------------------------------------------------

    def at(self, *parts):
        return os.path.join(self.where, *parts)

    def rel(self, path):
        return root.rel(self.where, path)


def defaults():
    """The toolkit's own defaults, read from `defaults.toml` beside this module."""
    return _read(os.path.join(os.path.dirname(os.path.abspath(__file__)), "defaults.toml"))


def load(start=None, required=True):
    """The Config for the repository above `start`, defaults merged under it.

    Returns None where no repository root is above `start` and `required` is false, so a tool can
    offer a plain message instead of a traceback.
    """
    where = root.find(start, required=required)
    if where is None:
        return None
    return Config(where, _merge(defaults(), _read(os.path.join(where, CONFIG_NAME))))
