#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# The smart-punctuation family, which is wider than the em dash.
#
# The quote characters below are the substitutions a word processor, a web paste or a well-meaning
# editor makes, and each has an ASCII form meaning exactly the same thing. The em dash is structural
# and em_dashes() errors on it. These report and never break, for two reasons: a quote inside a
# string literal or a test vector can change what a program means when it is ASCII-ised, and a
# person decides each site.
#
# THE EN DASH IS NOT IN THIS TABLE AND THAT IS THE WHOLE DESIGN OF THE CHECK.
#
# Listing it beside the em dash and advising a hyphen would be wrong. This corpus's en dashes are
# all doing semantic work: numeric ranges, and author pairs. Walsh-Hadamard tells a reader Hadamard
# might be a hyphenated surname; Walsh–Hadamard tells them it is two people. ASCII-ising it destroys
# information.
#
# The corpus settles the wider question with it. docs/ carries 74 U+2212 minus signs and a working
# set of arrows, inequalities and set operators. It is a mathematics corpus using mathematical
# characters deliberately, and "unicode punctuation is suspect" would be a rule imported from
# somewhere else. The em dash is banned because it is a stylistic tell carrying no information. The
# en dash carries information. One is errored on and the other is ignored, and this follows that.
#
# AND THE SAME RULE REACHES THE CURLY QUOTE, which is the whole reason MARK_SUBJECT exists.
#
# U+2019 is a stylistic substitution for an apostrophe in English prose and it is a CHARACTER in
# several of the orthographies this tree transcribes: the glottalization mark in Nuxalk and in
# Lyon's Okanagan. U+2018 and U+2019 are also the delimiters linguistics puts around a gloss, as in
# yidád 'fish trap'. ASCII-ising either destroys information exactly the way ASCII-ising an en dash
# does, and a run whose subject is the mark is held to the same exemption the en dash gets outright.
#
# BOUNDED TO THE RUN AND TO THIS STAGE. A paragraph about an orthography does not get to carry a
# banned construction; only a quote finding goes quiet, and only inside the run that names what the
# character is doing. The cost is reported by the exclusion ledger, so a reader counts it instead of
# trusting this sentence.

import re

from .index import folded
from .quoting import NAMED_SPAN
from .scan import runs

# The character, what to call it, and what to write instead. The em dash is absent because
# em_dashes() already errors on it and a second report of one site is noise.
#
# U+2018 IS ABSENT AND THAT IS THE RULE AND NOT AN OMISSION. Nothing writes an opening single quote
# as an apostrophe. Its presence in a run therefore says the run uses PAIRED single quotes, which is
# the gloss convention linguistics writes as yidád 'fish trap' and is a convention and not a
# substitution. So a closing one is read as a substitution only where there is no opening one to
# have closed, which is exactly the apostrophe case: Lyon's, doesn't, the paper's.
SUSPECT = (
    ("’", "right single quote", "'"),
    ("“", "left double quote", '"'),
    ("”", "right double quote", '"'),
)

# The opening half of the pair. A run holding one is writing a gloss or a quotation, so the closing
# halves in it are that convention's and not a word processor's.
PAIRED = "‘"

# A closing single quote located where English puts an apostrophe: before the tail of a contraction
# or a possessive (doesn't, Lyon's, they're), or after the s of a plural possessive (the papers'). A
# run with no such site holds the mark only as a character, at the head of a form or beside a consonant
# (’qsápi, c’tQap@nwíxw, /k’/), and ASCII-ising those rewrites the transcription.
APOSTROPHE = re.compile(r"(?<=[A-Za-z])’(?=(?:s|t|re|ve|ll|d|m)\b)|(?<=[A-Za-z]s)’(?![A-Za-z])")

# A run whose subject is the mark rather than the sentence around it. Each arm names what the
# character is DOING, the same way BRITISH_SUBJECT names a word about writing instead of a country:
# a bare language name would exempt every paragraph that happens to mention one.
MARK_SUBJECT = re.compile(
    r"\b(?:glottal\w*|orthograph\w*|gloss(?:e[sd]|ing)?|enclitic\w*|apostrophe\w*"
    r"|grapheme\w*|diacritic\w*|transliterat\w*|codepoint\w*|typeset\w*)\b"
    r"|U\+20(?:18|19|1C|1D)",
    re.IGNORECASE,
)

MARK_REASON = (
    "the subject of the passage is the mark, which is a character in these orthographies and the "
    "delimiter of a gloss, and ASCII-ising either destroys information"
)


def smart_quotes(lines, path=None, ledger=None):
    """Every smart quote in the prose, as (line, complaint) findings.

    Read off the lines prose_only returns, so a quote inside a legal block, a verbatim span or a
    quieted region is out of scope here for the same reason it is out of scope for a banned token.
    One finding per character per run, carrying the count, because a run holding four of them is
    one edit and not four.
    """
    found = []
    for said, offsets in runs(lines):
        # A backticked span cites a form as the source writes it, quote marks and all, as it does
        # for a banned token. Blanked to its own length so the offsets still line up.
        text = NAMED_SPAN.sub(lambda span: " " * len(span.group(0)), said)
        held = [one for one in SUSPECT if one[0] in text]
        if PAIRED in text or not APOSTROPHE.search(text):
            held = [one for one in held if one[0] != "’"]
        if not held:
            continue
        if MARK_SUBJECT.search(folded(text)):
            if ledger is not None and path is not None:
                at = offsets[min(text.index(one[0]) for one in held)]
                ledger.note(
                    "mark as a character",
                    MARK_REASON,
                    "%s:%d" % (path.replace("\\", "/"), at),
                )
            continue
        for character, name, ascii_form in held:
            found.append(
                (
                    offsets[text.index(character)],
                    "%s (%d), write %s" % (name, text.count(character), ascii_form),
                )
            )
    return found
