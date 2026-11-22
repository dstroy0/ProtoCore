#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# What a contribution looks like before a person reads it.
#
#   Usage:  `python utils/maint/prose/submission_check.py <path> [<path> ...]`
#           `python utils/maint/prose/submission_check.py --tree <repository root>`
#
# THE RULE THIS SERVES
#
# CONTRIBUTING.md:8 states a condition as non-negotiable, and it is the only one there. A tool for language that comes
# out of this work requires a human to review its output. The project does not accept a contribution that removes a
# person from that loop. CONTRIBUTING.md:30 then lists three checks, prose and the kernel and the ports, and not one
# looks at the condition that comes first. A person who pastes generated text into a Salishan form is a person removed
# from the loop. No other part of the tree catches it.
#
# IT REPORTS. IT DOES NOT DECIDE, AND NOBODY MAY WIRE IT AS ONE
#
# A scanner that errors on a contribution on its own automates away the human that CONTRIBUTING.md:8 requires, in the
# name of that same rule. Nothing here prints pass, fail, accept or reject. The exit status reports what the scan found,
# and never what to do about it. Do not put this in a hook that rejects. The next person to wire it up may not know this
# rule, and this comment states it where that person will look.
#
# The numbers below are evidence for a person, and a person can look at any of them and say that the contributor writes
# that way. A contributor who does is not at fault.
#
# THE SURFACE
#
# A register, which carries the measurement. The tool utils/maint/prose/docs_check holds 285 patterns. For
# 121 of them, it holds the human rate: the rate in 759,815 words of the papers under build/papers. This file imports
# those rates from that file, and never restates them. Their sum is the rate that a human carries for the whole list.
# The baseline here comes from the same table as the findings, and cannot drift away from it.
#
# WHAT THE RATES COUNT OVER, WHICH DECIDES WHETHER THEY MEAN ANYTHING
#
# A rate is per hundred thousand words. The denominator here is a count of character runs in the prose of the
# submission, after docs_check.prose_only removes code, math and markup. The human rates count over the output of
# english_gate instead, which is stricter. It drops the interlinear glosses, the orthography and the tables that fill a
# Salishan paper.
#
# For a submission that is English prose, the two denominators are close, and the ratio is worth a read. For a
# submission that is mostly a table, a word list or IPA, this count runs high and every rate runs low. The comparison
# then says little. The output prints the word count for that reason. Read it before the ratio.
#
# A LOW NUMBER IS NOT EVIDENCE OF ANYTHING
#
# The rates are a floor and only a floor. A contributor who knows the list can write around it. This tree's own prose
# also suppresses the phrases, and the human corpus does not. A submission with a score near the human rate says
# nothing, except that its score is near the human rate.

import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from docs_check import (
    BANNED,
    CHECKED,
    HUMAN_RATE,
    retained_dirs,  # noqa: E402
    banned_hits,
    prose_only,
    quieted,
    stage_of,
)

ROW_FORMAT = "    %-26s %9.1f   human %7.1f   %s\n"

# The rate a human research writer carries for the whole banned list, per hundred thousand words.
# Summed from the table instead of written down. The two cannot disagree. It comes to 387.9.
HUMAN_WHOLE = sum(HUMAN_RATE.values())

# What a submission arrives as. docs_check reads the five that this repository writes in. A contribution also arrives as
# a plain page or a transcribed paper, and neither has a comment marker to find prose behind. The tool reads both as
# prose all the way down.
PLAIN = (".txt", ".rst", ".markdown", ".text", ".org")
SUBMITTED = CHECKED + PLAIN

# Extensions read as bytes. A match inside one would be unreadable in a report.
BINARY = (
    ".png",
    ".jpg",
    ".jpeg",
    ".gif",
    ".webp",
    ".bmp",
    ".ico",
    ".pdf",
    ".zip",
    ".gz",
    ".xz",
    ".tar",
    ".7z",
    ".wav",
    ".mp3",
    ".flac",
    ".ogg",
    ".ttf",
    ".otf",
    ".woff",
    ".woff2",
    ".so",
    ".dll",
    ".exe",
    ".dylib",
    ".o",
    ".a",
    ".pyc",
    ".npy",
    ".npz",
    ".bin",
)

# Bytes above which the report names a file as skipped, and does not read it. A silent skip reads as a clean result. The
# foot of the report names every skipped file.
LARGE = 4 * 1024 * 1024

# A character run. Digits, punctuation and the markup that docs_check leaves behind are not words. A count of them would
# lower every rate, because the denominator would grow.
WORD = re.compile(r"[^\W\d_][\w'’-]*")


def text_of(path):
    """One submitted file as text, or None where the read failed."""
    try:
        if os.path.getsize(path) > LARGE:
            return None
        with open(path, "rb") as handle:
            raw = handle.read()
    except OSError:
        return None
    if b"\x00" in raw:
        return None
    return raw.decode("utf-8", "replace")


def prose_of(path, lines):
    """The prose of one submitted file, with code, math and markup taken out.

    docs_check knows the five extensions that this repository writes in. A plain page or a transcribed paper has no
    comment marker to find prose behind. The function reads it whole, and honors only its quiet blocks.
    """
    if path.endswith(CHECKED):
        return prose_only(path, lines)
    return quieted(lines)


def walk(roots):
    """Every submitted file under the given paths, with a file argument taken as itself."""
    found = []
    for root in roots:
        if os.path.isfile(root):
            found.append(root)
            continue
        for here, dirs, names in os.walk(root):
            dirs[:] = retained_dirs(here, dirs)
            found.extend(os.path.join(here, name) for name in sorted(names))
    return sorted(found)


def git(root, *arguments):
    """One git command in one repository, as text, or None where git errored.

    The helper turns core.quotePath OFF for every call, and not only at the site that lists paths.

    With it on, which is git's default, a path that holds any character outside ASCII comes back
    quoted and octal-escaped. The path `papers/...Kwak’wala.pdf` comes back as
    `"papers/...Kwak\\342\\200\\231wala.pdf"`. tracked() then joins that to the root, and produces a
    path that does not exist. The file drops out of the listing with no error. A screen that
    cannot see a file reports it clean.

    This is not hypothetical in this tree. The Salishan corpus tracks two papers whose names carry
    U+2019, which is ordinary Salishan orthography. A listing without this flag returns 0 of them,
    and one with it returns 2.

    That is the argument for the flag in the helper. A rule in a document relies on the memory of
    whoever writes the next call site. A flag in the one function that every call goes through does
    not.
    """
    try:
        done = subprocess.run(
            ("git", "-c", "core.quotePath=false") + arguments,
            cwd=root,
            capture_output=True,
            timeout=120,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if done.returncode != 0:
        return None
    return done.stdout.decode("utf-8", "replace")


def tracked(root):
    """Every path that git tracks under one repository root, or None where git errored.

    The tree mode asks a question about a contribution too. It asks whether the tree that a contributor works
    inside already carries what the screen looks for.
    """
    listed = git(root, "ls-files")
    if listed is None:
        return None
    return [os.path.join(root, one.replace("/", os.sep)) for one in listed.splitlines() if one]


def counted(paths, base, out):
    """Read every file once. Returns the per pattern counts, the word count and the count of files read."""
    unread = []
    tally = {}
    words = 0
    read = 0

    for path in paths:
        shortened = os.path.relpath(path, base).replace("\\", "/")
        if path.endswith(BINARY) or not path.endswith(SUBMITTED):
            continue
        text = text_of(path)
        if text is None:
            unread.append(shortened)
            continue
        read += 1
        lines = text.splitlines()
        said = prose_of(path, lines)
        words += len(WORD.findall(" ".join(said)))
        for _, pattern, _ in banned_hits(said, quotations=path.endswith(".md")):
            tally[pattern] = tally.get(pattern, 0) + 1

    if unread:
        out.write("\n  NOT READ\n")
        for shortened in unread:
            out.write("    %s\n" % shortened)

    return (tally, words, read)


def register(out, tally, words):
    """The banned list as rates per hundred thousand words, against what a human writer carries."""
    if not words:
        out.write("\n  REGISTER\n    no prose was read. There is no rate to report\n")
        return
    scale = 100000.0 / words

    total = sum(tally.values()) * scale
    phrase = sum(count for pattern, count in tally.items() if stage_of(pattern) == "phrase") * scale
    absent = sum(count for pattern, count in tally.items() if pattern not in HUMAN_RATE) * scale

    out.write("\n  REGISTER, per hundred thousand words of prose\n")
    out.write(ROW_FORMAT % ("the whole banned list", total, HUMAN_WHOLE, times(total, HUMAN_WHOLE)))
    out.write(ROW_FORMAT % ("the phrase stage", phrase, phrase_human(), times(phrase, phrase_human())))
    out.write(ROW_FORMAT % ("patterns humans never use", absent, 0.0, "no human rate to divide by"))

    ranked = sorted(tally.items(), key=lambda one: excess(one[0], one[1] * scale), reverse=True)
    if not ranked:
        return
    out.write("\n    %-46s %5s %8s %8s %9s\n" % ("token", "n", "here", "human", "times"))
    for pattern, count in ranked:
        rate = count * scale
        human = HUMAN_RATE.get(pattern, 0.0)
        out.write("    %-46s %5d %8.1f %8.1f %9s\n" % (readable(pattern), count, rate, human, times(rate, human)))


def phrase_human():
    """What a human writer carries for the phrase stage, summed from the same table."""
    return sum(rate for pattern, rate in HUMAN_RATE.items() if stage_of(pattern) == "phrase")


def excess(pattern, rate):
    """How far above a human writer one pattern sits, for the rank order. Absent patterns rank first."""
    human = HUMAN_RATE.get(pattern, 0.0)
    if not human:
        return float("inf") if rate else 0.0
    return rate / human


def times(rate, human):
    """One ratio, or the words for a denominator of zero."""
    if not human:
        return "absent" if rate else "-"
    return "%.1f" % (rate / human)


def readable(pattern):
    """One pattern with its regex furniture removed, which leaves the phrase that a reader can read."""
    flat = pattern.replace(r"\b", "").replace(r"\s?", " ").replace(r"\s+", " ")
    flat = flat.replace("[\\w-]+", "X").replace(".{1,40}", " ... ")
    flat = re.sub(r"\\(.)", r"\1", flat)
    return " ".join(flat.split())[:46]


def main():
    import io

    out = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace", newline="")

    tree = "--tree" in sys.argv
    given = [one for one in sys.argv[1:] if not one.startswith("-")]
    if not given:
        out.write("  usage: submission_check.py <path> [<path> ...]\n")
        out.write("         submission_check.py --tree <repository root>\n")
        out.flush()
        return 2

    base = os.path.abspath(given[0])
    if os.path.isfile(base):
        base = os.path.dirname(base)

    if tree:
        root = os.path.abspath(given[0])
        paths = tracked(root)
        if paths is None:
            out.write("  %s is not a git repository, or git errored\n" % given[0])
            out.flush()
            return 2
        out.write("  TRACKED IN %s\n" % base)
    else:
        paths = walk([os.path.abspath(one) for one in given])
        out.write("  SUBMISSION %s\n" % ", ".join(given))

    tally, words, read = counted(paths, base, out)

    out.write("\n  %d file(s) offered, %d read, %s words of prose\n" % (len(paths), read, format(words, ",")))

    register(out, tally, words)

    out.write("\n  %d register hit(s).\n" % sum(tally.values()))
    out.write("  A person reads this. Nothing here is a verdict.\n")
    out.flush()

    # A read of nothing is never a pass. A run that opens no file and reports success is the failure that nobody sees.
    # That is how a wrong path goes unnoticed, until somebody wonders why the count never moves.
    if read == 0:
        out.write("  no file was read. Nothing was screened. Nothing came back clean\n")
        out.flush()
        return 2
    if tally:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
