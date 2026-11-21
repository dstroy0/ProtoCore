#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# The shapes a search dated after 2020 and nobody wrote before.
#

SHAPES = (
    # Tier four, and the test for it was a search. Each shape below was looked up and returned
    # no result dated before 2020. A phrase people did not write until models wrote it is a
    # phrase to cut. The distance instrument reads the same tree the same way: 79.6 percent of
    # the margin on docs/research/index.md sat on sentence shape.
    #
    # A clause that announces a conclusion and carries no fact.
    r"\bthat is (what|why|the (difference|point|whole|answer|test|reason|rule|shape|cost))\b",
    # Searched with the measuring word attached as well. "which is how far" returns nothing
    # dated before 2020 either. The exemption it looked like it deserved was not there.
    r"\bwhich is (what|why|how|the (difference|point|whole|answer|reason|rule))\b",
    # Bounded to the same noun list its sibling three lines up already carries. The bare `the`
    # arm was drift between two patterns written for one shape, and it fired on
    # code-documentation:133, "and that is the defect: each occupies the place a fact
    # goes", where the clause carries the fact instead of located in for one.
    r"\band that is (what|why|the (difference|point|whole|answer|test|reason|rule|shape|cost))\b",
    # Defining a thing by what it is not.
    r"\b(checking|reading|running|measuring|saying) [a-z]+ is not [a-z]+ing\b",
    r"\bis not an? (accusation|argument|claim|answer|excuse|guess|estimate)\b",
    # proof and evidence are the field's own words here. "a row that checked nothing is not
    # evidence that anything held" is a precise claim about a bench and it stays.
    r"\bis not (a pass|passing|failing)\b",
    # WITHDRAWN: \bworse than (not|nothing|none|no |having)\b and \bis worse than a\b. Both
    # fired on the standards. code-documentation:84 writes "A wrong line number is a
    # false claim and worse than none", :165 writes "A stale section is worse than a missing
    # one", and code-comments:122 writes the first of those again about a MISRA rule
    # number. A comparison of two failure modes by cost is the ordinary way to state which one
    # to prefer.
    #
    # WITHDRAWN: \bstated (once|twice)\b, \btwo facts that\b and \bone fact per\b. These banned
    # the standards' own rule text. Both files write "One fact is stated once; a second copy is
    # a future contradiction" at :21, code-documentation writes "One fact per sentence" at :146
    # and code-comments writes it at :199. A checker that reports the sentence stating a rule as
    # a violation of that rule is the over-reach class this pass exists to remove. Machinery
    # given intent. A run does not say anything and a file does not answer. A run of characters
    # is the field's own term and predates all of this. Only the execution sense is banned. The
    # verb has to be one a program does.
    r"\ba run that (finishes|reads|reports|says|passes|fails|completes|knows|decides)\b",
    r"\b(the (file|tool|check|hook|run|number|count|table)) (says|answers|knows|decides)\b",
    # Hedges, and a pointer left behind after the thing it pointed at was cut.
    r"\bthe ordinary (case|answer)\b",
    r"\bnothing else here\b",
    # "nothing here is a timing claim" states a scope and stays. The banned form is the flourish
    # that closes a paragraph on nothing.
    r"\bnothing here is (new|magic|special|clever|hidden|secret|surprising)\b",
    # \band nothing else\b belongs near the head of this tuple, beside the and-nothing-more and
    # and-no-more forms, and must not be written here a second time. One rule in two places is two
    # rules that can be edited apart, and banned_hits deduping on (line, offset, token) only hides
    # the duplication rather than preventing it.
    #
    # This is the preachy register. Writing about a corpus that belongs to somebody else pulls
    # prose toward the sermon, and the sermon is worse than useless here: every line in this
    # repository carries one person's name, and a paragraph telling the reader how to feel about
    # the material reads as that person performing and not stating. The ethics are in the
    # permission column and in what the gates error. They do not need narrating on top.
    #
    # The rule this tier enforces is that a fact is stated once, flat, and left alone.
    #
    # Moral entitlement. Whatever is owed here is settled in the licence and in SPEECH.tsv.
    r"\b(is|are|was|were) (the least|what) (they|we|he|she|you|somebody) (are |is |)?(owed|deserve)",
    r"\b(the least|more) (they|we|you) (deserve|are owed)\b",
    r"\bwe owe (them|him|her|you|it)\b",
    r"\bentitled to (make|take|say|claim)\b",
    # Ranking two things by worth. Three shapes belong outside this group and must not be added:
    # "a miss is worth more than a hit here" is a claim about what a diagnostic tells you, "the
    # whole point of the file" names what a function is for, and "as distinctive as it should be"
    # is a measurement against a prediction. None of those is the register this group is after, and
    # banning them costs true sentences at a worse rate than it catches tics. What is left is the
    # shape that only ever shows up in a sermon.
    r"\bthe more valuable\b",
    r"\bthe (smallest|least) part of what\b",
    r"\bworth more than (the|their|his|her|any) \w+ (itself|themselves)\b",
    # Aphorism built on a moral antithesis. "something done, never something suffered" is the
    # shape.
    r"\b(something|anything) [a-z]+ed, never (something|anything)\b",
    r"\bnever something (suffered|taken|lost|given)\b",
    # Declaring what a thing means to the reader.
    r"\bis what makes it (beautiful|worth|matter|special|right)\b",
    r"\bthat is the (beauty|tragedy|point) of\b",
    r"\ba person is not a\b",
    # Piety markers and the invitation to reflect.
    r"\b(it bears remembering|let us remember|we must remember|never forget)\b",
    r"\bwith the respect (it|they|that) deserve",
    r"\b(honou?r|honou?ring) (the|their|his|her) (memory|words|wishes|legacy)\b",
    # Announcing one's own virtue in doing the ordinary thing. The comma separates the
    # flourish from the measurement: "as distinctive as it should be" is a comparison and
    # ", as it should be" is a pat on the back.
    r"\bthe right thing to do\b",
    r",\s*as it should be\b",
    # The second half of the preachy tier, added after the first half missed a whole page of it.
    # The moral vocabulary was gone and the register was not: what replaced it was a comment
    # arguing for its own design against an imagined sceptic, with a closing line for emphasis.
    # A header states what a thing does and what it covers. Whoever reads it can decide for
    # themselves whether that was a good idea.
    #
    # Closers. A sentence added after the fact was already stated, to land it.
    r"\bis the type nobody\b",
    r"\bnobody (looks at twice|reads twice|rechecks|checks twice)\b",
    r"\band they could not have\b",
    r"\bwhich is the whole\b",
    # Emphasis by repeating the verb against its own negation. "holds something" and "holds
    # nothing" must not be banned here: sites across this tree use them for exactly what they say,
    # as in "a shuffle holds nothing beyond one symbol" and "a domain that holds nothing". What
    # makes the preachy version preachy is the verb repeating against itself across a clause, and
    # no pattern separates that from the plain use. Run any candidate over the tree and read what
    # it reaches before retention it.
    r"\bwould (prove|buy) nothing\b",
    # A bare abstraction located in for the subject, usually in a closing clause.
    r"\bthe (reproducible|checkable|measurable|honest|valuable) thing\b",
    # Arguing the design instead of describing it.
    r"\bagainst how (somebody|someone|anybody|people)\b",
    r"\bin nearly every (respect|way|case)\b",
    r"\bdoes not put a (reader|person|user) on the path\b",
    r"\ba (rule|check|gate|test) added here\b",
)
