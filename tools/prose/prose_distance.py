#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Score this repository's own prose against English, on the bench that already measures English.
#
#   Usage:  python tools/prose/prose_distance.py [<root> ...] [--worst N]
#
# WHAT THIS ANSWERS
#
# docs_check bans 242 phrases and every one got there because somebody noticed it, and that makes
# the list a record of noticing. The question underneath it is whether this tree's prose reads like
# English somebody wrote. That question already has a bench: english_sift.english_reference builds
# a byte pair reference from four megabytes of ordinary English plus the in-domain English nine
# readers marked by hand, and english_sift.surprise scores a text against it in bits per pair.
# This points that bench at the repository, with the research papers beside it to fix the scale.
#
# TWO EARLIER ARRANGEMENTS WERE WRONG AND ARE RECORDED SO THEY ARE NOT REBUILT
#
# The first used Shakespeare, Milton, Austen and the King James Bible as the human arm. A distance
# from technical documentation to a 1667 epic is a distance between two registers, and it would
# have been read as a distance between two writers.
#
# The second split this tree by whether the current session opened a file, and called the
# untouched half a person's prose. That split is void: the text of this repository is almost all
# machine-written, over many sessions. Both halves had one author. It explains the null the
# distributional form of this script returned, where the gap between those halves carried the same
# sign at four symbol widths and cleared no floor at any of them. There was no contrast in it to
# find, and a measurement that cannot fail is not a measurement.
#
# The research papers are the human arm. 154 of them, linguists writing for linguists, the same
# register as the documentation under test and out of the same PDF pipeline.
#
# WHAT A NUMBER MEANS
#
# Surprise is mean bits per byte pair. Lower sits closer to English. The papers fix the scale. A
# repository file inside the papers' own spread reads the way a human technical page reads, and one
# above all of them does not. Nothing here is a verdict on a sentence. It ranks files by how far
# they sit from English, which says where to look, and a person still reads the file.

import io
import re
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
PAPERS = os.path.join(ROOT, "build", "papers")

sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "src", "python", "engine", "nbody", "orior", "instrument"))

import docs_check  # noqa: E402
from english_sift import english_reference, looks_like_writing, surprise  # noqa: E402

# A file carries this much prose before it is scored. Under it the mean rests on one or two
# comments and moves on any single word.
LEAST = 400


# A LaTeX control sequence, and the braces and math shifts around it. Removed before a .tex file is
# scored, because \setmainfont and \renewcommand are markup and scoring them measures the format.
CONTROL = re.compile(r"\\[A-Za-z@]+\*?")
BRACES = re.compile(r"[{}$&~^_\\]|\[[^\]]*\]")


def tex_prose(lines):
    """The English in a .tex file: its percent commentary and its body text, with markup removed.

    Both halves, and the first arrangement retained the wrong one. Dropping the percent lines and
    retention the rest fed \\usepackage{iftex} \\RequireLuaTeX to an English scorer and threw away the
    paragraph explaining why the engine changed. In a preamble the prose is the commentary; in a
    chapter it is the body. Taking both is the only reading that works on either.
    """
    held = []
    for line in lines:
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith("%"):
            held.append(stripped.lstrip("%").strip())
            continue
        text = BRACES.sub(" ", CONTROL.sub(" ", stripped))
        held.append(text)
    return held


def prose_of(path):
    """The comment, docstring and markdown text of one file, with the code removed.

    The same extraction docs_check runs. The two tools read the same thing. That now includes
    .tex, which docs_check read for the first time on 2026-09-09 and had never read before.
    """
    full = path if os.path.isabs(path) else os.path.join(ROOT, path)
    if not os.path.isfile(full):
        return ""
    with open(full, encoding="utf-8", errors="replace") as handle:
        lines = handle.read().splitlines()
    if full.endswith(".tex"):
        retained = tex_prose(lines)
    else:
        retained = docs_check.prose_only(path, lines)
    return " ".join(one.strip() for one in retained if one.strip())


# The roots read where the caller names none. Every name here has to be a directory that exists: a
# name no directory answers is skipped in silence, and the walk then reads fewer files than it
# reports.
DEFAULT_ROOTS = ("docs", "src", "examples", "utils/maint", "theory", "utils/test")


def repository_files(where=None, roots=None):
    """Every prose file under `roots`, relative to `where`.

    `where` defaults to this repository and `roots` to the ones above. Both are arguments because
    this tool measures the distance of a body of writing from a human pole, and the body of writing
    is not always this tree.

    Both have to be taken. A tool that ignores a named path measures orior and reports the
    number as though it belonged to whatever the caller asked about, and the only sign of it is the
    word count at the foot. Silently measuring the wrong subject is the failure that looks max like
    success.
    """
    where = where or ROOT
    found = []
    for root in roots or DEFAULT_ROOTS:
        base = root if os.path.isabs(root) else os.path.join(where, root)
        if not os.path.isdir(base):
            continue
        for folder, dirs, names in os.walk(base):
            dirs[:] = docs_check.retained_dirs(folder, dirs)
            for name in sorted(names):
                if not name.endswith(docs_check.CHECKED):
                    continue
                found.append(os.path.relpath(os.path.join(folder, name), where).replace("\\", "/"))
    return found


def named_roots(argv):
    """The roots a caller named, and the tree they sit under, or (None, None) for the defaults.

    A named root that does not exist stops the run. Falling through to the defaults there is how a
    caller ends up holding this tree's number and believing it is their own.
    """
    given = []
    skip = False
    for at, one in enumerate(argv[1:], start=1):
        if skip:
            skip = False
            continue
        if one.startswith("--"):
            # --worst, --band, --explain and --decompose each take a value.
            skip = one in ("--worst", "--band", "--explain", "--decompose")
            continue
        given.append(one)
    if not given:
        return None, None

    for one in given:
        if not os.path.isdir(one):
            raise SystemExit("  %s is not a directory. Nothing was measured. Nothing passed." % one)
    where = os.path.commonpath([os.path.abspath(one) for one in given]) if len(given) > 1 else os.path.abspath(given[0])
    if os.path.isfile(where):
        where = os.path.dirname(where)
    return where, [os.path.abspath(one) for one in given]


def median(values):
    """The middle value. Used because these scores are heavy tailed and a mean follows the tail."""
    held = sorted(values)
    if not held:
        return 0.0
    middle = len(held) // 2
    if len(held) % 2:
        return held[middle]
    return (held[middle - 1] + held[middle]) / 2.0


def main():
    out = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace", newline="")
    worst = 25
    if "--worst" in sys.argv:
        worst = int(sys.argv[sys.argv.index("--worst") + 1])

    counts, total, lines = english_reference()
    if not total:
        out.write("  no English reference: build/corpora/cc_english.txt is missing\n")
        out.flush()
        return 1
    out.write("\n  English reference: %d byte pairs over %d lines\n" % (total, lines))

    human = []
    if os.path.isdir(PAPERS):
        for name in sorted(os.listdir(PAPERS)):
            if not name.endswith(".txt"):
                continue
            with open(os.path.join(PAPERS, name), encoding="utf-8", errors="replace") as handle:
                text = handle.read()
            if (len(text) < LEAST) or not looks_like_writing(text):
                continue
            human.append((surprise(text, counts, total), name))
    human.sort()

    if not human:
        out.write("  no research papers under build/papers to fix the scale\n")
        out.flush()
        return 1

    scores = [one for one, _ in human]
    low, high = scores[0], scores[-1]
    out.write("\n  the human scale, %d research papers, bits per byte pair\n" % len(human))
    out.write("    best   %.4f  %s\n" % (human[0][0], human[0][1]))
    out.write("    median %.4f\n" % median(scores))
    out.write("    worst  %.4f  %s\n" % (human[-1][0], human[-1][1]))

    where, roots = named_roots(sys.argv)
    if where is not None:
        out.write("\n  measuring %s\n" % where.replace("\\", "/"))

    mine = []
    for path in repository_files(where, roots):
        text = prose_of(os.path.join(where or ROOT, path))
        if (len(text) < LEAST) or not looks_like_writing(text):
            continue
        mine.append((surprise(text, counts, total), path))
    mine.sort()

    if not mine:
        out.write("  no repository prose to score\n")
        out.flush()
        return 1

    ours = [one for one, _ in mine]
    out.write("\n  the repository, %d prose files\n" % len(mine))
    out.write("    best   %.4f  %s\n" % (mine[0][0], mine[0][1]))
    out.write("    median %.4f\n" % median(ours))
    out.write("    worst  %.4f  %s\n" % (mine[-1][0], mine[-1][1]))

    above = [one for one in mine if one[0] > high]
    inside = [one for one in mine if low <= one[0] <= high]
    below = [one for one in mine if one[0] < low]
    out.write("\n  against the human spread of %.4f to %.4f\n" % (low, high))
    out.write("    %4d files sit inside it\n" % len(inside))
    out.write("    %4d sit below it, closer to ordinary English than any paper\n" % len(below))
    out.write("    %4d sit above it, further from English than all %d papers\n" % (len(above), len(human)))

    out.write("\n  furthest from English, worst first\n")
    for score, path in list(reversed(mine))[:worst]:
        flag = "   above every paper" if score > high else ""
        out.write("    %.4f  %s%s\n" % (score, path, flag))

    out.write("\n")
    out.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
