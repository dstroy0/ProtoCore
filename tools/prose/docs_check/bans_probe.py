#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# The tier an outside detector marked, sentence by sentence, in this tree's own prose.
#

PROBE = (
    # Tier six, and the first tier this file did not find by itself. A 340 word passage of
    # theory/theory/crystallography/chapters/chapter_whose_result.tex was scored by an outside
    # detector, which returned 80.4 percent machine written and marked which sentences carried
    # it. Every shape below is out of a marked sentence and was not already in the table above.
    #
    # The outside number is worth little on its own. A detector reading perplexity marks plain
    # declarative technical prose as machine written because that prose is low perplexity, which
    # is a property of the register and not of the author. What makes this tier worth adding is
    # that two unrelated instruments picked the same file: the eight patterns below were counted
    # across 53 documents before any was retained, and chapter_whose_result.tex is 26 lines long and
    # trips five of them, at lines 10, 16, 18, 20 and 24. No other file in the tree is that
    # dense.
    #
    # Counted first, per the rule the tiers above set: 11, 17, 3, and 1 apiece for the rest.
    #
    # The X-not-Y antithesis wearing definite articles. Line 30 and line 78 ask for a or an.
    # Therefore "the good outcome and not the bad one" walks past both of them.
    r"\b(is|was|are|were) the [\w-]+ and not the [\w-]+",
    # The pseudo-cleft, which holds a plain statement back and then delivers it as a reveal.
    r"\bwhat is \w+ is\b",
    # A hypothetical comparison the work makes about itself.
    r"\bit would be a (worse|better) [\w-]+ to\b",
    # Justifying a label by pointing at behavior, in a sentence that already gave the label.
    r"\bbecause of what \w+ (does|did|is|was)\b",
    # Naming a thing and then restating that it is one, to give the sentence a second beat.
    r"\band it is one\b",
    # Parallel negation hung off a passive claim.
    r"\band none is (wanted|claimed|needed|asked|offered|sought)\b",
    # The nothing-else tag one preposition away from the form banned above.
    r"\bon nothing else\b",
    # Half of a can-only against cannot pair. Each half is ordinary and the pairing is the tic,
    # which no single pattern reaches. This catches the half that carries it.
    r"\bcan only show (that|whether)\b",

    # ---- Found by a detector probe rather than by a frequency table. ----
    #
    # A frequency comparison against a human corpus finds TOKENS. It cannot find a sentence whose
    # every word is ordinary and whose SHAPE is the tell, and those pass the gate and still read
    # wrong. This group is for that half.
    #
    # THE PROCEDURE FOR ADDING ONE, and it is the whole warrant for anything in this group. Write
    # prose in the register being hunted and score it per sentence, twice, with different wording
    # carrying the same shapes. Retain a sentence only if it scores high AND trips nothing already in
    # this tuple, and retain a shape only if it recurs across both batches, since one high score is
    # detector noise. Send no document from either tree anywhere: the register is the thing being
    # characterised, and prose about nothing characterises it as well as prose about the work, with
    # nothing at stake in it.
    #
    # Then run the candidate against the whole tree and read every line it matches. "not merely"
    # fails this test: every line it reaches is the deliberate X-and-not-merely-Y idiom this work
    # uses on purpose, so banning it removes a construction rather than a tic. That is the failure
    # to check for before anything is added here.
    #
    # One calibration note for anyone extending this. The detector scored "It was a warm
    # afternoon and the window was open" at 0.99, which looks like a false positive and is not
    # one: that sentence is scene-setting, which belongs to fiction, and in technical prose a
    # register mismatch is exactly what it reads. Narrative openers are a real shape here and no
    # pattern below reaches them, because a regex cannot see register. They still have to be
    # caught by eye.

    # The hedge that closes a section while conceding nothing.
    r"\b(further|additional|more) (research|work|study|studies|investigation|analysis) (is|are) needed\b",
    # Announcing emphasis instead of emphasising.
    r"\bit is worth (emphasi[sz]ing|stressing|highlighting)\b",
    # The universal caveat, which says only that cases differ.
    r"\bone-size-fits-all\b",
    # A summary adverb opening a sentence that goes on to summarise nothing.
    r"(?m)^\s*Ultimately,",
    # Architectural metaphor doing the work a plain claim should do.
    r"\bthe foundation (up)?on which\b",
    # Frontier talk. Always about the field, never about the measurement.
    r"\bpush(?:ing|es|ed)? the boundaries\b",
    # Certainty about the future, in a sentence whose claim carries none.
    r"\bwill inevitably\b",
    # The paradox opener, promising depth before any has been displayed.
    r"\bdeceptively simple\b",
    r"\bstay(?:ing)? ahead of the curve\b",
    r"\bhas never been more important\b",

)
