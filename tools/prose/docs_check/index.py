#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# A literal index over the ban table, so a file is not matched against every pattern.
#

import re

from .bans import BANNED



# A quoted passage in a page: an opening double quote, a run of text, a closing one. Somebody
# else's words, and never this project's prose to repair. Jaynes is quoted twice in the ledger and
# once in an engine README, and joining wrapped lines put his wording in front of the scanner.
# Bounded to markdown, because a docstring's own """ marker would otherwise open a span that
# swallowed the rest of the file.
PASSAGE = re.compile(r"[\"“][^\"“”]{16,600}[\"”]")


# THE LITERAL INDEX. Running every BANNED pattern over every run is 394 patterns against about
# 14,000 runs over this tree, 5.5 million regex scans and 96 percent of a 29 second hook, nearly
# all of them over text that cannot hold a match. So each pattern is parsed once for literal text
# that every match of it must contain, and a run is scanned only by the patterns whose literal it
# holds. `\bdelve` needs "delve"; `\bis (what|why|the)\b` needs "is ".
#
# It is a necessary condition and never a sufficient one, so it cannot add a hit or lose one. The
# regex still decides every finding. A pattern the parser cannot name a literal for goes into the
# always-run list and is scanned against every run. The patterns are matched with
# IGNORECASE, and so the run is folded the way re folds it: ASCII lowercased, and a non-ASCII
# character mapped to the ASCII character re would let it match (the long s matches `s`, the Kelvin
# sign `k`, dotted capital I `i`). str.lower() is not that fold. It turns the dotted capital I
# into two characters and leaves the long s alone, and a prefilter built on it would drop a match
# re makes.
#
# test_docs_check_index.py holds the index to the brute force scan, hit for hit and in order.
_PARSER = re._parser if hasattr(re, "_parser") else __import__("sre_parse")
_CONST = re._constants if hasattr(re, "_constants") else __import__("sre_constants")
_ATOMIC = getattr(_CONST, "ATOMIC_GROUP", None)
_REPEATS = tuple(
    getattr(_CONST, name)
    for name in ("MAX_REPEAT", "MIN_REPEAT", "POSSESSIVE_REPEAT")
    if hasattr(_CONST, name)
)


def _required(parsed):
    """Literal strings, one of which appears in every match of a parsed pattern, or None."""
    best = None

    def offer(found):
        nonlocal best
        if found and all(found) and (best is None or min(map(len, found)) > min(map(len, best))):
            best = found

    run = ""
    for op, av in parsed:
        if op is _CONST.LITERAL and av < 128:
            run += chr(av).lower()
            continue
        # A zero-width anchor between two literals leaves them adjacent in the text.
        if op is _CONST.AT:
            continue
        offer({run})
        run = ""
        if op is _CONST.SUBPATTERN:
            offer(_required(av[-1]))
        elif op is _ATOMIC:
            offer(_required(av))
        elif op is _CONST.BRANCH:
            each = [_required(alternative) for alternative in av[1]]
            if all(each):
                offer(set().union(*each))
        elif op in _REPEATS and av[0] >= 1:
            offer(_required(av[2]))
    offer({run})
    return best


def literal_index(patterns):
    """(literal, patterns needing it) pairs, and the patterns no literal could be named for."""
    needing = {}
    always = []
    for pattern in patterns:
        found = _required(_PARSER.parse(pattern))
        if not found:
            always.append(pattern)
            continue
        for literal in found:
            needing.setdefault(literal, set()).add(pattern)
    return sorted(needing.items()), frozenset(always)


_INDEX = literal_index(BANNED)
_COMPILED = {pattern: re.compile(pattern, re.IGNORECASE) for pattern in BANNED}
_FOLDS = {}


def _fold(character):
    """The character a literal in the index is compared against, the way IGNORECASE compares."""
    if character not in _FOLDS:
        folded = character
        for character in "abcdefghijklmnopqrstuvwxyz":
            if re.fullmatch(character, character, re.IGNORECASE):
                folded = character
                break
        _FOLDS[character] = folded
    return _FOLDS[character]


def folded(text):
    """text as the index reads it."""
    if text.isascii():
        return text.lower()
    return "".join(c.lower() if c.isascii() else _fold(c) for c in text)


def present(text, needing=None):
    """The index entries whose literal text holds.

    banned_hits asks this once of the whole file and then once of each run, against only what the
    file held. Max files hold a few dozen of the 596 literals, so a run is compared against those
    and not against all of them.
    """
    text = folded(text)
    return [(literal, patterns) for literal, patterns in (_INDEX[0] if needing is None else needing)
            if literal in text]


def candidates(text, needing=None):
    """The BANNED patterns that can match somewhere in text, in BANNED order."""
    live = set(_INDEX[1])
    for _, patterns in present(text, needing):
        live |= patterns
    return [pattern for pattern in BANNED if pattern in live]
