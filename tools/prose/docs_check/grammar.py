#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# The grammatical context a word ban cannot see: a phrase with a plain replacement, a construction
# that hides the verb or the subject, and the sentence shapes this tree's prose repeats.
#

import os
import re

# The plain-language table: a phrase the NARA Writing Style Guide names, the plain form it gives,
# and its section. It sits beside voice.tsv, and the oracle reads it through plain_rows.
PLAIN_TABLE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "voice_plain.tsv")

# docs-check: quoting
# ====================================================================
# THREE GROUPS, AND WHAT STANDS BEHIND EACH
# ====================================================================
#
# PLAIN is read from voice_plain.tsv, one pattern a row. Each row is a phrase the NARA Writing Style
# Guide names, the plain form it gives, and its section. The pattern is the phrase as a run of words
# with any whitespace between them, so a phrase broken across two lines is still one phrase. The
# replacement comes from the guide and the oracle prints it: `--pick=plain` reads the same table. TIER A,
# cited to the section.
#
# CONSTRUCTIONS are rules of the guide that take a shape instead of a list:
#
#   2.3.3  a hidden verb: make, provide, perform or conduct carrying an article and
#          a noun in -ment, -ion, -ance or -ence. "made the decision" for decided.
#   2.3.5  a false subject: "It is clear that", "There are several issues that need".
#   1.5.2  a hyphen after an -ly adverb: "recently-received".
#   2.5.5  manner, fashion, way or basis carrying an adjective: "in a timely manner".
#
# These are TIER A, cited to the section. They fire in human papers too, and the guide bans them
# for every writer.
#
# ISMS are this tree's own: sentence shapes the machine's prose carries at many times the rate of
# the reference papers. Each is TIER B, reported with its rate in those papers. No standard names
# them. The measurement in grammar_evidence.py is their only warrant:
#
#   X, not Y.                  a contrast hung on the end of a clause
#   What it does is            the pseudo-cleft, a sentence that announces its own predicate
#   no X, no Y, and no Z       the negated triple
#   , which makes              a which-clause carrying the consequence of the last clause
#   Here's                     a sentence opened by pointing at what follows
#   is about X, not Y          the contrast in its topic form
#   that is deliberate         a clause that vouches for the sentence before it
#   instead of just            the contrast with a minimizer
#   : nothing.                 a colon and a one-word verdict
#   nothing more, nothing less the closing flourish
#
# GRAMMAR_RATE counts each pattern per 100k words of GRAMMAR_CORPUS. That text is every paper under
# build/papers, ungated: the glosses and the orthography stay in the denominator, and every rate
# here reads low by the share of the words that are not English. Re-measure after changing a
# pattern; a rate measured against another pattern is a rate for nothing.
# docs-check: end quoting


def plain_rows():
    """Every row of the plain table as (phrase, plain, section). An empty plain means leave it out."""
    held = []
    if not os.path.isfile(PLAIN_TABLE):
        return held
    with open(PLAIN_TABLE, encoding="utf-8", errors="replace") as handle:
        next(handle, None)
        for line in handle:
            part = line.rstrip("\r\n").split("\t")
            if len(part) == 3 and part[0].strip():
                held.append((part[0].strip().lower(), part[1].strip(), part[2].strip()))
    return held


# What may not are before a phrase of the plain table for the reading in the guide to hold.
# A determiner before prior makes it the noun ("no prior to estimate"), and and or but before a
# negated phrase makes not the second half of a contrast ("on entropy and not on time"), where 2.6.3
# is about a negated verb.
GUARDS = (
    ("prior ", r"(?<!\bno )(?<!\ba )(?<!\bthe )(?<!\bany )(?<!\bits )"),
    ("not ", r"(?<!\band )(?<!\bbut )"),
)


def plain_pattern(phrase):
    """The regex for one phrase of the plain table: its words with any whitespace between them."""
    guard = "".join(held for opens, held in GUARDS if phrase.startswith(opens))
    return guard + r"\b" + r"\s+".join(re.escape(word) for word in phrase.split()) + r"\b"


def plain_authority():
    """Each plain pattern, mapped to the section that bans it and the form the guide gives."""
    held = {}
    for phrase, plain, section in plain_rows():
        given = ("write %s" % plain) if plain else "leave it out"
        held.setdefault(plain_pattern(phrase), "NARA Writing Style Guide %s, %s" % (section, given))
    return held


PLAIN_AUTHORITY = plain_authority()
PLAIN = tuple(PLAIN_AUTHORITY)


# The verbs and suffixes the guide gives for 2.3.3, with the article between them that marks the noun
# as the thing done. Without the article it reads a verb and its object. The suffix is a clue from the guide
# and not its test: a function or a segment ends the same way and hides no verb. hides_verb
# below takes the test.
#
# docs-check: quoting
# The guide names do, have and give as well. Before an article do and have are an auxiliary or a
# possession far more often than a light verb ("has a direction", "does the information"), and in
# a derivation give means yield ("gives the difference"). The light-verb forms the guide gives
# by name are rows of the plain table ("did a study of", "has the tendency to", "gives the
# indication that").
# docs-check: end quoting
HIDDEN_VERB = (
    r"\b(?:make|makes|made|making|provide|provides|provided|providing"
    r"|perform|performs|performed|performing|conduct|conducts|conducted|conducting) (?:a|an|the)"
    r" (?:\w+ )?(?P<noun>\w+(?:ment|ion|ance|ence))\b"
)
FALSE_IT = (
    r"\bit (?:is|was) (?:clear|evident|apparent|obvious|important|necessary|possible|likely"
    r"|displayed|known|hoped|believed|expected|true) that\b"
)
# The word before the relative pronoun is the noun it modifies. A preposition there makes `that` a
# demonstrative, and the sentence has a real subject already.
FALSE_THERE = (
    r"(?:^|(?<=[.!?;:]\s))there (?:is|are|was|were) (?:\w+ )?"
    r"(?!(?:in|on|of|at|to|for|with|by|from|about) )\w+ (?:that|who|which) "
)
# An -ly word that is not an adverb takes the hyphen like any other compound. These are the -ly
# adjectives and nouns, and the hyphen after them is correct. is_adverb below takes the rest: an
# -ly word whose adjective the voice does not use is a noun or a verb ("anomaly-free",
# "multiply-add").
LY_HYPHEN = (
    r"\b(?!(?:early|only|family|holy|ugly|silly|daily|weekly|monthly|yearly|hourly|friendly|likely"
    r"|lonely|elderly|assembly|supply|reply|apply|rely|fly|ally|jelly|belly|bully|rally|tally"
    r"|italy)-)(?P<adverb>\w{3,}ly)-\w+"
)
# The word before manner, fashion, way or basis is an adjective only where its -ly adverb is a word
# of the voice. "on a given basis" has none to write instead. An ordinal counts the ways and has
# no adverb that means the same ("in a second way").
MANNER = (
    r"\b(?:in an? (?!(?:first|second|third|fourth|fifth|last|next|other|same)\b)(?P<how>\w+)"
    r" (?:manner|fashion|way)|on an? (?P<often>\w+) basis)\b"
)

CONSTRUCTION_AUTHORITY = {
    HIDDEN_VERB: "NARA Writing Style Guide 2.3.3, use the verb the noun hides",
    FALSE_IT: "NARA Writing Style Guide 2.3.5, make the real subject the subject",
    FALSE_THERE: "NARA Writing Style Guide 2.3.5, make the real subject the subject",
    LY_HYPHEN: "NARA Writing Style Guide 1.5.2, no hyphen after an -ly adverb",
    MANNER: "NARA Writing Style Guide 2.5.5, use the adverb",
}
CONSTRUCTIONS = tuple(CONSTRUCTION_AUTHORITY)


# The verb a noun's suffix can be taken back to: decision to decide, reflection to reflect,
# performance to perform, reference to refer, assessment to assess. Each suffix is tried with each
# ending, and the noun hides a verb when one of the stems is a word of voice.tsv. The guide lists
# -ity too. An -ity noun is made from an adjective (priority, majority) and hides no verb, and
# HIDDEN_VERB leaves it out.
UNDO = (
    ("sion", ("de", "d", "t")),
    ("ation", ("e", "")),
    ("ition", ("e",)),
    ("ion", ("", "e")),
    ("ment", ("", "e")),
    ("ance", ("", "e")),
    ("ence", ("", "e")),
)
_VOICE = []


def voice_words():
    """voice.tsv and project_words.tsv as the oracle reads them, read once."""
    if not _VOICE:
        from .oracle import counted
        _VOICE.append(counted())
    return _VOICE[0]


def is_adverb(word):
    """Whether an -ly word is an adjective's adverb: exactly from exact, happily from happy."""
    words = voice_words()
    word = word.lower()
    return any(len(one) > 3 and one in words for one in (word[:-2], word[:-3] + "y"))


def has_adverb(word):
    """Whether an adjective's -ly adverb is a word of the voice: rapid to rapidly, periodic to
    periodically, easy to easily.
    """
    words = voice_words()
    word = word.lower()
    forms = (word + "ly", word + "ally", word[:-1] + "ily", word[:-1] + "y")
    return any(one in words for one in forms)


def hides_verb(noun):
    """Whether a noun the 2.3.3 suffixes end can be taken back to a verb the voice uses."""
    words = voice_words()
    noun = noun.lower()
    for suffix, endings in UNDO:
        if not noun.endswith(suffix):
            continue
        stem = noun[: -len(suffix)]
        for ending in endings:
            verb = stem + ending
            if len(verb) > 3 and verb in words:
                return True
    return False


# A pattern here whose regex is only the first half of its test. scan.banned_hits retains a match
# where the second half holds.
# docs-check: quoting
# A causative make carries its object on to a complement ("makes the question answerable", "make
# the collision visible", "makes the instruction a branch"), and the noun there hides nothing. A
# light verb's noun ends the clause or opens the preposition or the clause that belongs to it
# ("made the decision to", "makes the argument and", "performs a measurement on").
# docs-check: end quoting
def ends_phrase(hit):
    """Whether the noun of a 2.3.3 match closes its phrase, as the noun of a light verb does."""
    after = hit.string[hit.end():].lstrip()
    if not after or not after[0].isalnum():
        return True
    return re.match(r"(?:%s)\b" % "|".join(TAILS), after, re.IGNORECASE) is not None


TAILS = ("of", "on", "about", "that", "to", "for", "with", "in", "at", "by", "from", "and", "or",
         "but", "which", "when", "where", "because", "as", "here", "there")


CONFIRM = {
    HIDDEN_VERB: lambda hit: ends_phrase(hit) and hides_verb(hit.group("noun")),
    LY_HYPHEN: lambda hit: is_adverb(hit.group("adverb")),
    MANNER: lambda hit: has_adverb(hit.group("how") or hit.group("often")),
}


# A sentence start: the start of a run, after a sentence's closing mark, or after a list bullet.
OPENS = r"(?:^|(?<=[.!?]\s)|(?<=^[-*+]\s))"

ISMS = (
    r"\b\w+, not (?:an?|the |just |only )?\w+(?: \w+){0,3}[.;:]",
    OPENS + r"what (?:it|this|that|the \w+) \w+s is\b",
    r"\bno \w+(?: \w+)?, no \w+(?: \w+)?,? and no \w+",
    r", which makes\b",
    OPENS + r"here['’]s\b",
    r"\b(?:is|are) about [^.]{1,40}, not\b",
    r"\b(?:that|this) is (?:deliberate|on purpose|intentional)\b",
    r"\binstead of just\b",
    r": (?:nothing|none|zero|yes|no)\.(?:\s|$)",
    r"\b(?<!and )nothing (?:more|less)\b",
)


# Per 100k words of GRAMMAR_CORPUS, one rate for each of ISMS in its order, as grammar_evidence.py
# prints them.
GRAMMAR_RATE = dict(zip(ISMS, (4.44, 0.17, 0.01, 0.61, 0.06, 0.01, 0.04, 0.06, 0.09, 0.28)))
GRAMMAR_CORPUS = "the 6,871,332-word build/papers text, ungated"

GRAMMAR = PLAIN + CONSTRUCTIONS + ISMS
GRAMMAR_AUTHORITY = dict(PLAIN_AUTHORITY)
GRAMMAR_AUTHORITY.update(CONSTRUCTION_AUTHORITY)
