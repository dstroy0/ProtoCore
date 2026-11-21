#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# What the checker can be asked besides whether the tree passes.
#
# A repair needs three things the scan does not give: the word this writing would use in a place,
# the words in the tree that have no warrant at all, and the lines a finding actually sits on. Each
# of those is a reading of the same tables and the same walk the scan already does, which is why
# they belong here instead of in five scripts that each rebuild the roots and the file list.
#
# A READING NEVER FAILS ANYTHING. Every one of these exits 0 when it had something to print and 4
# when the request could not be parsed. None of them returns 1, 2 or 3: those belong to the run and
# a hook reads them.

import os
import sys

from .holes import diff_holes
from .oracle import offlist, oracle_roots, pick, plain_for

USAGE = (
    "  docs_check --pick=after <word>          what this writing puts after a word",
    "  docs_check --pick=before <word>         what it puts before one",
    "  docs_check --pick=between <one> <two>   what it puts between two",
    "  docs_check --pick=rank <word> ...       each word's count, 0 where it is never used",
    "  docs_check --pick=plain <words> ...     the plain form the style guide gives for a phrase",
    "  docs_check --offlist [<root> ...]       words in the tree that voice.tsv does not approve",
    "  docs_check --holes=<unified diff>       added lines a repair may have left ungrammatical",
    "  docs_check --show=<regex> [<root> ...]  the lines the matching findings sit on",
    "  docs_check --slack                      ratchet ceilings nothing reaches",
    "  docs_check --rate [<root> ...]          banned shapes as a rate, against the drawn bars",
    "  docs_check --harmonics                  the voice as a rhythm spectrum, and writes its band",
    "  docs_check --sample --words N <root>    one continuous sample, and writes it with a manifest",
    "  docs_check --plain <path> ...           the prose with the mathematics taken out",
)


def words_given(argv):
    """The positional arguments, which a reading takes as its words and the run takes as its roots."""
    return [one for one in argv if not one.startswith("-")]


def show_pick(asked, given):
    """Print one pick reading. 4 where the request names no reading this can answer."""
    if asked == "plain":
        return show_plain_for(given)
    rows = pick(asked, given)
    if rows is None:
        print("  --pick takes after, before, between or rank, and the words to ask about.")
        for line in USAGE:
            print(line)
        return 4
    if not rows:
        print("  this writing makes no such juxtaposition. Nothing to report, which is an answer.")
        return 0
    for count, word in rows:
        print("  %8d  %s" % (count, word))
    return 0


def show_plain_for(given):
    """Print every phrase of the plain table the words hold, with the form the guide gives for it."""
    if not given:
        print("  --pick=plain takes the words to read: --pick=plain <words> ...")
        return 4
    rows = plain_for(given)
    if not rows:
        print("  the style guide names no phrase in these words.")
        return 0
    for phrase, plain, section in rows:
        print("  %-32s %-28s NARA %s" % (phrase, plain or "(leave it out)", section))
    return 0


def show_offlist(given):
    """Print every word in the tree's prose that the approved list does not hold."""
    roots = oracle_roots(given)
    rows, uses = offlist(roots)
    print(
        "  %d off-list word(s), %d use(s), over %d root(s)"
        % (len(rows), uses, len(roots))
    )
    print("     uses  files  word")
    for count, files, word in rows[:60]:
        print("  %7d  %5d  %s" % (count, files, word))
    if len(rows) > 60:
        print("  ... and %d more" % (len(rows) - 60))
    return 0


def show_holes(path):
    """Print every added line in a diff that reads as a hole left by a repair."""
    if not os.path.isfile(path):
        print("  no diff at %s. Write one with: git diff > <path>" % path)
        return 4
    found = diff_holes(path)
    for name, why, body in found:
        print("  %s  %s" % (name, why))
        print("     %s" % body)
    print("  %d suspect line(s)" % len(found))
    return 0


def show_slack(rows, where):
    """Print the ceilings nothing reaches, which is how much of the ratchet governs nothing."""
    print(
        "  slack in %s: %d entr(ies), %d finding(s) of room"
        % (os.path.basename(where), len(rows), sum(one[0] for one in rows))
    )
    if rows:
        print("   slack  ceiling  carries  file")
    for gap, ceiling, here, name in rows[:20]:
        print("  %6d  %7d  %7d  %s" % (gap, ceiling, here, name))
    if len(rows) > 20:
        print("  ... and %d more" % (len(rows) - 20))


def show_lines(path, lines, wording, want, trail):
    """Print the source line each matching finding sits on, with `trail` lines after it.

    Read before repaired. A finding names a pattern and a line number, and neither of those is the
    sentence: a repair written from the finding alone rewrites what the pattern matched instead of
    what the sentence was trying to say.
    """
    for at, what in sorted(wording):
        if not want.search(what):
            continue
        print("  == %s:%d  %s" % (path.replace("\\", "/"), at, what))
        for step in range(0, trail + 1):
            if 0 < at + step <= len(lines):
                print("     %d| %s" % (at + step, lines[at + step - 1]))


def without(argv, flag):
    """The command line with the reading's own flag taken out."""
    return [one for one in argv if one != flag]


def as_argv(entry, flag):
    """Run a reading that parses the command line itself, with its own flag taken out first.

    Two of the instruments read sys.argv through argparse. Passed the flag that selected them they
    stop on an unrecognised option, so it comes out before they look and goes back afterward.
    """
    held = sys.argv
    sys.argv = without(held, flag)
    try:
        return entry() or 0
    finally:
        sys.argv = held


def reading(argv, option_value):
    """One standalone reading, or None where the command line asks for the scan instead."""
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    given = words_given(argv)

    asked = option_value("--pick")
    if asked is not None:
        return show_pick(asked, given)
    if "--pick" in argv:
        return show_pick(None, given)

    if "--offlist" in argv:
        return show_offlist(given)

    diff = option_value("--holes")
    if diff is not None:
        return show_holes(diff)
    if "--holes" in argv:
        print("  --holes takes the diff to read: --holes=<path>")
        return 4

    # The four instruments. Each is imported where it is asked for and never at the package root:
    # two of them pull numpy in and one resolves the repository with git, and a scan that wanted
    # none of that would pay for all three.
    if "--rate" in argv:
        from .rate import show_rate
        return show_rate(words_given(argv))

    if "--harmonics" in argv:
        from .harmonics import show_harmonics
        # A reading that returns nothing would read as None here, and None is this function's way
        # of saying run the scan instead. Every reading answers with a status.
        return show_harmonics() or 0

    if "--sample" in argv:
        from .sample import show_sample
        return as_argv(show_sample, "--sample")

    if "--plain" in argv:
        from .plain import show_plain
        return as_argv(show_plain, "--plain")

    return None
