#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# Tests for what docs_check can open and what its alphabet stage can see.
#
#   Usage:  python utils/maint/prose/test_docs_check_coverage.py
#
# Two rules in one file, because either one alone hides the other: a build file
# such as CMakeLists.txt is opened by the extension list, and the alphabet stage reads the pattern
# the standard states and not a list of literals.
#
# A green run says the tool opens these files and sees these definitions. It does not say a hook runs
# it anywhere, or that a repository's roots reach a given file.

import os
import re
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import docs_check


# ============================================================================
# GROUND TRUTH FOR THE ALPHABET STAGE
# ============================================================================
#
# Plain literals, written by hand, sharing no code with the patterns under test. "The stage reaches
# N of N" is then two independent enumerations agreeing, instead of a regex agreeing with itself.
#
# Held as a tuple of single-quoted strings, deliberately. A triple-quoted block here would be a
# docstring, prose_only retains docstrings, and this tree's own run would then report every word in
# the list as a British variant, against the file that tests for British variants.
BRITISH_FORMS = (
    "initialise",
    "initialised",
    "initialises",
    "initialising",
    "initialiser",
    "initialisers",
    "initialisation",
    "optimise",
    "optimised",
    "optimises",
    "optimising",
    "optimiser",
    "optimisation",
    "optimisations",
    "recognise",
    "recognised",
    "recognises",
    "recognising",
    "recognisable",
    "normalise",
    "normalised",
    "normalises",
    "normalising",
    "normalisation",
    "serialise",
    "serialised",
    "serialises",
    "serialising",
    "serialisation",
    "authorise",
    "authorised",
    "authorises",
    "authorising",
    "authorisation",
    "synchronise",
    "synchronised",
    "synchronises",
    "synchronising",
    "synchronisation",
    "minimise",
    "minimised",
    "minimises",
    "minimising",
    "maximise",
    "maximised",
    "maximises",
    "maximising",
    "summarise",
    "summarised",
    "summarises",
    "summarising",
    "standardise",
    "standardised",
    "standardising",
    "utilise",
    "utilised",
    "utilises",
    "utilising",
    "categorise",
    "categorised",
    "categorising",
    "organise",
    "organised",
    "organises",
    "organising",
    "organisation",
    "organisations",
    "parenthesise",
    "parenthesised",
    "parenthesises",
    "specialise",
    "specialised",
    "generalise",
    "generalised",
    "finalise",
    "finalised",
    "customise",
    "customised",
    "sanitise",
    "sanitised",
    "randomise",
    "randomised",
    "localise",
    "localised",
    "realise",
    "realised",
    "emphasise",
    "emphasised",
    "amortise",
    "amortised",
    "vectorise",
    "vectorised",
    "devirtualise",
    "devirtualises",
    "parallelise",
    "parallelised",
    "centralise",
    "centralised",
    "materialise",
    "materialised",
    "analyse",
    "analysed",
    "analyses",
    "analysing",
    "analyser",
    "analysers",
    "paralyse",
    "paralysed",
    "catalyse",
    "catalysed",
    "dialyse",
    "dialysed",
    "behaviour",
    "behaviours",
    "behavioural",
    "colour",
    "colours",
    "coloured",
    "colouring",
    "colourful",
    "neighbour",
    "neighbours",
    "neighbouring",
    "neighbourhood",
    "favour",
    "favours",
    "favoured",
    "favourite",
    "honour",
    "honours",
    "honoured",
    "honouring",
    "humour",
    "humours",
    "labour",
    "labours",
    "laboured",
    "armour",
    "armoured",
    "rumour",
    "rumours",
    "vapour",
    "vapours",
    "odour",
    "odours",
    "harbour",
    "harbours",
    "savour",
    "savoured",
    "valour",
    "endeavour",
    "endeavours",
    "parlour",
    "splendour",
    "candour",
    "fervour",
    "rigour",
    "vigour",
    "tumour",
    "tumours",
    "clamour",
    "demeanour",
    "saviour",
    "flavour",
    "flavours",
    "flavoured",
    "ardour",
    "centre",
    "centres",
    "centred",
    "theatre",
    "theatres",
    "fibre",
    "fibres",
    "litre",
    "litres",
    "metre",
    "metres",
    "kilometre",
    "kilometres",
    "millimetre",
    "millimetres",
    "micrometre",
    "calibre",
    "calibres",
    "sabre",
    "sombre",
    "spectre",
    "lustre",
    "meagre",
    "manoeuvre",
    "manoeuvred",
    "sceptre",
    "labelled",
    "labelling",
    "labeller",
    "modelled",
    "modelling",
    "modeller",
    "signalled",
    "signalling",
    "travelled",
    "travelling",
    "traveller",
    "cancelled",
    "cancelling",
    "levelled",
    "levelling",
    "totalled",
    "totalling",
    "fuelled",
    "fuelling",
    "dialled",
    "dialling",
    "marvelled",
    "counselled",
    "counselling",
    "equalled",
    "equalling",
    "spiralled",
    "spiralling",
    "tunnelled",
    "tunnelling",
    "quarrelled",
    "jewelled",
    "fulfil",
    "fulfils",
    "fulfilment",
    "enrol",
    "enrols",
    "enrolment",
    "instal",
    "instals",
    "instalment",
    "skilful",
    "skilfully",
    "wilful",
    "wilfully",
    "enthral",
    "appal",
    "distil",
    "distils",
    "instil",
    "instils",
    "defence",
    "defences",
    "offence",
    "offences",
    "pretence",
    "pretences",
    "licence",
    "licences",
    "practise",
    "practised",
    "practises",
    "practising",
    "catalogue",
    "catalogues",
    "catalogued",
    "analogue",
    "analogues",
    "programme",
    "programmes",
    "whilst",
    "amongst",
    "grey",
    "greyscale",
    "artefact",
    "artefacts",
    "aluminium",
    "sulphur",
    "storey",
    "storeys",
    "tyre",
    "tyres",
    "cheque",
    "cheques",
    "draught",
    "draughts",
    "mould",
    "moulds",
    "speciality",
    "jewellery",
    "woollen",
    "aeroplane",
    "moustache",
    "pyjamas",
    "kerb",
    "plough",
    "gaol",
)

GROUND_TRUTH = re.compile(
    r"\b(?:%s)\b" % "|".join(sorted(set(BRITISH_FORMS), key=len, reverse=True)),
    re.IGNORECASE,
)


def forms_present(tree):
    """Every ground-truth form that occurs in the prose view of a tree, with how many sites.

    Reads through docs_check's own extractor. A word inside code is then not counted, and the two
    enumerations are measuring the same text.
    """
    found = {}
    for path in docs_check.walk_markdown([tree]):
        with open(path, encoding="utf-8", errors="replace") as handle:
            lines = handle.read().splitlines()
        for line in docs_check.prose_only(path, lines):
            for hit in GROUND_TRUTH.finditer(line):
                word = hit.group(0).lower()
                found[word] = found.get(word, 0) + 1
    return found


def stage_reaches(word):
    """Whether the alphabet stage matches one word on its own."""
    return any(re.search(one, word, re.IGNORECASE) for one in docs_check.LOCALE)


def definition_findings(path):
    """Every alphabet-stage finding in one file, as (line, matched text)."""
    with open(path, encoding="utf-8", errors="replace") as handle:
        lines = handle.read().splitlines()
    said = docs_check.prose_only(path, lines)
    found = []
    for at, pattern, token in docs_check.banned_hits(
        said,
        quotations=path.endswith(".md"),
        comments=not path.endswith((".md", ".tex")),
    ):
        if docs_check.tier_of(pattern) == "alphabet":
            found.append((at, token.lower()))
    return sorted(found)


class BuildFilesAreReadAtAll(unittest.TestCase):
    """A build file is opened, and reading nothing is never reported as success.

    A run that reads no file prints "0 file(s) checked" and exits 2, and that sentinel is the only
    part of the output telling it apart from a clean run.
    """

    def test_a_cmakelists_is_selected_by_its_name(self):
        # The one selection rule an extension list can never express. CMakeLists.txt has an
        # extension and it is .txt, which this tool does not read and should not start reading.
        self.assertTrue(docs_check.build_file("some/where/CMakeLists.txt"))
        self.assertTrue(docs_check.checked_file("some/where/CMakeLists.txt"))
        self.assertFalse(docs_check.checked_file("some/where/NOTES.txt"))

    def test_a_git_hook_is_selected_although_it_has_no_extension(self):
        # A hook has no extension, and a sweep by extension drops it and the gate with it. HOOK_NAMES
        # exists for that, and orior retains three of these under roots it already scans.
        for name in ("pre-commit", "commit-msg", "pre-push"):
            self.assertTrue(docs_check.build_file(os.path.join("hooks", name)), name)

    def test_the_build_suffixes_are_all_selected(self):
        for suffix in docs_check.BUILD_SUFFIXES:
            self.assertTrue(docs_check.checked_file("build" + suffix), suffix)

    def test_the_default_run_reads_build_files(self):
        # Asserted against the roots this repository actually scans. The selection rule is
        # measured where it has to work and not only against a made-up path.
        roots = list(docs_check.DEFAULT_ROOTS)
        got = [
            one for one in docs_check.walk_markdown(roots) if docs_check.build_file(one)
        ]
        types = sorted(
            set(
                (
                    os.path.basename(one)
                    if os.path.basename(one) in docs_check.BUILD_NAMES
                    or os.path.basename(one) in docs_check.HOOK_NAMES
                    else os.path.splitext(one)[1]
                )
                for one in got
            )
        )
        print(
            "\n  build files under this tree's own roots: %d, of types %s"
            % (len(got), types)
        )
        self.assertGreater(
            len(got), 0, "no build file was selected under this tree's own roots"
        )
        self.assertIn("CMakeLists.txt", types)
        self.assertIn("pre-commit", types)


class TheCommentExtractorForTheHashForm(unittest.TestCase):
    """The `#` comment, which shell, CMake, YAML and make all share, and the traps in finding it."""

    def test_a_hash_inside_a_string_is_not_a_comment(self):
        self.assertEqual(docs_check.hash_tail('message("a # b")'), "")
        self.assertEqual(docs_check.hash_tail("set(x 'a # b')"), "")

    def test_shell_argument_arithmetic_is_not_a_comment(self):
        # Without the open-a-word rule, `$#` and `${#name}` read as comment markers and a run of
        # argument handling gets scanned as prose.
        self.assertEqual(docs_check.hash_tail('if [ "$#" -lt 2 ]; then'), "")
        self.assertEqual(docs_check.hash_tail("len=${#name}"), "")

    def test_a_comment_after_code_is_found(self):
        self.assertEqual(docs_check.hash_tail("set(x 1)  # the reason"), "# the reason")
        self.assertEqual(docs_check.hash_tail("# at the start"), "# at the start")

    def test_line_numbers_are_preserved(self):
        # A finding has to name the line a reader opens. Blanked and never dropped.
        said = docs_check.comment_prose(["code()", "# one", "code()", "# two"])
        self.assertEqual(len(said), 4)
        self.assertEqual(said[1], "# one")
        self.assertEqual(said[0], "")

    def test_a_shebang_is_not_prose(self):
        said = docs_check.comment_prose(["#!/usr/bin/env bash", "# a real comment"])
        self.assertEqual(said[0], "")
        self.assertEqual(said[1], "# a real comment")

    def test_the_powershell_block_comment_is_read(self):
        said = docs_check.comment_prose(["<#", "the note", "#>", "code"])
        self.assertEqual(said[1], "the note")
        self.assertEqual(said[3], "")

    def test_the_extractor_is_reached_through_prose_only(self):
        # The dispatch and not only the helper, because a caller that has to know which extractor to
        # call is a caller that will get it wrong. submission_check.py imports CHECKED and
        # prose_only and never asks what type of file it has.
        said = docs_check.prose_only(
            "x/CMakeLists.txt", ["set(a 1)", "# undefined behaviour"]
        )
        self.assertEqual(said[0], "")
        self.assertIn("behaviour", said[1])


class TheAlphabetStageIsAPatternAndNotAList(unittest.TestCase):
    """code-documentation:149 states the rule as a shape.

    docs-check: quoting
    "Never a British variant, and the ban is on the pattern rather than on a list: no `-ise` or
    `-isation` where American takes `-ize` or `-ization`, no `-our` for `-or`, no `-re` for `-er`,
    no doubled `l` in `modelled`, `labelled`, `signalled`."
    docs-check: end quoting
    """

    def test_the_stage_is_spliced_into_the_table_that_enforces_it(self):
        # LOCALE is the single copy of the definition patterns, spliced into BANNED. A pattern in
        # LOCALE that BANNED does not hold reports nothing, and two copies can be edited apart with
        # no signal at all.
        for pattern in docs_check.LOCALE:
            self.assertIn(
                pattern,
                docs_check.BANNED,
                "a definition pattern that BANNED does not hold reports nothing: %r"
                % pattern,
            )
        self.assertEqual(len(set(docs_check.LOCALE)), len(docs_check.LOCALE))

    def test_the_first_ten_retain_their_human_rate_keys_character_for_character(self):
        # Nine of the ten are keys in HUMAN_RATE, which is keyed by the pattern string itself.
        # Rewriting one to fold it into a general arm takes a measured rate out of
        # submission_check.py's table with nothing left to show the rate. `whilst` carries no rate,
        # because it has no hit in the papers HUMAN_RATE is counted over.
        first_ten = (
            r"\blabelled\b",
            r"\bmodelled\b",
            r"\bneighbour",
            r"\bbehaviour",
            r"\bcolour",
            r"\bcentre\b",
            r"\bwhilst\b",
            r"\bamongst\b",
            r"\borganis(e|es|ed|ing|ation|ations)\b",
            r"\banalyse(s|d)?\b",
        )
        self.assertEqual(docs_check.LOCALE[: len(first_ten)], first_ten)
        dead = [one for one in first_ten if one not in docs_check.HUMAN_RATE]
        self.assertEqual(
            dead, [r"\bwhilst\b"], "a measured rate lost its key: %s" % (dead,)
        )

    def test_every_definition_pattern_is_the_alphabet_tier(self):
        for pattern in docs_check.LOCALE:
            self.assertEqual(docs_check.tier_of(pattern), "alphabet", pattern)
            self.assertNotIn(pattern, docs_check.AUTHORITY, pattern)

    def test_each_shape_the_standard_names_has_an_arm(self):
        # The four shapes in that one sentence, each checked through a word the sentence does not
        # itself contain. The arm is tested and not the literal.
        for word in (
            "categorising",
            "tokenisation",
            "harbour",
            "kilometres",
            "tunnelled",
        ):
            self.assertTrue(stage_reaches(word), "no arm reaches %r" % word)

    def test_the_twelve_american_definitions_the_standard_names_are_left_alone(self):
        # code-documentation:149 names these as the correct forms. A rule that reports one of them
        # is a rule enforcing the opposite of what it cites.
        for word in (
            "initialize",
            "behavior",
            "synchronize",
            "optimization",
            "devirtualize",
            "analog",
            "color",
            "center",
            "defense",
            "gray",
            "catalog",
            "license",
        ):
            self.assertFalse(
                stage_reaches(word),
                "the stage reports an American definition: %r" % word,
            )


class PrecisionOverRecall(unittest.TestCase):
    """Each of these is named, and none of them is reported.

    A gate that reports everything and leaves a person to sort it gets switched off inside a week.
    """

    def test_english_words_ending_in_ise_are_not_british(self):
        # The s belongs to the stem here, not to the Greek -ize suffix. American writes all of
        # these with an s too.
        for word in (
            "advise",
            "advised",
            "adviser",
            "revise",
            "devise",
            "supervise",
            "televise",
            "improvise",
            "exercise",
            "exercises",
            "excise",
            "concise",
            "precise",
            "imprecise",
            "incise",
            "circumcise",
            "promise",
            "promises",
            "premise",
            "surmise",
            "demise",
            "compromise",
            "despise",
            "franchise",
            "merchandise",
            "paradise",
            "treatise",
            "expertise",
            "enterprise",
            "comprise",
            "surprise",
            "apprise",
            "reprise",
            "chastise",
            "advertise",
        ):
            self.assertFalse(stage_reaches(word), "reported as British: %r" % word)

    def test_the_shapes_the_arm_errors_without_naming_them(self):
        # The consonant class and the two-character floor carry these. None of them has to be
        # written into a list that a later reader has to maintain.
        for word in (
            "noise",
            "raise",
            "praise",
            "braise",
            "chaise",
            "guise",
            "disguise",
            "cruise",
            "bruise",
            "poise",
            "tortoise",
            "porpoise",
            "malaise",
            "appraise",
            "rise",
            "arise",
            "prise",
            "wise",
            "wiser",
            "otherwise",
            "likewise",
            "clockwise",
            "bitwise",
            "stepwise",
            "pairwise",
            "anise",
        ):
            self.assertFalse(stage_reaches(word), "reported as British: %r" % word)

    def test_a_safe_stem_is_matched_from_the_end_of_a_word(self):
        # Compounds. An exemption anchored at the start of the word misses every one of them.
        for word in (
            "madvise",
            "keycompromise",
            "aacompromise",
            "saxexerciser",
            "unadvisable",
        ):
            self.assertFalse(stage_reaches(word), "reported as British: %r" % word)

    def test_anis_and_mortis_are_not_safe_stems(self):
        # These two must not be added. `anis` would exempt `organise`, which ends in those five
        # characters, and `mortis` would exempt `amortise`, which ends in those six. A stem matched
        # from the end has to be checked against the British words that end the same way.
        self.assertTrue(stage_reaches("organise"))
        self.assertTrue(stage_reaches("amortise"))
        self.assertTrue(stage_reaches("amortised"))

    def test_american_doubled_l_words_are_not_reported(self):
        # Why the doubled-l rule is a list. American doubles the l in all of these, and a general
        # rule would report a quarter of the verbs in the tree.
        for word in (
            "controlled",
            "controlling",
            "installed",
            "installing",
            "enrolled",
            "called",
            "filled",
            "pulled",
            "rolled",
            "billed",
        ):
            self.assertFalse(stage_reaches(word), "reported as British: %r" % word)

    def test_programmed_and_programming_are_american(self):
        self.assertFalse(stage_reaches("programmed"))
        self.assertFalse(stage_reaches("programming"))
        self.assertTrue(stage_reaches("programme"))

    def test_the_our_arm_errors_the_short_words_and_holds_the_long_ones(self):
        for word in (
            "our",
            "your",
            "four",
            "hour",
            "tour",
            "pour",
            "sour",
            "flour",
            "scour",
            "dour",
            "amour",
            "devour",
            "contour",
            "contours",
            "detour",
            "glamour",
            "byhour",
            "numberofcontours",
            "encourage",
            "journal",
            "resourceful",
        ):
            self.assertFalse(stage_reaches(word), "reported as British: %r" % word)
        for word in (
            "ardour",
            "candour",
            "behaviour",
            "behavioural",
            "colourful",
            "favourite",
            "neighbourless",
            "labourer",
            "harbour",
            "endeavour",
        ):
            self.assertTrue(stage_reaches(word), "not reached: %r" % word)

    def test_the_single_l_arm_stops_at_the_word_boundary(self):
        # `fulfilled` and `appalling` are defined the same on both sides of the Atlantic. Only the
        # forms that differ are in the arm, and the word boundary retains the rest out.
        self.assertTrue(stage_reaches("fulfil"))
        self.assertTrue(stage_reaches("fulfilment"))
        self.assertFalse(stage_reaches("fulfilled"))
        self.assertFalse(stage_reaches("appalling"))

    def test_no_isable_arm(self):
        # An -isable arm must not be added. It matches `controldisable` and every other compound
        # ending in `disable`.
        for word in (
            "disable",
            "disabled",
            "disabling",
            "controldisable",
            "autodisabled",
        ):
            self.assertFalse(stage_reaches(word), "reported as British: %r" % word)

    def test_the_stage_reports_nothing_in_either_standard(self):
        # The governing test, in the alphabet stage's slice. A gate that flags the documents that
        # authorize it is wrong by construction, and a new pattern arm is the likely way to
        # break that. The standards write `initialise` and `behaviour` by name at :149 and :157,
        # inside backticks. The document is naming a form there, and a name is not a use.
        standards = {
            "code-documentation": os.environ.get("PROSE_STANDARD_DOCUMENTATION", ""),
            "code-comments": os.environ.get("PROSE_STANDARD_COMMENTS", ""),
        }
        checked = 0
        for name, path in standards.items():
            if not path or not os.path.isfile(path):
                continue
            checked += 1
            got = definition_findings(path)
            self.assertEqual(got, [], "%s reported on definition: %s" % (name, got))
        if not checked:
            self.skipTest("PROSE_STANDARD_DOCUMENTATION and PROSE_STANDARD_COMMENTS do not name the two standards")


class TheOtherStagesAreUntouched(unittest.TestCase):
    """Guards for the structural stage and the tiers. A rebuild drops them without deciding to."""

    def test_the_structural_gate_errors_a_doxygen_reference(self):
        self.assertFalse(docs_check.path_candidate("@ref HTTP_10"))
        self.assertFalse(
            docs_check.path_candidate("const char *user, const char *pass")
        )
        self.assertTrue(docs_check.path_candidate("docs/README.md"))

    def test_the_tiers_name_their_sections(self):
        self.assertEqual(docs_check.tier_of(r"\brather\b"), "A")
        self.assertIn("code-documentation:110", docs_check.AUTHORITY[r"\brather\b"])

    def test_a_build_file_comment_fails_no_commit(self):
        # The structural stage fails a commit, and a plain comment in a build file must not fail
        # one. em_dashes is the only structural rule that runs on every extension, .py, .c and .h
        # among them.
        said = docs_check.prose_only("x/build.sh", ["# a plain comment", "echo hi"])
        self.assertEqual(docs_check.em_dashes(said), [])
        self.assertEqual(docs_check.em_dashes(["# " + chr(0x2014)]), [(1, "em dash")])


if __name__ == "__main__":
    unittest.main(verbosity=2)
