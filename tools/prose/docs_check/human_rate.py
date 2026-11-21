#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# What a construction costs a human writer, where that has been measured.
#

from .locale import LOCALE


# docs-check: quoting
# ====================================================================
# THE THREE STAGES, AND WHAT MEASURED THEM
# ====================================================================
# The list above is one flat table and it was built by noticing. utils/maint/prose/ban_evidence.py
# scored every pattern in it against 1,108,054 words of human research papers under build/papers,
# and against the 403,111 words of prose in this tree. That run split the table into three stages
# that filter at different widths, and the stages behave nothing alike.
#
# ALPHABET. Orthography, and it recovers the locale before it says anything about a writer. The
# papers are Canadian and British convention linguistics. Neighbour fires at 17.3 per hundred
# thousand words in them and analyse at 8.9, behaviour at 4.8, labelled at 2.9, centre at 2.3.
# None of that is machine prose. It is where the author is, and the American definitions this tree
# uses are a house rule and not a defect in anybody's English.
#
# WORD. Vocabulary, and the measurement mostly refutes it. Humans write crucial at 5.5, vital at
# 0.5, journey at 2.2, delve at 0.4, synergy at 2.4, utilize at 2.3 and leverage at 0.5. A ban on
# these removes ordinary academic English. They stay in the table because a house style is allowed
# to be narrower than the field, and they are reported as the weaker class they measured as.
#
# PHRASE. The structural shapes, and every one of them is confirmed. These nine appear 23.3 times
# per hundred thousand words in this tree and exactly zero times in 1.1 million words of human
# writing: is what separates and its family, is what makes, what survives, is the whole of, the
# X-not-Y grammar, that is the whole, and nothing more, what matters is, the one that matters.
# A clause that explains the sentence just written is the signature, and the phrase stage
# catches it.
#
# One caution the numbers earn. Absence from the papers is not proof a phrase is machine written:
# the papers are linguistics and a phrase can be missing because the domain is.
# Re-run ban_evidence.py after changing this table.
#
# MEASURED AGAINST THE MACHINE'S OWN PROSE, WHICH SETTLED IT
#
# utils/maint/prose/session_prose.py takes the machine's messages out of a session transcript. That
# corpus needs no label to be trusted, and it is the only one here whose author is not in question.
# 38,702 gated English words of it, against 759,815 of the papers, per hundred thousand words:
#
#   the whole list            643.4 machine     387.9 human   112.8 this tree
#   the eight phrase shapes    56.8 machine       0.0 human    26.4 this tree
#
#   rather                    240.3 machine      57.6 human      4.2 times the human rate
#   which is why/what/the     131.8 machine       3.2 human     41 times
#   so a                       46.5 machine       1.3 human     36 times
#   is the one                 43.9 machine       2.5 human     18 times
#   is exactly what/why/the    25.8 machine       0.4 human     65 times
#   and nothing else           12.9 machine       0.1 human    129 times
#   is what makes              10.3 machine       0.0 human     absent from 759,815 human words
#   the one that matters        7.8 machine       0.0 human     absent, and banned here by name
#
# Two things follow. The list was built by noticing and the noticing is accurate: the phrases
# called out by hand are the ones carrying the largest ratios. And every rate above is a floor,
# since the prose they are taken over was written with the same phrases under suppression.
#
# THE TIER IS NOT REFUTED BY HUMANS USING THESE WORDS. Humans do use them. The whole claim is a
# ratio, and the list is reached at well above the human rate. A reading that scores the words
# instead of the ratio says nothing, and a rate taken against an older corpus says less.
# docs-check: end quoting

# The locale stage is defined above BANNED, because BANNED splices it in and a tuple has to exist
# before it can be spliced. It is one copy in one place and must stay that way.

# docs-check: quoting
# What each pattern costs a human writer, per hundred thousand words of the research papers.
# Measured, not estimated. A pattern absent from this table fired zero times in all 154 of them.
#
# Counted over 759,815 words, the papers after english_gate takes them down to English. The divisor
# has to be that gated count and not the raw 1,108,054 tokens, which carry the Salishan orthography,
# the interlinear glosses and the IPA that fill these pages. Dividing by the raw count puts every
# rate here about 40 percent low: rather reads 40.4 against 57.6, in order to 26.6 against 44.4.
# docs-check: end quoting
# Regenerate with utils/maint/prose/ban_evidence.py after any change to the table above.
HUMAN_RATE = {
    r"\brather\b": 57.6,
    r"\bin (order to|terms of)\b": 44.4,
    r"\bneighbour": 23.8,
    r"\bon the other hand\b": 16.7,
    r"\b(furthermore|moreover|additionally)\b": 15.7,
    r"\b(indispensable|paramount|imperative)\b": 14.6,
    r"\b(consequently|nevertheless|nonetheless|conversely)\b": 14.3,
    r"\b(notably|interestingly|importantly|crucially|remarkably)\b": 14.0,
    r"\banalyse(s|d)?\b": 12.2,
    r"\b(remarkable|noteworthy|impressive|exceptional|extraordinary|outstanding)\b": 10.9,
    r"\b(arguably|essentially|basically|fundamentally)\b": 9.3,
    r"\bnot (just|only) .{1,40}\bbut (also )?\b": 8.7,
    r"\ba (wide range|variety) of\b": 8.3,
    r"\b(innovative|disruptive|trailblazing|visionary)\b": 8.3,
    r"\bcrucial\b": 7.8,
    r"\bbehaviour": 6.8,
    r"\blet us\b": 4.9,
    r"\b(stunning|striking|breathtaking|awe.inspiring|mesmeri[sz]ing|dazzling)\b": 4.7,
    r"\bwith regards? to\b": 4.6,
    r"\b(underscores|highlights|showcases|illustrates|exemplifies) the\b": 4.3,
    r"\blabelled\b": 4.2,
    r"\b(rigorous|painstaking|diligent|thorough)\b": 3.9,
    r"\bsynergy\b": 3.6,
    r"\butili[sz](e|es|ed|ing|ation)\b": 3.3,
    r"\b(serves|is) as an?\b": 3.3,
    r"\b(premier|foremost|quintessential|iconic|legendary|timeless)\b": 3.3,
    r"\bwhich is (why|what|the)\b": 3.2,
    r"\bjourney\b": 3.2,
    r"\b(enhance|augment)\b": 2.8,
    r"\bin light of\b": 2.6,
    r"\bfacilitat(e|es|ed|ing)\b": 2.6,
    r"\bcentre\b": 2.6,
    r"\b(uncover|unveil|illuminate|unearth|unravel|demystify)": 2.6,
    r"\bis the one\b": 2.5,
    r"\bit should be noted\b": 2.2,
    r"\bit (is|'s) (important|worth|useful|helpful) (to note|noting|to remember|to mention|mentioning)\b": 2.2,
    r"\b(intuitive|elegant|sleek|polished|frictionless)\b": 2.0,
    r"\bone of the max\b": 1.7,
    r"\bimagine (a|an|the|that|if)\b": 1.7,
    r"\b(sophisticated|layered|expansive|exhaustive)\b": 1.7,
    r"\bwe (will|'ll) (explore|look|dive|examine|see)\b": 1.6,
    r"\bplays an? [\w-]*\s?role\b": 1.6,
    r"\bpertaining to\b": 1.4,
    r"\bcompelling\b": 1.4,
    r"\bcolour": 1.4,
    # Deliberately NOT widened to `so an?`, whatever the Tier A ban above does. The 1.3 is a human
    # rate taken for `so a` over the reference papers. Widening the pattern carries a rate to a
    # population it was never taken over, which is the fault the rate exists to avoid. `so an` is
    # caught by the Tier A ban and needs no frequency entry; measure one before wanting it.
    r"\bso a\b": 1.3,
    r"\bwhen it comes to\b": 1.1,
    r"\bamongst\b": 1.1,
    r"\balbeit\b": 1.1,
    r"\b(stellar|superb|phenomenal|tremendous|immense|enormous|staggering)\b": 1.1,
    r"\b(open the door|set the stage|position the foundation)\b": 1.1,
    r"\bthe one (thing|place|case|word|reason|table|file|grain|repair|mistake|addition|column)\b": 0.9,
    r"\bin this (article|post|guide|section, we)\b": 0.9,
    r"\bfoster(s|ing)?\b": 0.9,
    r"\bvital\b": 0.8,
    r"\brealm\b": 0.8,
    r"\binvaluable\b": 0.8,
    r"\bdue to the fact that\b": 0.8,
    r"\ba (wide array|host|multitude|spectrum|plethora) of\b": 0.8,
    r"\b(countless|endless|limitless|boundless|unmatched|unrivall?ed)\b": 0.8,
    r"\bto some extent\b": 0.7,
    r"\bthe (intersection|convergence) of\b": 0.7,
    r"\bleverag(e|es|ed|ing)\b": 0.7,
    r"\bin many ways\b": 0.7,
    r"\bfirst and foremost\b": 0.7,
    r"\b(far.reaching|wide.ranging|all.encompassing|overarching)\b": 0.7,
    r"\bthe landscape of\b": 0.5,
    r"\bsome of the max\b": 0.5,
    r"\bintricate\b": 0.5,
    r"\bdelve": 0.5,
    r"\b(traverse|venture|spotlight|champion|nurture)\b": 0.5,
    r"\b(it is here that|this is where)\b": 0.5,
    r"\btreasure trove\b": 0.4,
    r"\bthe whole of (the|what|it)\b": 0.4,
    r"\bstands out\b": 0.4,
    r"\bprofound\b": 0.4,
    r"\bplethora\b": 0.4,
    r"\bnuanced\b": 0.4,
    r"\bis precisely (what|why|the)\b": 0.4,
    r"\bis exactly (what|why|the)\b": 0.4,
    r"\bendeavor\b": 0.4,
    r"\baforementioned\b": 0.4,
    r"\badd up\b": 0.4,
    r"\b(name|definition|token|type|structure|constraint)s?\s+(says|say|signals|signal|encodes|encode|conveys|convey|announces|announce|advertises|advertise)\b": 0.4,
    r"\buser.friendly\b": 0.3,
    r"\bunlock(s|ing)? the\b": 0.3,
    r"\bunderpin": 0.3,
    r"\bto sum up\b": 0.3,
    r"\bthe bottom line\b": 0.3,
    r"\bstate.of.the.art\b": 0.3,
    r"\borganis(e|es|ed|ing|ation|ations)\b": 0.3,
    r"\bneedless to say\b": 0.3,
    r"\blet['’]s\b": 0.3,
    r"\bas you can see\b": 0.3,
    r"\band no more\b": 0.3,
    r"\ban array of\b": 0.3,
    r"\ball in all\b": 0.3,
    r"\babundance of\b": 0.3,
    r"\b(simply|put) put\b": 0.3,
    r"\b(scalable|versatile)\b": 0.3,
    r"\b(having said that|with that said)\b": 0.3,
    r"\bwealth of\b": 0.1,
    r"\bthink of it as\b": 0.1,
    r"\bthe crux of\b": 0.1,
    r"\bthat is (what|why) \w+ (is|was|does|did)\b": 0.1,
    r"\bsheds light on\b": 0.1,
    r"\bresonat(e|es|ed|ing)\b": 0.1,
    r"\bon purpose\b": 0.1,
    r"\bnavigat(e|es|ing) the\b": 0.1,
    r"\bmodelled\b": 0.1,
    r"\blook no further\b": 0.1,
    r"\blies at the\b": 0.1,
    r"\bin a nutshell\b": 0.1,
    r"\bembark\b": 0.1,
    r"\becosystem\b": 0.1,
    r"\bcaptivat": 0.1,
    r"\bbolster": 0.1,
    r"\bbest practices\b": 0.1,
    r"\bat (its core|the heart of)\b": 0.1,
    r"\band nothing else\b": 0.1,
    r"\b(what|that) matters (is|here|max)\b": 0.1,
}


def stage_of(pattern):
    """Which of the three filters a pattern belongs to: alphabet, word or phrase.

    A pattern is alphabet when it matches one defined form. It is phrase when it matches across a
    space, and that makes it a shape instead of a vocabulary item. Everything else is word.

    This answers what a pattern LOOKS like. tier_of answers what authority it carries, and the two
    disagree on purpose: see the note above AUTHORITY.
    """
    if pattern in LOCALE:
        return "alphabet"
    if " " in pattern:
        return "phrase"
    return "word"
