#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# The tiers a detector found after the ones a person found, read back out of marked passages.
#

DETECTED = (
    # The constructions an outside detector marks in a passage that scores as machine written.
    # Striking these six out of a passage moves its score by an order of magnitude with every fact
    # and every number unchanged, which is what says the shape carries the score and not the
    # content.
    #
    # What they share is that each one puts the document in the subject position and gives it a
    # verb of authority or of reading. A page does not settle a character and a reading is not
    # an authority. A person settled it, off that page.
    r"\bis (?:the|an?) authority for\b",
    r"\bit then reads as\b",
    r"\bare settled on page\b",
    # The temporal hedge on a state nobody dated. Either the table has the note or it does not.
    r"\b(?:table|document|page|file|row) first had no\b",
    # A colon splicing two independent clauses, the elaboration shape section 6 bans in
    # prose and which the detector scores the same way.
    r"\bhas no call and no text:",

    # Tier eight. An outside detector is trained on published machine prose, and that is not the
    # question this gate asks. A passage can score clean there and still not sound like the person
    # whose name is on the research paper. So a score from one bounds nothing here, and every
    # pattern below is on a reading of the passage itself.
    #
    # Each of these names a table field and then gives it something to
    # do. The tree already calls that field the who column, and somebody fills it in by hand.
    r"\bthe slot stays empty\b",
    r"\bwere read off page\b",
    r"\bread by hand with no reader\b",
    r"\bthe record slot is empty\b",
    # Four words where one does. An entry that carried this reads "No speaker named" instead.
    r"\bis named as having\b",
    r"\bnobody wrote an? \w+ for this one\b",
    # Padding on a possessive. The paper's text is the paper.
    r"\b(?:paper|page|file|table|document)'s own text\b",
    # The instrument named by category and then given a passive. It has a filename: oracle.tsv, and
    # the entry names it, as in "Oracle.tsv checked against the paper."
    r"\bthe oracle is checked against\b",

    # A corpus README entry read out loud against these retains its facts and loses every sentence
    # written about them, which is the test for whether a sentence was carrying anything.
    #
    # A reading, a page or a mark given authority over a question.
    r"\bneither (?:is|settles|decides|says) what\b",
    r"\bis (?:not )?the authority for what\b",
    # A count announced as a superlative instead of given.
    r"\bonly mark dropped\b",
    r"\bthe only place a \w+ was dropped\b",
    # A footnote reported as though its absence were an event.
    r"\bhas no call and no text\b",
    # The mark has a name. The research paper uses glottalization mark three times.
    r"\bglottal tick\b",
    r"\bwith a tick added\b",
    # Two sentences restating the sentence before them. Where the magnification already
    # appeared, saying it again is the paragraph explaining itself.
    r"\bat that magnification\b",
    # Declaring a field empty, in a document whose rule is that an empty field says so by being
    # empty.
    r"\bwho column empty\b",
    r"\bhis name does not go in it\b",
    # A participle located in for the condition. Name the condition: doing it without saying
    # so.
    r"\bmade quietly\b",
    r"\bif it were made quietly\b",
    # A pipeline given a dependency. Name the caller: no text tool calls it.
    r"\bnothing in the \w+ pipeline depends\b",
    r"\bnothing downstream depends\b",
    # A digest placed somewhere by itself. Name the file that carries it.
    r"\bits SHA-256 sits in\b",
    r"\b(?:hash|digest|checksum) sits in\b",
    # A tool or a representation given eyes. A script reads a file, a representation is the
    # output.
    r"\b(?:sound|text|word) representation reads\b",
    r"\bthe representation reads\b",
    # Deixis repeating the heading it sits under.
    r"(?m)^This one does not read\b",
    r"\bno (?:text|other) tool calls it\b",
    # Exclusivity asserted about a set the reader cannot see. Either it is the only one, cited,
    # or the clause comes out.
    r"\band no other tool does\b",
    r"\bno other \w+ does\b",
    # A file given a residence. It is retained somewhere and run from somewhere, by somebody.
    r"\blives in the closed\b",
    r"\bit lives in\b",
    r"\band is run from there\b",
    # A script given eyes, under a heading that already named it.
    r"\breads the recordings\b",
    # The summarizing sentence at the end of a paragraph, restating what the paragraph said. The
    # paragraph is the statement.
    r"(?m)^A \w+ therefore\b",
    # NO BARE WORD BAN FOR carry, hold, book, construction, cost, buy, pay, spend, earn,
    # afford, win, price OR read. Every one of them is ordinary technical English. The shape worth
    # catching is the is-what-VERB construction above, and the narrow phrases each group already
    # carries. WITHDRAWN in bans.py names each one and the sentence that excludes it.
    #
    # There is a real observation underneath that ban and it does not warrant one: "A digest is
    # listed in a manifest. A value is in a column. A form appears on a page." That is a house
    # preference for a precise verb, and precise verbs are worth wanting. It is not a register
    # tell, and a bare word ban is not how it gets enforced.

    # The shapes a provenance section reaches for when it is read out loud instead of scanned.
    #
    # shape, where nothing has a shape. The topology research papers use it for a real one and are the
    # only place it is.
    r"\bfor that shape and reports\b",
    # WITHDRAWN: bare \bthat shape\b. It fires on code-documentation:51, "putting that
    # shape at the head of a .md is a defect introduced rather than a rule satisfied", where the
    # shape is an SPDX header block and is a real one. The bounded form above stays.
    # A claim described by its three possible forms instead of stated.
    r"\ba claim that something here is new\b",
    r"\bnew, first,? or absent\b",
    # A repository given the power to settle things, then denied it.
    r"\bcannot settle that type of claim\b",
    r"\bfrom inside itself\b",
    # Position located in for the relation. A reference is attached to a claim or it is
    # missing.
    r"\bno reference beside it\b",
    # WITHDRAWN: bare \bbeside it\b. It fires on code-documentation:84, "cite the line
    # that shows the thing being said, not the line beside it", where beside it is literal
    # adjacency in a file and is the whole of what that rule is about. The bounded form above
    # stays. The reading, as a thing that happens without a reader.
    r"\bthe reading has not been done\b",
    # A script given a character: it reports, it never decides, it is honest about itself.
    r"\bit reports and never decides\b",
    r"\breports and never\b",
    # An emphatic tail on a count that was already exact.
    r"\bare mixed at all\b",
    r"\b(?:is|are|was|were) \w+ at all\b",
    # slot, in every form. Nothing here has slots. A field has a name already: the who column,
    # the year field, the identifier. A chaining state has positions, and FIPS 180-4 calls its
    # eight a through h the working variables.
    #
    # The weight of this one sits in the chain chapters, where a field has a name already.
    #
    # The bare ban on slot is in bans_words.py. The phrase below gives the slot a verb.
    r"\bthe slot (?:holds|carries|gives|takes|decides)\b",
    # A copy given a job. The value is the same at both ends and nothing moved it.
    r"\ba copy that transports\b",
    r"\btransports a \w+ without\b",
    # A fact filed somewhere by itself, and a section named for an accounting nobody retained.
    r"\band is recorded\b",
    r"\bunder what it cost\b",
    # A verdict announced, then restated as its own announcement. The word is Unconfirmed, or
    # Inconclusive.
    r"\bis unconfirmed and is recorded\b",
    r"\bunconfirmed and written down\b",
    r"\bthe candidate mechanism\b",
    r"\brecorded as a candidate\b",
    # These are theories, and the weight of the word sits in the ledger chapters. It is a method
    # and not a construction.
    #
    # NO BARE BAN on book or construction. Both are naming rules about this tree's own vocabulary
    # and neither is a register claim. Section 143 has no
    # construction to bound them to. code-comments:199 writes "Antithesis, parallelism,
    # and colon-then-elaboration are essay construction, not comment construction". A bare ban
    # reports that sentence twice over. See WITHDRAWN at the foot of this tuple. A caveat given
    # descendants, and the pair of clauses that always follows it.
    r"\binherits that\b",
    r"\brecorded here inherits\b",
    r"\bevery one of them \w+ and none of them\b",
    # The verb has to be a verb. \w+s also matches is, was and has. Unbounded it fires on
    # code-documentation:133, "None of them is wrong English and none carries a
    # measurement", where the clause is a plain statement about a word list.
    r"\bnone of them (?!is\b|was\b|has\b|does\b)\w+s\b",
    # cost, in every form. A retrieval that took five attempts took five attempts. Say the
    # number.
    #
    # No exception. Give the number and its units: corpus symbol accesses, character
    # comparisons, cycles, bytes.
    r"\ba measured cost\b",
    # The rest of the transaction. A fold does not buy rounds, an arm does not win, a
    # measurement does not earn or spend or pay.
    #
    # NO BARE BAN on cost, buy, bought, pay, paid, spend, spent, earn, afford, win, won, read or
    # price. The observation behind them is and the bare word ban does not. See WITHDRAWN in
    # bans.py.
    #
    # ONE AUTHOR. There are no other contributors to this tree or to theory/, only the author and
    # the prior art. Prose that credits an observation to a session, a peer session, the theorist,
    # a specialist, the project architect or orior as a person presents one author's work as
    # a team's. code-documentation:141 bans that register in a file a person wrote about their own
    # work, and naming the run or the tool that produced a sentence is the same register. State the
    # observation as the author's and drop the carrier.
    #
    # BARE `session` STAYS LEGAL and must not be banned. It names a transcript, a PowerShell
    # session, a recording session and a worktree's lifetime, all of them in this tree. Every
    # pattern here needs a second word that turns the session into an agent: `a later session`,
    # `in one session`, `this session found`, `reported by the crystallography session`.
    #
    # The same holds for orior. `orior's run` and `orior's 54-bit run` name this
    # repository's own measurement and stay. `orior's reading` credits a reader and goes.
    #
    # FOUR SHAPES MUST NOT BE ADDED: `handoff`, `passed off`, `a peer's` and `the specialist`, and
    # neither may bare `this session`. Each of them reaches sentences that credit nobody.
    r"\b(a|the|one|each|every|another) (later|earlier|previous|next|other|second|third|peer"
    r"|builder) sessions?\b",
    r"\bpeer sessions?\b",
    r"\b(every|each) session (builds|reads|takes|writes|runs)\b",
    r"\bin (one|a single|the same|an earlier|a later) session\b",
    r"\bthis session(?: (?:read|wrote|found|ran|measured|produced)\b|['’]s work\b)",
    r"\bsessions? (fed|relayed|reported|contributed)\b",
    r"\b(the|your|our) theorists?\b",
    r"\bproject architect\b",
    r"\bprecision measurement specialist\b",
    r"\blead of the private\b",
    r"\b(relayed|reported|flagged|found|caught|reproduced) by (the |a )?(\w+ )?(session|peer"
    r"|theorist|writer|specialist|architect)\b",
    r"\b(relayed|reported|flagged|found|caught|written) by orior\b",
    r"\borior['’]s (reading|framing|bounds?|why|formalization|hand conversion)\b",
    r"\b(agreed with|per) orior\b",
    r"\borior (confirmed|checked|changed|gave|framed)\b",
    r"\bbiohub-cell-tracking-\d+\b",
    r"\bleaderboard disruptor\b",
)
