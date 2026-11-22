#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# A signed manifest, what it lists, and the command that reconciles it after a write.
#

import os



# --------------------------------------------------------------------
# SIGNED MANIFESTS
# --------------------------------------------------------------------
#
# The max dangerous exclusion in this file, and the only one whose cost is not a question of taste.
#
# A tree may carry a MANIFEST.tsv recording the SHA-256, the byte count and the row count of its
# files, with a detached signature beside it. Changing one byte of a listed file makes its hash
# wrong, fails the reconcile, and invalidates a signature whose whole purpose is to attest what a
# published measurement was taken over.
#
# A LISTED PATH IS THEREFORE ERROR FOR REWRITING AND NEVER QUIETLY PASSED OVER, and a run that
# offered to rewrite anything in a tree carrying a manifest prints that manifest's own reconcile
# instruction under its output. The instruction is lifted from the manifest header and not written
# out here. It cannot drift from the tool that maintains it.
#
# WORSE HERE THAN ANYWHERE ELSE. The corpus is Salishan linguistics: Canadian and British convention
# throughout, and this file's own measurements put neighbour at 17.3 per 100k there, analyse at 8.9,
# behaviour at 4.8, labelled at 2.9 and centre at 2.3. Those are the source texts' own convention,
# they are what the hashes were taken over, and an alphabet-stage rewrite would change thousands of
# them and break every hash it touched.
#
# READING IS NOT WRITING, and this rule does not stop the scan. A listed file is read, and a finding
# in one is reported like any other, because reporting changes no bytes. Only the rewrite is
# errored.
MANIFEST_NAMES = ("MANIFEST.tsv", "AUDIO_MANIFEST.tsv")
SIGNATURE_SUFFIX = ".asc"

_MANIFEST_CEILING = 24
_MANIFEST_DIRS = {}
_MANIFEST_INDEX = {}


def manifest_home(start):
    """The nearest directory at or above `start` holding a signed manifest, or None.

    Stops at a repository boundary. A manifest attests one repository's contents, and a parent
    directory holding several repositories beside each other is not that repository.
    """
    here = os.path.abspath(start if os.path.isdir(start) else os.path.dirname(start))
    walked = []
    answer = None
    for _ in range(_MANIFEST_CEILING):
        if here in _MANIFEST_DIRS:
            answer = _MANIFEST_DIRS[here]
            break
        walked.append(here)
        if any(os.path.isfile(os.path.join(here, one)) for one in MANIFEST_NAMES):
            answer = here
            break
        if os.path.isdir(os.path.join(here, ".git")) or os.path.isfile(
            os.path.join(here, ".git")
        ):
            break
        up = os.path.dirname(here)
        if up == here:
            break
        here = up
    for one in walked:
        _MANIFEST_DIRS[one] = answer
    return answer


def manifest_index(home):
    """{relative posix path: (manifest, signature or None)} for every manifest in one directory.

    The file is a comment header, one header row naming its columns, then one row per attested file
    with the path last. Read by taking the last tab-separated field. A manifest that grows a
    column still parses. A row with no tab is not a row.
    """
    if home in _MANIFEST_INDEX:
        return _MANIFEST_INDEX[home]

    held = {}
    for name in MANIFEST_NAMES:
        where = os.path.join(home, name)
        if not os.path.isfile(where):
            continue
        signature = where + SIGNATURE_SUFFIX
        signed = signature if os.path.isfile(signature) else None
        with open(where, encoding="utf-8", errors="replace") as handle:
            for line in handle:
                line = line.rstrip("\n").rstrip("\r")
                if line.startswith("#") or ("\t" not in line):
                    continue
                listed = line.split("\t")[-1].strip()
                # The header row names the columns and attests nothing.
                if (not listed) or (listed == "path"):
                    continue
                held[listed.replace("\\", "/")] = (where, signed)
    _MANIFEST_INDEX[home] = held
    return held


def manifest_listed(path):
    """(manifest, signature, listed path) where this file's bytes are attested, else None."""
    home = manifest_home(path)
    if not home:
        return None
    listed = os.path.relpath(os.path.abspath(path), home).replace(os.sep, "/")
    held = manifest_index(home).get(listed)
    if not held:
        return None
    return (held[0], held[1], listed)


def reconcile_command(manifest):
    """The manifest's own instruction for reconciling a tree against it.

    Lifted from the manifest header instead of written out here. The tool that writes the manifest
    maintains both the file and the sentence. Quoting the sentence retains this from drifting away from the tool
    that would have to be run.
    """
    try:
        with open(manifest, encoding="utf-8", errors="replace") as handle:
            for line in handle:
                if not line.startswith("#"):
                    break
                said = line.lstrip("#").strip()
                if ".py" in said:
                    return said
    except OSError:
        pass
    return (
        "reconcile this tree against %s and re-sign it before committing"
        % os.path.basename(manifest)
    )
