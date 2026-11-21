#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# The voice oracle: which words this body of writing uses, and which words it puts next to which.
#
# THE TABLES DECIDE A WORD AND NOTHING ELSE DOES. voice.tsv is the approved word list, counted over
# the research papers, and voice_word_web.tsv is every adjacent pair in them. A word absent from
# voice.tsv is not a word of this voice however well it reads, and a pair absent from the web is a
# juxtaposition this writing does not make. Both are read here and neither is edited here: a count
# is evidence about the corpus and changing one to suit a repair falsifies the only instrument that
# can judge the repair.
#
# project_words.tsv is the second word list: the names and the mnemonics of this project, which the
# research papers do not hold. It carries no counts. Its words are the author's, one row each with
# what type of word it is, and like voice.tsv it is read here and never written here. counted() joins
# the two into one dict, and a project word carries the count 0, which is its true count in the
# corpus.
#
# The readings are deliberately narrow. `pick` answers what goes in a place and `offlist` answers
# which words in the tree have no warrant, and between them they cover the whole of what the tables
# can say. Anything wider would be this file having an opinion.

import os
import re

from .files import walk_markdown
from .grammar import PLAIN_TABLE, plain_rows  # noqa: F401
from .prose import prose_only
from .repository import DEFAULT_ROOTS, REPOSITORY

# Beside the package, not inside it. The tables are read by the distance tools too and belong to
# utils/maint/prose rather than to the checker.
TABLES = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORD_TABLE = os.path.join(TABLES, "voice.tsv")
PROJECT_TABLE = os.path.join(TABLES, "project_words.tsv")
WEB_TABLE = os.path.join(TABLES, "voice_word_web.tsv")

# A word as the tables define one: lower case, and an apostrophe or a hyphen inside it counts as part
# of the word. Splitting on those would turn `doesn't` into `doesn` and `t`, and neither is a word
# the corpus holds.
WORD = re.compile(r"[a-z][a-z'-]*")

# How many rows a reading prints. A longer tail is the same information at a length nobody reads.
DISPLAYED = 30

# Words this reading will not report missing. Two characters is a particle, and the question of whether
# the corpus happens to hold `fd` or `ok` is not a question about voice.
SHORTEST = 3


def counted():
    """Every word voice.tsv or project_words.tsv approves, as one dict of word to corpus count."""
    held = {}
    if os.path.isfile(WORD_TABLE):
        with open(WORD_TABLE, encoding="utf-8", errors="replace") as handle:
            for line in handle:
                part = line.rstrip("\r\n").split("\t")
                if len(part) > 1 and part[1].isdigit():
                    held[part[0]] = int(part[1])
    if os.path.isfile(PROJECT_TABLE):
        with open(PROJECT_TABLE, encoding="utf-8", errors="replace") as handle:
            next(handle, None)
            for line in handle:
                word = line.rstrip("\r\n").split("\t")[0].strip().lower()
                if word:
                    held.setdefault(word, 0)
    return held


def plain_for(given):
    """The plain-table rows whose phrase occurs in the words given, in table order."""
    said = " " + " ".join(" ".join(given).lower().split()) + " "
    return [row for row in plain_rows() if (" %s " % row[0]) in said]


def adjacent():
    """Every pair in the web, indexed both ways: what follows a word, and what precedes it."""
    after = {}
    before = {}
    if not os.path.isfile(WEB_TABLE):
        return after, before
    with open(WEB_TABLE, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            part = line.rstrip("\r\n").split("\t")
            if len(part) < 3 or not part[2].isdigit():
                continue
            count = int(part[2])
            after.setdefault(part[0], []).append((count, part[1]))
            before.setdefault(part[1], []).append((count, part[0]))
    return after, before


def between(after, before, left, right):
    """Every word the corpus puts after `left` and also before `right`.

    Scored at the weaker of the two counts. A word that follows `left` ten thousand times and
    precedes `right` once is a word this writing does not actually put between them, and taking the
    larger of the two would rank it first.
    """
    leading = dict((word, count) for count, word in after.get(left, []))
    trailing = dict((word, count) for count, word in before.get(right, []))
    both = set(leading) & set(trailing)
    return sorted(((min(leading[one], trailing[one]), one) for one in both), reverse=True)


def pick(asked, given):
    """One reading of the tables, as (count, word) rows, commonest first.

    `asked` is after, before, between or rank. rank answers for words the caller names, and reports
    0 for a word the corpus never uses, which is the answer and not a failure to find one. The plain
    reading has rows of its own shape and is plain_for.
    """
    given = [one.lower() for one in given]
    if asked == "rank":
        held = counted()
        return [(held.get(one, 0), one) for one in given]
    if not given:
        return None
    after, before = adjacent()
    if asked == "after":
        return sorted(after.get(given[0], []), reverse=True)[:DISPLAYED]
    if asked == "before":
        return sorted(before.get(given[0], []), reverse=True)[:DISPLAYED]
    if asked == "between" and len(given) > 1:
        return between(after, before, given[0], given[1])[:DISPLAYED]
    return None


def offlist(roots, ledger=None):
    """Every word in the tree's prose that voice.tsv does not approve, with where it is used.

    Returns (rows, uses), where a row is (count, files, word) ordered commonest first. The prose is
    taken through the checker's own reader, so a word inside a legal header, a verbatim block or a
    generated region is out of scope here for the same reason it is out of scope for a finding.
    """
    allowed = counted()
    counts = {}
    where = {}
    for path in sorted(walk_markdown(roots, ledger)):
        with open(path, encoding="utf-8", errors="replace") as handle:
            lines = handle.read().splitlines()
        # prose_only answers line for line, with everything out of scope blanked, so joining it
        # back up gives the prose and nothing else.
        said = "\n".join(prose_only(path, lines, ledger))
        for word in WORD.findall(said.lower()):
            if word in allowed or len(word) < SHORTEST:
                continue
            counts[word] = counts.get(word, 0) + 1
            where.setdefault(word, set()).add(path)
    rows = sorted(((count, len(where[word]), word) for word, count in counts.items()), reverse=True)
    return rows, sum(counts.values())


def oracle_roots(given):
    """The roots a reading walks: what the caller named, or this repository's prose roots."""
    if not given:
        return list(DEFAULT_ROOTS)
    roots = []
    for one in given:
        if os.path.exists(one):
            roots.append(one)
            continue
        beside = os.path.join(REPOSITORY, one)
        roots.append(beside if os.path.exists(beside) else one)
    return roots
