#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# What the prose standard says, checked instead of remembered.
#
#   Usage:  python utils/maint/prose/docs_check [root]
#
# Every check here is a defect that reaches a reader as a broken page. None of them is a matter of
# taste.
#
# AND IT ANSWERS THE QUESTIONS A REPAIR ASKS, which is the other half of being usable. A gate that
# names a fault and cannot say what belongs instead leaves the writer guessing, and a guess is how
# a repair introduces the next finding. readings.py holds them and USAGE lists them: --pick asks
# the voice oracle what this writing puts in a place, --offlist names every word in the tree the
# oracle does not approve, --show prints the lines the findings sit on, --slack says which ratchet
# ceilings nothing reaches, and --holes reads a diff for sentences a repair broke.
#
# Four instruments answer the same way: --rate (rate.py), --plain (plain.py), --sample (sample.py)
# and --harmonics (harmonics.py). None of the four is imported here. harmonics.py pulls numpy in and
# resolves the repository with git when it loads, and a scan that asked for none of them would pay
# for that on every run. readings.py imports each one at the moment it is asked for.
#
# EXIT STATUS. 0 when nothing breaking was found, 1 when something breaking was, 2 when no file was
# read at all, 3 when a file rose above its ratchet ceiling, 4 when the run was asked for something
# it cannot do. Never a count: counts wrap at 256, and 256 findings would report success.
#
# Prose findings print and exit 0. A hook nobody can satisfy is a hook somebody turns off, so only
# the tier that stops a commit reaches the status. Ask the instrument, do not model it, and that
# applies to this header as much as to anything else.
#
# WHAT THIS TOOL IS TO THE REST OF THE TREE. Both writing standards name it by path and hand it the
# list: code-documentation section 145 and code-comments section 208. Where it is retained is not what
# it governs. The table is the standard for every .md page, every .py, .c and .h comment and every
# .tex body in every repository here.
#
# THE TWO TIERS. Read the note above AUTHORITY for the rule that assigns them and for why the regex
# never decides. TIER A is a construction one of the two standards bans in a sentence, tree-wide
# with no opt-in. TIER B is vocabulary, scored against what it costs a human writer where that has
# been measured. Neither fails a build, in any repository, --strict included.
#
# THE FAULT THIS TABLE RETAINS MAKING runs in both directions and is a transcription failure rather
# than a judgement one. A rule is read for its EXAMPLES and coded as a list, or read for one
# INSTANCE and coded as a bare word. Against the first, code-documentation:143 states the escape:
# "a word that reads as a tic in one construction only is bounded to that construction." Against
# the second, :149 states the British ban as a pattern and means the shape, not the ten words it
# names. Transcribe the sentence, not the illustration beside it.
#
# A RULE TABLE DUPLICATED INTO THE TABLE THAT ENFORCES IT DRIFTS WITH NO SIGNAL AT ALL. LOCALE is
# spliced into BANNED and there is one copy. Retain it that way.
#
# Cite the sentence and not only the number: a number moves when a document is edited and the
# sentence is the rule.
# Re-check every rule against the sentence that states it, and run the standards' own illustrations
# through the checker, since a rule that misses its own example is a rule transcribed wrong.


import os

from .locale import LOCALE, LOCALE_NAMED, _ISE_STEMS, _ISE_TAIL, _OUR_TAIL, _OUR_WORDS  # noqa: F401
from .bans_outright import OUTRIGHT  # noqa: F401
from .bans_register import REGISTER  # noqa: F401
from .bans_shapes import SHAPES  # noqa: F401
from .bans_probe import PROBE  # noqa: F401
from .bans_detected import DETECTED  # noqa: F401
from .grammar import CONFIRM, CONSTRUCTIONS, GRAMMAR, GRAMMAR_AUTHORITY, GRAMMAR_CORPUS, GRAMMAR_RATE, ISMS, PLAIN, PLAIN_AUTHORITY, hides_verb, plain_pattern, plain_rows  # noqa: F401
from .bans import BANNED, WITHDRAWN  # noqa: F401
from .human_rate import HUMAN_RATE, stage_of  # noqa: F401
from .tier import AUTHORITY, COMMENT_ONLY, _ORPHANS, tier_of  # noqa: F401
from .quoting import EM_DASH, NAMED_IN_MARKDOWN, NAMED_SPAN, QUIET_CLOSE, QUIET_OPEN, QUOTED, quieted  # noqa: F401
from .ledger import Ledger  # noqa: F401
from .files import BUILD_NAMES, BUILD_SUFFIXES, CHECKED, HOOK_NAMES, SKIP_DIRS, build_file, checked_file, generated_chapter, retained_dirs, walk_markdown  # noqa: F401
from .verbatim import VERBATIM_MARKER, VERBATIM_ROOTS, _VERBATIM_CACHE, _VERBATIM_CEILING, verbatim_root  # noqa: F401
from .manifest import MANIFEST_NAMES, SIGNATURE_SUFFIX, _MANIFEST_CEILING, _MANIFEST_DIRS, _MANIFEST_INDEX, manifest_home, manifest_index, manifest_listed, reconcile_command  # noqa: F401
from .legal import COMMENT_FORMS, LEGAL, comment_blocks, comment_form, form_closes, legal_blank  # noqa: F401
from .generated import GENERATED_BY, GENERATED_CLOSE, GENERATED_OPEN, generated_regions  # noqa: F401
from .context import BRITISH_SUBJECT, CONTEXT_REASON, NAMED_STANDARD, RFC_2119, context_exempt  # noqa: F401
from .repository import DEFAULT_ROOTS, GIT_HANDOFF, REPOSITORY, git_env, git_say, main_checkout  # noqa: F401
from .refs import _REF_CACHE, manifests_covering, refs_for, tree_ref  # noqa: F401
from .fixes import FIX_TIERS, fix_error, fix_plan  # noqa: F401
from .markdown import ASCII_ART, CODE_SPAN, DECLARATOR_HEAD, DOXYGEN_TARGET, LINK, MARKDOWN_BOLD, MARKDOWN_ITALIC, MARKDOWN_RULE, ROW, SEPARATOR, dead_links, empty_tables, markdown_leftovers, path_candidate  # noqa: F401
from .index import PASSAGE, _ATOMIC, _COMPILED, _CONST, _FOLDS, _INDEX, _PARSER, _REPEATS, _fold, _required, candidates, folded, literal_index, present  # noqa: F401
from .scan import CORPUS, MARKER, attributed, banned_hits, banned_tokens, em_dashes, measured, runs  # noqa: F401
from .prose import CONTINUATION, SENTENCE_END, STRING_SPAN, comment_prose, hash_tail, marker_edges, near_marker, prose_only, tex_prose  # noqa: F401
from .ratchet import DEFAULT_RATCHET, RATCHET_HEADER, ratchet_read, ratchet_slack, ratchet_write, staged_paths  # noqa: F401
from .oracle import PLAIN_TABLE, SHORTEST, DISPLAYED, TABLES, WEB_TABLE, WORD, WORD_TABLE, adjacent, between, counted, offlist, oracle_roots, pick, plain_for  # noqa: F401
from .holes import HOLE, diff_holes  # noqa: F401
from .printed import REPORTING, printed_hits, printed_strings  # noqa: F401
from .punctuation import SUSPECT, smart_quotes  # noqa: F401
from .changed import HUNK, added_lines, changed_paths, changed_scope, git_lines, on_changed, untracked_lines  # noqa: F401
from .readings import USAGE, reading, show_holes, show_lines, show_offlist, show_pick, show_plain_for, show_slack, words_given  # noqa: F401
from .run import main, option_value  # noqa: F401



def source():
    """Every line this package is written in, for the guards that read it.

    Two tests open the checker's own source and search it, one for a report string that has to be
    absent and one for an exit rule that has to be present. A package has no single file to open
    through __file__, so the reading they want is offered here instead.
    """
    here = os.path.dirname(os.path.abspath(__file__))
    said = []
    for one in sorted(os.listdir(here)):
        if one.endswith(".py"):
            with open(os.path.join(here, one), encoding="utf-8") as handle:
                said.append(handle.read())
    return "\n".join(said)
