#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# Where the ban table meets a file, and what a hit is reported as.
#

import re

from .context import CONTEXT_REASON, context_exempt
from .grammar import CONFIRM, GRAMMAR_CORPUS, GRAMMAR_RATE
from .human_rate import HUMAN_RATE, stage_of
from .index import PASSAGE, _COMPILED, candidates, present
from .quoting import EM_DASH, NAMED_IN_MARKDOWN, NAMED_SPAN, QUOTED
from .tier import AUTHORITY, COMMENT_ONLY, tier_of



# What opens a comment or continues a wrapped one. Stripped before lines are joined: a phrase
# broken across two comment lines reads as prose and not as prose with a marker in the middle.
MARKER = re.compile(r"^\s*(#+|//+|\*+/?|/\*+)\s?")


def runs(lines):
    """Consecutive non-blank prose lines joined into one string, with a map back to line numbers.

    Yields (text, offsets) where offsets[i] is the source line number of character i. A banned
    phrase that wraps across a line break is invisible to a per-line scan, and one escaped that way
    into a ledger heading: `is the` ended a line and `whole of the mechanism` opened the next.
    Joining the run finds it and the offset map still reports the line a reader has to open.
    """
    held = []
    where = []
    for at, line in enumerate(lines):
        text = MARKER.sub("", line).strip()
        if not text:
            if held:
                yield "".join(held), where
                held = []
                where = []
            continue
        if held:
            held.append(" ")
            where.append(at + 1)
        held.append(text)
        where.extend([at + 1] * len(text))
    if held:
        yield "".join(held), where


def banned_hits(lines, quotations=False, comments=False, path=None, ledger=None):
    """Every banned token in one file, as (line number, pattern, matched text).

    One site is yielded once. A run is scanned whole, and two patterns that overlap would otherwise
    report the same words twice: "which is exactly what" matches both which-is-what and
    is-exactly-what, and repairing the sentence closes both at once.

    banned_tokens turns these into the findings a reader sees, and submission_check counts them per
    pattern against the rate a human writer carries. Both read the same hits. A count and a
    finding cannot disagree about what fired.

    quotations exempts a long quoted passage and the markdown citation spans, and is set for .md.

    comments turns on the COMMENT_ONLY patterns, which are the ones a standard scopes to a comment
    in the sentence that bans them. main() sets it for the extensions whose prose lives in comments.
    It defaults off. A caller that has not been taught the scope gets the documentation reading,
    the one both standards share. submission_check.py is that caller.

    The run-level context exemption is applied here, beside QUOTED, because it answers the same
    question QUOTED does about a different subject: a run whose subject is a writing convention has
    to be able to write the word it is about. It is tier-aware and QUOTED is not. It cannot be a
    span list, and it is the single call site in this file that knows both the run and the tier.
    """
    seen = set()
    held = list(runs(lines))
    needing = present("\n".join(text for text, _ in held))
    for text, offsets in held:
        exempt = context_exempt(text)
        quoted = [span.span() for name in QUOTED for span in name.finditer(text)]
        # A backticked token is a name in a comment as much as in a page. This one is not
        # bounded to markdown the way the emphasis and quote spans are.
        quoted.extend(span.span() for span in NAMED_SPAN.finditer(text))
        if quotations:
            quoted.extend(span.span() for span in PASSAGE.finditer(text))
            quoted.extend(
                span.span()
                for name in NAMED_IN_MARKDOWN
                for span in name.finditer(text)
            )
        for pattern in candidates(text, needing):
            if (not comments) and (pattern in COMMENT_ONLY):
                continue
            tier = tier_of(pattern) if exempt else None
            for hit in _COMPILED[pattern].finditer(text):
                start, stop = hit.span()
                if any(
                    (start >= opens) and (stop <= closes) for opens, closes in quoted
                ):
                    continue
                if (pattern in CONFIRM) and not CONFIRM[pattern](hit):
                    continue
                at = offsets[start] if start < len(offsets) else offsets[-1]
                token = hit.group(0)
                if tier in exempt:
                    if ledger is not None:
                        ledger.note(
                            "the subject is the convention",
                            CONTEXT_REASON,
                            "%s:%d %r" % ((path or "?").replace("\\", "/"), at, token),
                        )
                    continue
                # One phrase reported once. A run is scanned as a whole. A duplicate here would
                # be the same site seen through two patterns that overlap.
                key = (at, start, token.lower())
                if key in seen:
                    continue
                seen.add(key)
                yield (at, pattern, token)


# The only human corpus this file has a rate against, named wherever a rate is printed.
#
# It is the 154 research papers under build/papers, cut down to English by english_gate, which is
# 759,815 words. They are Salishan linguistics: Canadian and British convention throughout, carrying
# orthography, interlinear glosses, IPA and tables of forms. english_gate's own header names
# ban_evidence.py, which generates HUMAN_RATE, as one of three measurements this corpus corrupted,
# and the header above HUMAN_RATE records that correction.
#
# WHAT THIS REPLACES, AND IT WAS WRONG TWICE OVER. The unmeasured branch printed "unused in 1.1M
# human words". 1,108,054 is the UNGATED token count, which the HUMAN_RATE header states is the
# wrong denominator and low by about 40 percent, while the measured branch beside it was already
# dividing by 759,815. Two branches, two denominators, one corpus, in eleven lines of each other.
# And "human words" describes a general English sample, which 154 linguistics papers are not: the
# sentence read as a claim about English and was a claim about these papers.
#
# THE UNMEASURED BRANCH IS THE OVERWHELMING MAJORITY OF WHAT THIS TOOL PRINTS, because max banned
# patterns have no measured rate. A misdescription there is not an edge case in the report, it is
# almost the whole of it, so the wording on that branch matters more than the wording anywhere else.
#
# Absence from these papers is weak evidence and the wording now says which papers. A reader can
# weigh it. A phrase can be missing because the domain is. Re-cut this against an English corpus, or
# retain naming the corpus. Never print a rate or an absence without the corpus behind it.
CORPUS = "the 759,815-word reference papers"


def measured(pattern):
    """A pattern's human rate per 100k words, with the corpus that rate was counted in.

    A grammar pattern carries its own rate and its own corpus. Printing it against CORPUS would name
    a text it was never counted in.
    """
    if pattern in GRAMMAR_RATE:
        return GRAMMAR_RATE[pattern], GRAMMAR_CORPUS
    return HUMAN_RATE.get(pattern, 0.0), CORPUS


def banned_tokens(
    lines, quotations=False, comments=False, path=None, ledger=None, regions=None
):
    """Findings a reader sees, one per hit, carrying the tier and what is behind it.

    A TIER A line names the section that bans the construction. A reader can go and read the
    sentence. A TIER B line carries a frequency where one was
    measured and says which corpus it was measured in where one was not. Neither fails a build.

    A finding inside a generated region retains its place in the count and names the generator. Read
    the note above generated_regions for why it is attributed and not suppressed: in the one tree
    measured, suppressing it would have deleted the only genuine structural finding there was.
    """
    found = []
    for at, pattern, token in banned_hits(lines, quotations, comments, path, ledger):
        rate, corpus = measured(pattern)
        shape = stage_of(pattern)
        tier = tier_of(pattern)
        said = " ".join(token.split())
        if tier == "A":
            note = "tier A %s %r, banned at %s" % (shape, said, AUTHORITY[pattern])
        elif tier == "alphabet":
            note = "definition %r, American convention is the house rule" % said
        elif rate:
            note = "tier B %s %r, %.1f per 100k in %s" % (shape, said, rate, corpus)
        else:
            note = "tier B %s %r, not seen in %s" % (shape, said, corpus)
        found.append((at, attributed(note, at, regions)))
    return sorted(found)


def attributed(note, at, regions):
    """The same finding with its generator named, where it sits inside a generated region."""
    if regions and (at in regions):
        return "%s [generated by %s: fix the generator, then rerun it]" % (
            note,
            regions[at],
        )
    return note


def em_dashes(lines):
    return [(at + 1, "em dash") for at, line in enumerate(lines) if EM_DASH in line]
