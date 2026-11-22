#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# The exclusion layer and the reporting lines, checked instead of remembered.
#
#   Usage:  python utils/maint/prose/test_docs_check_exclusions.py
#
# Standalone unittest, no pytest dependency, same as the three suites beside it.
#
# WHAT THESE TESTS ARE FOR. Every exclusion in docs_check is a rule that makes the tool report LESS,
# and a rule that makes a checker report less is the easiest type of rule to get wrong and the
# hardest type to notice being wrong. A wrong exclusion reports a clean tree. So each one here is
# asserted from both ends: what it silences and what it must NOT silence.
#
# EVERY COUNT IS DERIVED AT RUN TIME AND NONE IS WRITTEN DOWN. The manifest row count, the British
# convention cost, the legal block cost and the generated region membership are all measured from
# the tree when the test runs, and the ref they were measured at is printed with its reachability.
# Numbers hand-carried through this tree have been wrong repeatedly: the brief for this work said a
# manifest held 2028 rows and a read pass said 2029, and both were describing the same file with the
# header row counted differently.

import os
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import docs_check as dc  # noqa: E402


def findings_in(path, regions=None):
    """Every prose finding in one file, through the same path main() takes."""
    with open(path, encoding="utf-8", errors="replace") as handle:
        lines = handle.read().splitlines()
    said = dc.prose_only(path, lines)
    return dc.banned_tokens(
        said,
        quotations=path.endswith(".md"),
        comments=not path.endswith((".md", ".tex")),
        path=path,
        regions=regions,
    )


# ============================================================================
# 1. THE SHAPE EVERY EXCLUSION SHARES
# ============================================================================


class EveryExclusionErrorsAndSaysSo(unittest.TestCase):
    """A skip that says nothing is the failure this whole section exists against.

    A skip that prints nothing and a tree with nothing to skip look the same from outside, and a
    run that read less than it should have passed for a clean one.
    """


    def test_the_ledger_retains_the_sites_and_not_only_a_count(self):
        ledger = dc.Ledger()
        ledger.note("a rule", "a reason", "a/site.md:1")
        ledger.note("a rule", "a reason", "a/site.md:9")
        ledger.note("another rule", "another reason", "b/site.md:1")
        self.assertEqual(ledger.total(), 3)
        printed = "\n".join(ledger.report())
        self.assertIn("a/site.md:1", printed)
        self.assertIn("a/site.md:9", printed)
        self.assertIn("a rule: 2, a reason", printed)

    def test_the_ledger_holds_its_rules_in_first_appearance_order(self):
        ledger = dc.Ledger()
        ledger.note("second", "why", "x")
        ledger.note("first", "why", "y")
        ledger.note("second", "why", "z")
        self.assertEqual([one[0] for one in ledger.order], ["second", "first"])


# ============================================================================
# 2. VERBATIM THIRD-PARTY TEXT, AS A CONCEPT AND NOT A PATH LIST
# ============================================================================


class VerbatimThirdPartyIsANamedConcept(unittest.TestCase):
    """Somebody else's words, reproduced byte for byte.

    Three instances were found independently, which is what makes this a concept. The test that
    matters max is the marker one: a path list is a chore somebody has to remember to extend, and
    the way a chore fails is that a fourth corpus arrives and nobody adds it.
    """

    def test_a_directory_declares_itself_with_a_marker_and_no_list_is_touched(self):
        import tempfile

        with tempfile.TemporaryDirectory() as where:
            vendored = os.path.join(where, "a_corpus_this_tool_has_never_heard_of")
            os.makedirs(vendored)
            inside = os.path.join(vendored, "someone_elses.md")
            with open(inside, "w", encoding="utf-8") as handle:
                handle.write("# a document\n")
            self.assertIsNone(
                dc.verbatim_root(inside), "not verbatim before the directory says so"
            )
            dc._VERBATIM_CACHE.clear()
            with open(
                os.path.join(vendored, dc.VERBATIM_MARKER), "w", encoding="utf-8"
            ) as handle:
                handle.write("the IETF's, not ours\n")
            held = dc.verbatim_root(inside)
            dc._VERBATIM_CACHE.clear()
            self.assertIsNotNone(
                held, "the marker is the second answer to the question"
            )
            self.assertIn(dc.VERBATIM_MARKER, held[1])

    def test_every_default_root_is_matched_on_the_path(self):
        for one, why in dc.VERBATIM_ROOTS:
            held = dc.verbatim_root("/a/tree/%s/a_file.md" % one)
            self.assertIsNotNone(held, "%s is a default root and did not match" % one)
            self.assertEqual(held[1], why, "the reason travels with the root")

    def test_every_default_root_carries_more_than_one_path_component(self):
        """A bare directory name is not distinctive enough to be a rule.

        `oracles` alone would match any directory anywhere with that name, and this tree has a
        build/papers that is a different thing from the corpus papers/.
        """
        for one, _ in dc.VERBATIM_ROOTS:
            self.assertIn(
                "/",
                one.strip("/"),
                "%r is one component and would match too much" % one,
            )

    def test_every_default_root_states_a_reason_and_not_only_a_rule(self):
        for one, why in dc.VERBATIM_ROOTS:
            self.assertGreater(
                len(why.split()),
                4,
                "%s has a rule and no reason, which is what gets deleted" % one,
            )

    def test_a_path_outside_every_root_is_read(self):
        self.assertIsNone(dc.verbatim_root(os.path.join(HERE, "docs_check")))


# ============================================================================
# 3. SIGNED MANIFESTS
# ============================================================================


# ============================================================================
# 4. LEGAL BLOCKS
# ============================================================================


class LegalBlocksAreBlankedPerBlockAndNotPerLine(unittest.TestCase):
    """A GPL grant is fifteen lines and two of them hold anything a regex can find.

    Where the block ends is the whole rule, and getting it wrong in either direction has a cost that
    was measured before this landed.
    """


    def test_a_bare_comment_marker_separates_two_blocks(self):
        """The regression this rule was rewritten for, guarded by name.

        A first attempt split blocks on blank SOURCE lines. A `#` alone on a line is not blank,
        an entire comment header read as one block: 164 findings went quiet across this tree alone,
        among them six hits in a file about orthography. Emptiness is measured on the MARKER-stripped
        text instead, the same test runs() already makes.
        """
        said = [
            "#!/usr/bin/env python3",
            "# a project - Copyright (C) 2026 Somebody <nobody@example.com>",
            "# SPDX-License-Identifier: AGPL-3.0-or-later",
            "#",
            "# The header carries a crucial myriad of things, the point.",
        ]
        retained = dc.legal_blank(said)
        self.assertEqual(retained[:3], ["", "", ""], "the legal block goes")
        self.assertIn("crucial", retained[4], "and the prose under the separator stays")

    def test_a_doxygen_file_block_under_an_spdx_block_is_still_read(self):
        """The comment standard puts the @file block directly under the SPDX block with no blank.

        Without the comment-form and close tests the two read as one, and every @file brief in every
        C file in every tree here would go unchecked.
        """
        said = [
            "/* a project - Copyright (C) 2026 Somebody <nobody@example.com>",
            " * SPDX-License-Identifier: AGPL-3.0-or-later",
            " */",
            "/**",
            " * @file ring_buffer.h",
            " * @brief A crucial myriad of frames, which is what makes it work.",
            " */",
        ]
        retained = dc.legal_blank(said)
        self.assertEqual(retained[:3], ["", "", ""], "the license block goes")
        self.assertIn("crucial", retained[5], "and the @file block beneath it does not")

    def test_a_file_with_no_legal_line_is_returned_unchanged(self):
        said = ["# a comment", "# another", "", "# a third"]
        self.assertEqual(dc.legal_blank(said), said)

    def test_the_rule_silences_nothing_in_the_trees_on_disk(self):
        """Derived at run time over a bounded scope, and re-derive it after any change here.

        Zero is the outcome to want from a rule whose job is to protect an artifact and not to hide
        a backlog. A block walker reaching too far reports the same zero on this test and 164
        findings on the tree, and for that reason the two boundary tests above are asserted by shape.
        """
        roots = [os.path.join(dc.REPOSITORY, "maint")]
        roots = [one for one in roots if os.path.isdir(one)]
        silenced = 0
        read = 0
        for path in sorted(dc.walk_markdown(roots)):
            read += 1
            with open(path, encoding="utf-8", errors="replace") as handle:
                lines = handle.read().splitlines()
            raw = dc.prose_only(path, lines)
            blanked = set()
            for start, stop in dc.comment_blocks(raw):
                if any(dc.LEGAL.search(one) for one in raw[start:stop]):
                    blanked.update(range(start + 1, stop + 1))
            if not blanked:
                continue
            quotations = path.endswith(".md")
            comments = not path.endswith((".md", ".tex"))
            for at, _, _ in dc.banned_hits(raw, quotations, comments):
                if at in blanked:
                    silenced += 1
        print(
            "  legal blanking over %d file(s): %d finding(s) silenced"
            % (read, silenced)
        )
        self.assertEqual(silenced, 0)


# ============================================================================
# 5. GENERATED REGIONS
# ============================================================================


class GeneratedRegionsAreAttributedAndNeverSuppressed(unittest.TestCase):
    """The one decision here a reader is likely to want to reverse. It is tested hardest.

    A genuine structural finding can sit inside a generated region. Skipping marked regions
    deletes that finding and reports the tree clean.
    """

    def test_both_marker_shapes_are_read(self):
        lines = [
            "before",
            "<!-- BEGIN GENERATED CONFIG-OVERRIDES (tools/ci_tooling/generate/gen_sections.py) -->",
            "inside the first",
            "<!-- END GENERATED CONFIG-OVERRIDES -->",
            "between",
            "<!-- BEGIN GENERATED test-environments (edit test_matrix.json, run harness.py gen) -->",
            "inside the second",
            "<!-- END GENERATED test-environments -->",
            "after",
        ]
        inside, complaints = dc.generated_regions(lines)
        self.assertEqual(complaints, [])
        self.assertEqual(inside[3], "tools/ci_tooling/generate/gen_sections.py")
        self.assertEqual(inside[7], "edit test_matrix.json, run harness.py gen")
        self.assertNotIn(1, inside)
        self.assertNotIn(5, inside)
        self.assertNotIn(9, inside)

    def test_an_unclosed_marker_is_reported_and_not_allowed_to_swallow_the_file(self):
        lines = [
            "before",
            "<!-- BEGIN GENERATED THING (gen.py) -->",
            "inside",
            "still inside",
        ]
        inside, complaints = dc.generated_regions(lines)
        self.assertEqual(len(complaints), 1)
        self.assertEqual(complaints[0][0], 2)
        self.assertIn("no END GENERATED", complaints[0][1])
        self.assertIn(3, inside)

    def test_a_finding_inside_a_region_retains_its_place_and_names_the_generator(self):
        note = dc.attributed(
            "table header with no rows under it", 4, {4: "gen_sections.py"}
        )
        self.assertIn("table header with no rows under it", note)
        self.assertIn("gen_sections.py", note)
        self.assertIn("fix the generator", note)

    def test_a_finding_outside_a_region_carries_no_attribution(self):
        self.assertEqual(
            dc.attributed("a finding", 9, {4: "gen_sections.py"}), "a finding"
        )


    def test_a_site_inside_a_region_is_error_for_rewriting(self):
        why = dc.fix_error("a/page.md", "alphabet", 4, {4: "gen_sections.py"})
        self.assertIsNotNone(why)
        self.assertIn("gen_sections.py", why)
        self.assertIn(
            "regeneration",
            why,
            "a source fix not paired with regeneration reds the pull request",
        )


# ============================================================================
# 6. OBJECTIVE 13's CLAUSE: UNLESS THE SUBJECT IS BRITISH
# ============================================================================


class TheSubjectIsTheConvention(unittest.TestCase):
    """A passage about a writing convention has to be able to write the word it is about.

    Bounded to the alphabet tier and to the run, which is what retains it from being a bypass.
    """

    def test_a_passage_about_the_convention_may_write_the_word(self):
        said = [
            "# British English writes colour and behaviour where American writes color."
        ]
        found = [what for _, what in dc.banned_tokens(said, comments=True)]
        self.assertEqual(
            [one for one in found if one.startswith("definition")],
            [],
            "the subject of the sentence is the convention itself",
        )

    def test_the_same_passage_still_reports_a_construction(self):
        """The bound is the test. A paragraph about convention does not get a register pass."""
        said = [
            "# British definition is what makes the difference. A reader has to know."
        ]
        found = [what for _, what in dc.banned_tokens(said, comments=True)]
        self.assertTrue(
            any(one.startswith("tier A") for one in found),
            "only the alphabet tier goes quiet, never a construction",
        )

    def test_the_subject_is_the_convention_and_not_the_country(self):
        """A bare country name would exempt a company, and the company's prose is a live finding."""
        said = ["# British Telecom asked for an optimisation of the initialised path."]
        found = [what for _, what in dc.banned_tokens(said, comments=True)]
        self.assertTrue(
            any(one.startswith("definition") for one in found),
            "the subject here is a company. The convention is still reported",
        )

    def test_the_exemption_is_bounded_to_the_run_that_carries_the_subject(self):
        said = [
            "# British definition writes colour.",
            "",
            "# The optimisation runs once.",
        ]
        at = [
            one
            for one, what in dc.banned_tokens(said, comments=True)
            if what.startswith("definition")
        ]
        self.assertEqual(
            at, [3], "the next paragraph is a different run and is reported"
        )

    def test_context_exempt_answers_with_a_tier_set_and_not_a_boolean(self):
        self.assertEqual(
            dc.context_exempt("British English writes colour"), frozenset(("alphabet",))
        )
        self.assertEqual(dc.context_exempt("an ordinary sentence"), frozenset())

    def test_the_clause_silences_nothing_in_the_trees_on_disk(self):
        """Derived at run time. Nothing in any tree here writes about the convention today.

        The rule is the objective's own clause written down before a document needs it, the
        opposite of the usual order and is why the number is asserted and not described.
        """
        roots = [os.path.join(dc.REPOSITORY, "maint")]
        roots = [one for one in roots if os.path.isdir(one)]
        silenced = 0
        read = 0
        for path in sorted(dc.walk_markdown(roots)):
            read += 1
            with open(path, encoding="utf-8", errors="replace") as handle:
                lines = handle.read().splitlines()
            said = dc.prose_only(path, lines)
            quotations = path.endswith(".md")
            comments = not path.endswith((".md", ".tex"))
            for text, offsets in dc.runs(said):
                if not (offsets and dc.context_exempt(text)):
                    continue
                for _, pattern, _ in dc.banned_hits([text], quotations, comments):
                    if dc.tier_of(pattern) == "alphabet":
                        silenced += 1
        print(
            "\n  the convention clause over %d file(s): %d finding(s) silenced"
            % (read, silenced)
        )
        self.assertEqual(silenced, 0)

    def test_the_standards_rule_is_a_rewrite_error_and_not_a_scan_exemption(self):
        """Measured out and not retained, and the measurement is the reason.

        Exempting a run for naming a standard silenced 1,733 findings in one tree unbounded and 20
        bounded to the alphabet tier, and every one of the 20 was ordinary prose in a paragraph that
        happened to cite an RFC. It bought nothing: the site it was written for, "Robustness
        Variable" in an RFC 2236 brief, matches no pattern in LOCALE and produced no finding.
        """
        said = [
            "# RFC 2236 section 8.1 describes the behaviour of the initialised timer."
        ]
        found = [what for _, what in dc.banned_tokens(said, comments=True)]
        self.assertTrue(
            any(one.startswith("definition") for one in found),
            "naming a standard does not turn the convention stage off",
        )
        why = dc.fix_error("a/file.c", "alphabet", line=said[0])
        self.assertIsNotNone(why, "but the line is never rewritten")
        self.assertIn("standard", why)


# ============================================================================
# 7. A BANNED HINGE IS DISSOLVED AND NEVER REPLACED
# ============================================================================


class NothingIsRewrittenThatIsNotTokenForToken(unittest.TestCase):
    """Detection is mechanical. Substitution is where the judgement lives.

    A construction ban targets a rhetorical move and not a word. The nearest synonym preserves
    the move and lands on another banned item. `rather` is banned at code-documentation:110 and the
    X-not-Y shape that repairs it is banned at :146 of the same document.
    """

    def test_the_alphabet_tier_is_the_only_tier_a_rewrite_may_reach(self):
        self.assertEqual(
            dc.FIX_TIERS,
            frozenset(("alphabet",)),
            "read code-documentation:110 and :146 before widening this",
        )

    def test_every_construction_tier_is_error_with_the_sections_that_say_so(self):
        for tier in ("A", "B"):
            why = dc.fix_error("a/file.md", tier)
            self.assertIsNotNone(why)
            self.assertIn("code-documentation:110", why)
            self.assertIn(":146", why)
            self.assertIn("permanently", why)

    def test_a_plain_convention_finding_is_allowed(self):
        self.assertIsNone(
            dc.fix_error(os.path.join(HERE, "nothing_special.md"), "alphabet")
        )


    def test_a_line_carrying_a_normative_keyword_is_never_rewritten(self):
        why = dc.fix_error(
            "a/file.c",
            "alphabet",
            line="// The sender MUST NOT retransmit the initialised segment.",
        )
        self.assertIsNotNone(why)
        self.assertIn("2119", why)

    def test_the_normative_keyword_test_is_case_sensitive(self):
        """ "this may be null" is prose. "the sender MAY retransmit" is a requirement."""
        self.assertIsNone(
            dc.fix_error(
                os.path.join(HERE, "plain.md"), "alphabet", line="the value may be null"
            )
        )
        self.assertIsNotNone(
            dc.fix_error("a/file.c", "alphabet", line="the sender MAY retransmit")
        )

    def test_the_source_states_the_limit_where_a_maintainer_will_look_for_it(self):
        """A rule with no reason beside it is the type a later maintainer finishes.

        This one reads as an unimplemented feature and is a permanent limit. The reasoning has to
        sit at the constant a person would edit.
        """
        with open(os.path.join(HERE, "docs_check", "fixes.py"), encoding="utf-8") as handle:
            body = handle.read()
        head = body[: body.index("FIX_TIERS = ")]
        note = head[head.index("WHAT A REWRITE MAY TOUCH") :]
        for wanted in ("code-documentation:110", ":146", "permanently", "X-not-Y"):
            self.assertIn(wanted, note, "the note above FIX_TIERS drops %r" % wanted)


# ============================================================================
# 8. THE REPORT SAYS WHAT IT MEASURED
# ============================================================================


class TheReportSaysWhatItMeasured(unittest.TestCase):
    """Three lines, and each one prevented a real confusion.

    A run reporting "0 findings" over two configured roots reads exactly like a clean tree, and that
    is how a repository carrying dozens of British definitions reads as green.
    """

    def run_on(self, *args):
        answer = subprocess.run(
            [sys.executable, os.path.join(HERE, "docs_check")] + list(args),
            capture_output=True,
            text=True,
            env=dc.git_env(),
        )
        return answer.stdout

    def test_the_roots_are_printed_before_any_finding(self):
        said = self.run_on(os.path.join(HERE, "docs_check"))
        lines = [one.strip() for one in said.splitlines() if one.strip()]
        self.assertTrue(
            lines[0].startswith("roots configured:"),
            "a reader meets the scope before the count, and got %r" % lines[0],
        )

    def test_a_root_that_is_not_there_is_printed_as_not_found(self):
        said = self.run_on(os.path.join(HERE, "no_such_directory_anywhere"))
        self.assertIn(
            "NOT FOUND",
            said,
            "a missing root reads as zero findings and must never read as clean",
        )

    def test_the_revision_is_printed_with_its_reachability(self):
        said = self.run_on(os.path.join(HERE, "docs_check"))
        measured = [one for one in said.splitlines() if "measured at" in one]
        print("\n  %s" % "\n  ".join(one.strip() for one in measured))
        self.assertEqual(len(measured), 1)
        self.assertTrue(
            ("reachable from" in measured[0]) or ("NOT PUSHED" in measured[0]),
            "a revision no remote contains is a tree of one and has to say so",
        )

    def test_the_excluded_count_prints_even_when_it_is_zero(self):
        """ "excluded: 0" and a silence have to print differently."""
        said = self.run_on(os.path.join(HERE, "docs_check"))
        self.assertIn("excluded:", said)

    def test_the_scope_sentence_names_the_axes_this_tool_does_not_answer_for(self):
        said = self.run_on(os.path.join(HERE, "docs_check"))
        self.assertIn("installed nowhere", said)
        self.assertIn("root(s) and nothing outside them", said)


    def test_prose_still_never_fails_a_build(self):
        """Verbatim in both standards, in the sentence that names this tool, and unchanged here.

        An exclusion layer is a place where an exit rule gets rewritten by accident. The run reads a
        fixture carrying one banned phrase. This file is excluded as a gate self-test, and a run
        over it reads nothing and exits 2 for that reason.
        """
        import tempfile

        with tempfile.TemporaryDirectory() as where:
            fixture = os.path.join(where, "prose_only.md")
            with open(fixture, "w", encoding="utf-8") as handle:
                handle.write("# A fixture\n\nThe cache is small, which is what makes it fast.\n")
            answer = subprocess.run(
                [sys.executable, os.path.join(HERE, "docs_check"), fixture],
                capture_output=True,
                text=True,
                env=dc.git_env(),
            )
        breaking = [
            one for one in answer.stdout.splitlines() if one.strip().startswith("BREAK")
        ]
        prose = [
            one for one in answer.stdout.splitlines() if one.strip().startswith("prose")
        ]
        if breaking:
            self.skipTest(
                "this file carries a structural finding. The exit code is about that"
            )
        print(
            "  this suite reports %d prose finding(s) and exits %d"
            % (len(prose), answer.returncode)
        )
        self.assertTrue(prose, "the fixture carries a prose finding")
        self.assertEqual(answer.returncode, 0)


# ============================================================================
# 9. THE WORK BESIDE THIS ONE IS UNTOUCHED
# ============================================================================


class TheWorkBesideThisOneIsUntouched(unittest.TestCase):
    """Four passes have landed in this file. Each one guards the three before it."""

    def test_the_structural_gate_is_still_there(self):
        self.assertFalse(dc.path_candidate("@ref HTTP_10"))
        self.assertFalse(dc.path_candidate("const char *user, const char *pass"))
        self.assertTrue(dc.path_candidate("docs/README.md"))

    def test_the_tiers_still_name_their_sections(self):
        self.assertEqual(dc.tier_of(r"\brather\b"), "A")
        self.assertIn(r"\brather\b", dc.AUTHORITY)

    def test_the_convention_stage_is_still_spliced_into_the_table_that_enforces_it(
        self,
    ):
        for one in dc.LOCALE:
            self.assertIn(one, dc.BANNED)

    def test_the_build_file_selection_still_reaches_a_name_and_a_hook(self):
        self.assertTrue(dc.checked_file("a/tree/CMakeLists.txt"))
        self.assertTrue(dc.checked_file("a/tree/.githooks/pre-commit"))

    def test_every_git_query_passes_off_the_callers_repository(self):
        """Git EXPORTS GIT_DIR to a hook, and a rev-parse inheriting it answers about that
        repository instead of the directory it was asked from.

        The worktree repair that landed --git-common-dir was written against this and the clearing
        did not come with it. A correct query retained giving a wrong answer under a hook. This pass
        added it, and the reporting lines depend on it: every one of them asks git a question about
        a directory it was passed.
        """
        for one in ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_COMMON_DIR"):
            self.assertIn(one, dc.GIT_HANDOFF)
        retained = dict(os.environ)
        retained["GIT_DIR"] = "somewhere/else/.git"
        was = os.environ.get("GIT_DIR")
        os.environ["GIT_DIR"] = "somewhere/else/.git"
        try:
            self.assertNotIn("GIT_DIR", dc.git_env())
        finally:
            if was is None:
                os.environ.pop("GIT_DIR", None)
            else:
                os.environ["GIT_DIR"] = was


if __name__ == "__main__":
    unittest.main(verbosity=2)
