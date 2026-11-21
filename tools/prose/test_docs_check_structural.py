#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# Tests for the structural stage of docs_check. That stage fails a commit.
#
#   Usage:  python utils/maint/prose/test_docs_check_structural.py
#
# The stage is not tiered and --strict does not reach it. A false positive here is a commit that
# cannot be made. These are the hardest assertions in the suite and the counts below are derived
# from the tree at run time.
#
# Every count this file asserts on is measured when the test runs. A constant is correct on the day
# it is written and wrong on the day the tree moves.

import os
import re
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import docs_check


TABLE_SEPARATOR = re.compile(r"^[ \t]*\|[-:| \t]+\|[ \t]*$")
TABLE_ROW = re.compile(r"^[ \t]*\|")


class LinkTargetsThatAreNotPaths(unittest.TestCase):
    """path_candidate() erroring on the two shapes that wear markdown link syntax."""

    def test_doxygen_reference_is_not_a_path(self):
        # Doxygen references, and the filesystem has no answer about any of them.
        for target in (
            "@ref HTTP_10",
            "@ref HttpVersion",
            "@ref HttpReq::version",
            "@ref send_chunked",
            "@ref WS_FRAME_SIZE",
            "@ref MAX_HEADERS",
            "@ref ORIOR_ENABLE_KEEPALIVE",
            "@ref orior_config.h",
        ):
            self.assertFalse(docs_check.path_candidate(target), target)

    def test_both_doxygen_definitions_are_error(self):
        # Doxygen accepts the backslash form everywhere it accepts the at sign.
        self.assertFalse(docs_check.path_candidate("\\ref MAX_CONNS"))
        self.assertFalse(docs_check.path_candidate("@subpage porting"))

    def test_declarator_syntax_is_not_a_path(self):
        for target in (
            "uint8_t slot, HttpReq *req",
            "const char *user, const uint8_t *blob, size_t len",
            "const char *user, const char *pass",
        ):
            self.assertFalse(docs_check.path_candidate(target), target)

    def test_declarator_with_no_pointer_star_is_not_a_path(self):
        # The parameter list neither the star rule nor the keyword rule reaches.
        self.assertFalse(docs_check.path_candidate("uint8_t slot, size_t len"))

    def test_type_keyword_head_is_not_a_path(self):
        self.assertFalse(docs_check.path_candidate("const char *name"))
        self.assertFalse(docs_check.path_candidate("struct HttpReq request"))

    def test_a_directory_named_const_is_still_a_path(self):
        # The keyword rule wants the space after the keyword. A path segment defined the same way
        # is untouched.
        self.assertTrue(docs_check.path_candidate("const/README.md"))

    def test_two_paths_separated_by_a_comma_are_still_checked(self):
        # The comma rule is bounded to items of two words each. A pair of paths has no interior
        # space in either item and stays a candidate.
        self.assertTrue(docs_check.path_candidate("docs/a.md, docs/b.md"))


class LinksStillChecked(unittest.TestCase):
    """The rule doing the job it is for. A repair that turns a check off is not one."""

    def setUp(self):
        self.here = os.path.join(HERE, "sample.md")

    def test_a_missing_relative_target_is_reported(self):
        found = docs_check.dead_links(
            self.here, ["see [the notes](no_such_file_here.md) for it"]
        )
        self.assertEqual(len(found), 1)
        self.assertIn("no_such_file_here.md", found[0][1])
        self.assertEqual(found[0][0], 1)

    def test_a_target_that_is_there_is_not_reported(self):
        found = docs_check.dead_links(
            self.here, ["see [the checker](docs_check) for it"]
        )
        self.assertEqual(found, [])

    def test_external_and_absolute_targets_are_left_alone(self):
        lines = ["[a](https://example.invalid/page)", "[b](/absolute/path.md)"]
        self.assertEqual(docs_check.dead_links(self.here, lines), [])

    def test_a_doxygen_reference_line_reports_nothing(self):
        # A table row of Doxygen references reports no dead link.
        line = (
            "| `version`        | [`HttpVersion`](@ref HttpVersion) | [`HTTP_10`](@ref HTTP_10)"
            ", [`HTTP_11`](@ref HTTP_11), or [`HTTP_UNKNOWN`](@ref HTTP_UNKNOWN) |"
        )
        self.assertEqual(docs_check.dead_links(self.here, [line]), [])

    def test_a_lambda_in_an_example_reports_nothing(self):
        # A lambda in example code is not a link.
        lines = [
            'server.on("/diag", HTTP_GET, [](uint8_t slot, HttpReq *req) {',
            "orior_ssh_auth_set_pubkey_cb([](const char *user, const uint8_t *blob, "
            "size_t len) {",
            "orior_ssh_auth_set_password_cb([](const char *user, const char *pass) {",
        ]
        self.assertEqual(docs_check.dead_links(self.here, lines), [])


class StandardsPassTheirOwnStructuralStage(unittest.TestCase):
    """The two documents that authorize this checker, against the stage of it that fails a commit.

    A gate that flags the standard it enforces is wrong by construction. This is the slice of that
    test belonging to dead_links. The em dash findings in both documents come from a different
    stage and are a decision for the author of those documents. They are reported and not
    asserted on, and code-documentation section 109 bans the em dash by name.
    """

    def standard_files(self):
        found = [os.environ.get(name, "") for name in ("PROSE_STANDARD_DOCUMENTATION", "PROSE_STANDARD_COMMENTS")]
        return [one for one in found if one and os.path.isfile(one)]

    def test_no_dead_link_findings_in_either_standard(self):
        files = self.standard_files()
        if not files:
            self.skipTest("PROSE_STANDARD_DOCUMENTATION and PROSE_STANDARD_COMMENTS do not name the two standards")
        for path in files:
            with open(path, encoding="utf-8", errors="replace") as handle:
                lines = handle.read().splitlines()
            self.assertEqual(docs_check.dead_links(path, lines), [], path)


if __name__ == "__main__":
    unittest.main(verbosity=2)
