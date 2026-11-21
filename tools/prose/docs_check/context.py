#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# The passages where the subject is the convention, and a normative keyword stays.
#

import re



# --------------------------------------------------------------------
# THE SUBJECT IS BRITISH, AND OTHER RUN-LEVEL CONTEXT
# --------------------------------------------------------------------
#
# A British convention is banned unless the subject IS British convention, and this is that clause.
# A passage about it has to be able to write the word it is about. "British English writes colour"
# is a correct sentence, and an alphabet-stage finding on it is the same error as a checker
# reporting a standard for naming its own bans.
#
# THIS EXTENDS THE COMPILED CONTEXT EXEMPTION QUOTED ALREADY IS. That tuple exists for exactly this
# question one instance at a time: the International Conference on Salish and Neighbouring Languages
# defines its own name that way and thirteen extraction headers cite it. Pointing this at the same
# shape is why it is two tuples of compiled patterns and not a new stage.
#
# BOUNDED TO THE ALPHABET TIER AND TO THE RUN, and that retains it from being a bypass. A
# paragraph mentioning Canada does not get to write `is what makes`. Only a convention finding goes
# quiet, and only inside the run carrying the subject.
#
# THE SUBJECT IS THE CONVENTION AND NOT THE COUNTRY, and the pattern says so. A bare \bbritish\b
# would exempt "British Telecom's optimisation", where the subject is a company and the convention
# is a live finding. Each arm names a word about writing: `english`, `convention`,
# `usage`, `variant`, `orthography`.
#
# THE COST OF THIS ARM IS ZERO FINDINGS SILENCED across every tree here, because nothing in them
# writes about the convention. The rule is the objective's own clause written down before a
# document needs it. utils/maint/prose/test_docs_check_exclusions.py re-derives that zero from the trees
# on disk, so a reader checks it instead of trusting this sentence.
BRITISH_SUBJECT = (
    re.compile(
        r"\b(?:british|american|canadian|commonwealth|oxford)\s+"
        r"(?:english|definition|convention|conventions|usage|variant|variants"
        r"|orthograph\w*|dictionar\w*)",
        re.IGNORECASE,
    ),
    re.compile(r"\ben[-_]GB\b"),
)

# A standard named by number. The terms around it are that standard's own field names: a published
# field defined the way its document defines it, quoted in a Doxygen brief with its default.
# Rewriting a field name makes a comment cite something that is not in the document it names.
#
# THIS IS A REWRITE ERROR AND NOT A SCAN EXEMPTION. Written as a scan exemption it silences the
# register gate in any tree that cites a standard in the majority of its comments, which is the type of
# tree it exists for. Bounded to the alphabet tier it silences ordinary prose that happens to sit
# in a paragraph citing a standard, and reaches no field name at all. A published field name is
# rarely a locale form in the first place, so there is nothing for the exemption to buy.
#
# A rule that silences correct findings and buys nothing is a net loss, and precision over
# recall means exactly that when it costs something. So it moved to fix_error, where it errors on a
# rewrite on a line quoting a named standard and costs nothing at all.
NAMED_STANDARD = re.compile(
    r"\b(?:RFC|STD|BCP|IEEE|ISO|IEC|ANSI|FIPS|NIST(?:\s+SP)?)\s*\d", re.IGNORECASE
)

# A normative keyword as RFC 2119 defines it, in capitals. Case matters and the pattern is compiled
# without IGNORECASE on purpose: "this may be null" is prose and "the sender MAY retransmit" is a
# requirement whose wording is not this tree's to edit.
#
# NOT A SCAN EXEMPTION. Nothing in BANNED matches a capitalized normative keyword. Exempting a
# run for carrying one would buy nothing and cost whatever else is in the run. It is a rewrite
# error and only that: a line carrying one is never rewritten, because reflowing a requirement is
# how a requirement stops being the one that was agreed.
RFC_2119 = re.compile(
    r"\b(?:MUST NOT|MUST|SHALL NOT|SHALL|SHOULD NOT|SHOULD|NOT RECOMMENDED|RECOMMENDED"
    r"|REQUIRED|MAY|OPTIONAL)\b"
)


CONTEXT_REASON = (
    "the subject of the passage is a writing convention, and a passage about one has "
    "to be able to write the word it is about"
)


def context_exempt(text):
    """Tiers that go quiet in one run because of what the run is about.

    Returns a frozenset of tier names. Empty for every run in every tree measured today, and that is
    the point: an exemption that fires often is a rule that was written too wide.
    """
    if any(one.search(text) for one in BRITISH_SUBJECT):
        return frozenset(("alphabet",))
    return frozenset()
