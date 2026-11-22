#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# Tests for the literal index in docs_check, which decides which BANNED patterns scan a run.
#
#   Usage:  python utils/maint/prose/test_docs_check_index.py
#
# The index is a prefilter and it is allowed one property: it never changes a finding. Every test
# here holds it to the unfiltered scan, where every pattern runs over every run, and compares the
# two hit for hit and in order. The tree test reads every file under the checker's own roots and
# takes about half a minute here, because the unfiltered scan is half of what it runs.

import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import docs_check


def both(lines, **options):
    """(indexed hits, brute force hits) for one file's lines."""
    indexed = list(docs_check.banned_hits(lines, **options))
    retained = docs_check.candidates
    docs_check.candidates = lambda text, needing=None: list(docs_check.BANNED)
    try:
        brute = list(docs_check.banned_hits(lines, **options))
    finally:
        docs_check.candidates = retained
    return indexed, brute


class TheIndexNamesALiteral(unittest.TestCase):
    def test_every_pattern_is_indexed_or_always_run(self):
        needing, always = docs_check._INDEX
        indexed = set().union(*(patterns for _, patterns in needing)) | always
        self.assertEqual(indexed, set(docs_check.BANNED))

    def test_known_literals(self):
        # Each is read off the parsed pattern: a literal prefix, a literal across a \b, and one
        # alternative per branch of a group.
        parse = docs_check._PARSER.parse
        self.assertEqual(docs_check._required(parse(r"\bdelve")), {"delve"})
        self.assertEqual(docs_check._required(parse(r"\bthe one\b")), {"the one"})
        self.assertEqual(docs_check._required(parse(r"\bis (what|why|the)\b")), {"is "})
        self.assertEqual(
            docs_check._required(parse(r"\b(which|where|there) is\b")),
            {"which", "where", "there"},
        )

    def test_an_optional_part_names_nothing(self):
        # A literal that may be absent from a match cannot be required of the text.
        parse = docs_check._PARSER.parse
        self.assertIsNone(docs_check._required(parse(r"(abc)?")))
        self.assertIsNone(docs_check._required(parse(r"x|\w+")))


class TheIndexChangesNoFinding(unittest.TestCase):
    def test_folded_characters_still_reach_their_pattern(self):
        # re lets each of these match an ASCII character under IGNORECASE and str.lower() does not
        # turn it into one. The Kelvin sign and the long s would each drop a hit from an index
        # built on str.lower(), and the dotted capital I would become two characters.
        for line in (
            "the tapeſtry of it",
            "a Kerb and a TAPESTRY",
            "THE ONE THAT MATTERS",
            "İS WHAT MAKES it",
        ):
            indexed, brute = both([line], comments=True)
            self.assertEqual(indexed, brute, line)
            self.assertTrue(brute, line)

    def test_every_file_under_the_roots(self):
        roots = [os.path.join(docs_check.REPOSITORY, one) for one in docs_check.DEFAULT_ROOTS]
        roots = [one for one in roots if os.path.exists(one)]
        checked = 0
        for path in sorted(docs_check.walk_markdown(roots)):
            with open(path, encoding="utf-8", errors="replace") as handle:
                lines = handle.read().splitlines()
            said = docs_check.prose_only(path, lines)
            # The options main() gives this file.
            indexed, brute = both(
                said,
                quotations=path.endswith(".md"),
                comments=not path.endswith((".md", ".tex")),
            )
            self.assertEqual(indexed, brute, path)
            checked += 1
        self.assertGreater(checked, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
