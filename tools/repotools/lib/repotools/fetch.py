#!/usr/bin/env python3
# repotools-stamp: lib/repotools/fetch.py 95b0ba6478fef338
# repo_tools - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
"""Moving tools between this toolkit and the repositories that use it, in both directions.

The toolkit is upstream. A repository fetches from it and never pushes back by editing a fetched
file in place. Where a repository has a tool worth sharing, it is promoted here deliberately with
`adopt`, and every other repository then fetches the promoted copy.

    fetch(cfg)        toolkit -> repository. Copies the sets the repository asked for, stamps each
                      file, writes repotools.lock.
    check(cfg)        reports a fetched file edited in the repository, and one the toolkit has moved
                      past. The first refuses. The second is a note saying a fetch is due.
    adopt(path)       repository -> toolkit. Brings one file in, records where it came from, and
                      reports what still carries that repository's name.

WHAT THE LOCK COVERS, AND WHAT IT IGNORES

`repotools.lock` lists the files that came from here. A repository's own tools are its own, and
nothing in this module reads them, reports on them or has an opinion about them. The lock is the
boundary: inside it the toolkit is the source, outside it the repository is.

THE STAMP, NOT THE PATH

Each fetched file carries a stamp line naming the toolkit path it came from and the digest of the
content that was installed. Identity is the stamp. A repository that moves a fetched file into a
different directory still matches, because the check reads the stamp and never infers identity from
where the file sits. The alternative was measured next door: a registry that matched examples on
path alone read a move as one example retired and another created, which burned the number and broke
every citation to it.

THE HEADER STAYS THIS TOOLKIT'S

A fetched file keeps the repo_tools copyright and license header. It is this toolkit's code running
in another tree. The fetching project's name, version and license reach the tool at runtime through
its own `repotools.toml`, which is the entire reason the eight copies of `codemask.py` differed.
"""

import hashlib
import os
import shutil

from repotools import boot, root

LOCK_NAME = "repotools.lock"

# The importable spine. Every runnable tool walks up for a directory holding it and imports from it,
# so it travels with every fetch whether or not a repository named it.
SPINE = "lib/repotools"

# The comment marker each extension takes. A file whose extension is absent here is copied and
# locked without a stamp, and the check falls back to matching it by its recorded path.
MARKERS = {
    ".py": "#",
    ".sh": "#",
    ".ps1": "#",
    ".toml": "#",
    ".yml": "#",
    ".yaml": "#",
    ".cfg": "#",
    ".c": "//",
    ".h": "//",
    ".cpp": "//",
    ".hpp": "//",
    ".cu": "//",
    ".js": "//",
    ".cjs": "//",
}


def digest(text):
    """Sixteen hex characters of the SHA-256 of `text`, newlines normalized.

    Normalized, because a checkout on Windows and one on Linux hold the same file with different
    line endings, and a check that called those two files different would fire on every clone.
    """
    return hashlib.sha256(text.replace("\r\n", "\n").encode("utf-8")).hexdigest()[:16]


def strip_stamp(text):
    """`text` without its stamp line, which is what the digest is taken over.

    The digest covers the file as it left the toolkit. Taking it over the stamped copy would make
    the stamp cover itself, and no two stamps could ever agree.
    """
    return "\n".join(line for line in text.splitlines() if STAMP not in line)


def read_stamp(text):
    """The `(source_path, digest)` a stamped file carries, or None where it carries no stamp."""
    for line in text.splitlines():
        if STAMP in line:
            parts = line.split(STAMP, 1)[1].split()
            if len(parts) >= 2:
                return parts[0], parts[1]
    return None


def apply_stamp(text, source_path, marker):
    """`text` with a stamp line inserted, below a shebang where one is present.

    Below the shebang, because a `#!` line that is not the first line of a file stops being a
    shebang, and the tool then runs under whatever shell happened to call it.
    """
    stamped = "%s %s %s %s" % (marker, STAMP, source_path, digest(text))
    lines = text.splitlines()
    at = 1 if lines and lines[0].startswith("#!") else 0
    lines.insert(at, stamped)
    return "\n".join(lines) + "\n"


def _lock_path(cfg):
    return os.path.join(cfg.where, LOCK_NAME)


def read_lock(cfg):
    """The lock as `{installed_relative_path: (source_path, digest)}`, empty where there is none.

    Parsed by hand instead of through tomllib, because the lock is written by this module and read
    by this module, and a three column text file is legible in a diff while a TOML table of hashes
    is not.
    """
    path = _lock_path(cfg)
    if not os.path.isfile(path):
        return {}
    out = {}
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) == 3:
                out[parts[0]] = (parts[1], parts[2])
    return out


def write_lock(cfg, entries):
    """Write the lock, sorted, so two fetches over an unchanged toolkit produce an identical file."""
    lines = [
        "# repotools.lock - written by `repotools fetch`. Do not edit.",
        "#",
        "# installed_path    toolkit_source_path    digest",
        "#",
        "# Every file here came from the repo_tools checkout and is maintained there. A file absent",
        "# from this list belongs to this repository and the toolkit never reads it.",
        "",
    ]
    for installed in sorted(entries):
        source, dig = entries[installed]
        lines.append("%s %s %s" % (installed, source, dig))
    with open(_lock_path(cfg), "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")


def _set_files(toolkit, one_set):
    """Every file under one toolkit set, as `(absolute_path, toolkit_relative_path)`."""
    base = os.path.join(toolkit, one_set.replace("/", os.sep))
    if not os.path.isdir(base):
        raise SystemExit(
            "repotools: no tool set named %s in the toolkit at %s. `repotools sets` lists them." % (one_set, toolkit)
        )
    out = []
    for here, dirs, names in os.walk(base):
        dirs[:] = sorted(name for name in dirs if name not in ("__pycache__", ".git"))
        for name in sorted(names):
            if name.endswith(".pyc"):
                continue
            full = os.path.join(here, name)
            out.append((full, root.rel(toolkit, full)))
    return out


def fetch(cfg, sets=None, dry=False):
    """Copy the repository's tool sets in from the toolkit and write the lock.

    Returns the list of `(toolkit_path, installed_path, action)` it performed, where action is one
    of `new`, `updated` or `same`, so a caller prints what moved instead of claiming everything did.
    """
    toolkit = boot.toolkit_root()
    chosen = tuple(sets) if sets else cfg.fetch_sets()

    # The spine comes with every fetch, whether or not it was asked for. Every runnable tool opens
    # by walking up for a directory holding lib/repotools and then imports from it, so a fetch
    # without it installs tools that cannot import. The documented join sequence in the README named
    # code/code_maint alone and produced exactly that: dedup.py landed, its preamble walked to the
    # filesystem root, and `from repotools import config` raised ModuleNotFoundError.
    if chosen and SPINE not in chosen:
        chosen = (SPINE,) + tuple(one for one in chosen if one != SPINE)

    if not chosen:
        raise SystemExit(
            "repotools: %s names no tool sets under [fetch] sets, so a fetch would copy nothing."
            % os.path.join(cfg.where, "repotools.toml")
        )

    into = os.path.join(cfg.tools_root(), cfg.data.get("fetch", {}).get("into", "repotools"))
    entries = read_lock(cfg)
    done = []

    # The lock is rebuilt from the sets asked for. A file the toolkit dropped since the last fetch
    # leaves the lock with it, and the check then stops reporting a file nobody is maintaining.
    keep = {}

    for one_set in chosen:
        for full, source_rel in _set_files(toolkit, one_set):
            with open(full, encoding="utf-8", errors="replace") as handle:
                text = handle.read()
            marker = MARKERS.get(os.path.splitext(full)[1])
            body = strip_stamp(text)
            dig = digest(body)
            target = os.path.join(into, source_rel.replace("/", os.sep))
            installed_rel = root.rel(cfg.where, target)

            was = entries.get(installed_rel)
            action = "new" if was is None else ("same" if was[1] == dig else "updated")
            keep[installed_rel] = (source_rel, dig)
            done.append((source_rel, installed_rel, action))

            if dry or action == "same":
                continue
            os.makedirs(os.path.dirname(target), exist_ok=True)
            if marker:
                with open(target, "w", encoding="utf-8", newline="\n") as handle:
                    handle.write(apply_stamp(body, source_rel, marker))
            else:
                shutil.copyfile(full, target)

    if not dry:
        write_lock(cfg, keep)
    return done


def check(cfg, report):
    """Report a fetched file edited in place, and one the toolkit has moved past.

    A local edit is breaking. The toolkit is upstream, so an edit here is a change that exists in
    one repository and is lost the next time anything fetches. Where the edit is worth keeping, it
    is promoted with `adopt` and flows back out to every repository.

    A file the toolkit has changed since the fetch is a note. It says a fetch is due, and it refuses
    nothing, because a repository is entitled to sit on a known version until it chooses to move.
    """
    toolkit = boot.toolkit_root()
    locked = read_lock(cfg)
    if not locked:
        return report

    for installed_rel, (source_rel, dig) in sorted(locked.items()):
        installed = os.path.join(cfg.where, installed_rel.replace("/", os.sep))
        report.saw()

        if not os.path.isfile(installed):
            report.breaking(installed_rel, 1, "locked as fetched from %s and is not on disk" % source_rel)
            continue

        with open(installed, encoding="utf-8", errors="replace") as handle:
            here_text = handle.read()
        if digest(strip_stamp(here_text)) != dig:
            report.breaking(
                installed_rel,
                1,
                "edited since it was fetched from %s. The toolkit is upstream: move the change there "
                "with `repotools adopt` and fetch it back." % source_rel,
            )
            continue

        upstream = os.path.join(toolkit, source_rel.replace("/", os.sep))
        if not os.path.isfile(upstream):
            report.note(installed_rel, 1, "no longer exists in the toolkit at %s" % source_rel)
            continue
        with open(upstream, encoding="utf-8", errors="replace") as handle:
            upstream_dig = digest(strip_stamp(handle.read()))
        if upstream_dig != dig:
            report.note(installed_rel, 1, "the toolkit has a newer %s. Run `repotools fetch`." % source_rel)

    return report


def adopt(source, into_set, name=None, dry=False):
    """Promote one file from a repository into the toolkit, and report what still names that repo.

    The promoted copy keeps its body and takes the toolkit's own header. What it cannot take is a
    project name, a macro prefix or a hard coded root left in the body, so those are reported by
    line and the promotion is not finished until they read from a Config.
    """
    toolkit = boot.toolkit_root()
    target_dir = os.path.join(toolkit, into_set.replace("/", os.sep))
    if not os.path.isdir(target_dir):
        raise SystemExit("repotools: no tool set named %s in the toolkit" % into_set)

    with open(source, encoding="utf-8", errors="replace") as handle:
        text = handle.read().replace("\r\n", "\n")
    text = retitle(text, MARKERS.get(os.path.splitext(source)[1], "#"))
    target = os.path.join(target_dir, name or os.path.basename(source))

    if not dry:
        with open(target, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)

    return root.rel(toolkit, target), project_traces(text)


TOOLKIT_HEADER = (
    "repo_tools - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>",
    "SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational",
)


def retitle(text, marker):
    """Swap the promoted file's header for the toolkit's own, written with `marker`.

    A promoted tool is this toolkit's code running in another tree, so it carries this toolkit's
    copyright and license. The header was the largest measured difference between the copies: four
    copies of `codemask.py` differed in the project name and the license expression, and in nothing
    at all besides.

    Only the copyright and license lines are replaced. A comment below them belongs to the tool and
    is left where it is, because a promotion that ate a file's first paragraph would be discovered
    one file at a time over the following year.
    """
    lines = text.split("\n")
    at = 1 if lines and lines[0].startswith("#!") else 0
    end = at
    while end < len(lines) and lines[end].startswith(marker):
        if "Copyright (C)" in lines[end] or "SPDX-License-Identifier" in lines[end]:
            end += 1
            continue
        break
    header = ["%s %s" % (marker, one) for one in TOOLKIT_HEADER]
    return "\n".join(lines[:at] + header + lines[end:])


# A promoted file carrying any of these is still a copy of one repository's tool. Each is a question
# the file has to ask a Config instead of answering for itself.
TRACES = (
    ("hard coded repository root", ("os.getcwd()", 'sys.path.insert(0, os.path.join(ROOT,')),
    ("counted parent directories", ("os.path.dirname(os.path.dirname(os.path.dirname(",)),
    ("a project name in the body", ("MMGR_", "PROTOCORE_", "IDEMIP_", "ANCHOR_SIFT")),
    ("a fixed source root", ('"src"', "'src'", '"tools/dev_env"')),
    ("a fixed license expression", ("SPDX-License-Identifier:",)),
    # A bare sibling import resolves only when the file is RUN, because Python puts a script's own
    # directory on sys.path then and not when it is loaded by path. A fetched tool is invoked from
    # a repository root, so the bare form fails in exactly the case this toolkit exists for. It
    # passed nsconv.py clean while that file carried one.
    ("a bare sibling import", ("from codemask import", "from strip_comments import", "from dedup import")),
    # Lowercase project names, which the upper-case needles above walk straight past. nsconv.py
    # carried a hard-coded DBENCH macro list and a lowercase mmgr_ name and was reported clean.
    ("a project name in the body", ("mmgr_", "protocore_", "idemip_", "anchor_sift", "DBENCH_")),
    # A tool that opens its own socket bypasses every courtesy rule in lib/retrieval: the identifying
    # User-Agent, robots.txt, the machine-wide rate floor, the cache. Retrieval goes through
    # retrieval.polite.Fetcher, and a promoted file reaching the network directly is unfinished.
    #
    # Socket-opening imports only. `subprocess` is deliberately absent: measured against a tree with
    # a known answer, one file opened a socket and three used subprocess to run a locally built
    # binary, a sibling script and a code generator. Listing subprocess would have flagged three
    # local-execution sites to catch one that this list already catches by its import. The egress
    # case for a spawned process is in the argv, so `curl` and `wget` are named and the module is not.
    (
        "reaches the network without going through retrieval.polite",
        ("urllib.request", "import requests", "import httpx", "import socket", "http.client", "ftplib", '"curl"', "'curl'", '"wget"', "'wget'"),
    ),
)


def project_traces(text):
    """Lines in a promoted file that still answer a question a Config should answer.

    Returns `(line_number, what)` pairs. The list is a floor: it catches the forms measured across
    the eight copies on this machine, and a ninth copy will find a form nobody has written yet.
    """
    out = []
    for number, line in enumerate(text.splitlines(), 1):
        # A header line states this file's license and is correct. A second one further down is a
        # project's license expression left inside the body.
        if number <= 3:
            continue
        for what, needles in TRACES:
            if any(needle in line for needle in needles):
                out.append((number, what))
    return out
