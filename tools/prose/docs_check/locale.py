#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# British convention against American, as a pattern and not as a list.
#


# ====================================================================
# THE LOCALE STAGE. BRITISH CONVENTION AGAINST AMERICAN, AS A PATTERN AND NOT AS A LIST
# ====================================================================
#
# code-documentation:149 states this rule in one sentence and states it as a shape: "Never a British
# variant, and the ban is on the pattern rather than on a list: no `-ise` or `-isation` where
# American takes `-ize` or `-ization`, no `-our` for `-or`, no `-re` for `-er`, no doubled `l` in
# `modelled`, `labelled`, `signalled`." code-comments:157 says the same thing shorter.
#
# ONE COPY. BANNED splices this tuple in directly, so a pattern added here is enforced here. A rule
# table duplicated into the table that enforces it drifts with no signal at all. Retain it spliced.
#
# THE NAMED PATTERNS BELOW ARE KEYS AND CANNOT BE REWRITTEN FREELY. HUMAN_RATE is keyed by pattern
# string and holds a measured rate for eight of them. Folding one into a general arm takes that rate
# out of submission_check.py's table with nothing to say it had gone. The general arms overlap the
# named ones, and banned_hits dedupes on (line, offset, token), so an overlap costs a wasted match
# and never a doubled finding.
#
# WHAT THE -ise ARM ENFORCES, worth a sentence because it reads as dialect detection and invites a
# correction toward Oxford. `-ise` is not reliably British; Oxford usage is `-ize`. A tree written
# in Oxford English trips none of this and a tree written in American English trips none of it
# either. The arm enforces a house American convention against one common British convention, and
# is not a claim about where the writer is from.
#
# THREE ARMS ARE DELIBERATELY ABSENT AND MUST STAY ABSENT.
#
#   An `-isable` arm matches `controldisable` and every other compound ending in `disable`.
#   A general doubled-l arm cannot be written: `controlled`, `installed` and `enrolled`
#   are American with two l's, so the doubled-l rule stays the named list the standard gives it.
#   A general `-re` arm cannot be written either: `are`, `here`, `more`, `figure` and `structure`
#   are the majority of English words ending in those two characters.
#
# The standard's sentence names four shapes and does not name the `ae` and `oe` digraphs
# (`haemoglobin`, `foetus`, `anaesthetic`). No arm is written for them, and a rule with no measured
# hit is a rule nobody can tune.

# The named forms, ahead of the general arms.
LOCALE_NAMED = (
    r"\blabelled\b",
    r"\bmodelled\b",
    r"\bneighbour",
    r"\bbehaviour",
    r"\bcolour",
    r"\bcentre\b",
    r"\bwhilst\b",
    r"\bamongst\b",
    # Bounded to the verb and its forms. A bare \borganis also matches organism, which is a word.
    r"\borganis(e|es|ed|ing|ation|ations)\b",
    # KNOWN FALSE POSITIVE, named here so the next reader does not rediscover it. `analyses` is also
    # the American plural of `analysis`, and this pattern cannot tell the noun from the verb.
    # Narrowing it drops the rate keyed to this exact string in HUMAN_RATE, so it stays as written.
    r"\banalyse(s|d)?\b",
)

# The suffixes an -ise stem takes. `-isable` and `-isability` are not among them: see above.
_ISE_TAIL = r"(?:e|es|ed|ing|er|ers)"

# English words ending in -ise where American also writes -ise, because the s belongs to the stem
# instead of to the Greek -ize suffix. Held as stems so one entry covers every inflection, and
# matched with any characters allowed in front of them. `madvise`, `imprecise`, `keycompromise` and
# `saxexerciser` are all left alone.
#
# TWO STEMS MUST NOT BE ADDED. `anis` exempts `organise`, which ends in those five characters, and
# `mortis` exempts `amortise`, which ends in those six. A stem matched from the end of a word has
# to be checked against the British words that end the same way.
_ISE_STEMS = (
    "advis",
    "revis",
    "devis",
    "televis",
    "improvis",
    "supervis",
    "chastis",
    "advertis",
    "exercis",
    "excis",
    "incis",
    "concis",
    "precis",
    "circumcis",
    "promis",
    "premis",
    "surmis",
    "demis",
    "compromis",
    "despis",
    "franchis",
    "merchandis",
    "paradis",
    "treatis",
    "expertis",
    "valis",
    "enterpris",
    "compris",
    "surpris",
    "appris",
    "repris",
    "upris",
    "sunris",
    "denis",
)

# Words ending in -our that American defines the same way. `our`, `your`, `four`, `hour`, `tour`,
# `pour`, `sour`, `dour`, `flour`, `scour` and `amour` are absent on purpose: the {3,} floor in the
# arm already errors on them, and listing a short one here would exempt every British word ending in
# the same characters. `dour` would take `ardour` and `candour` with it.
_OUR_WORDS = (
    "devour",
    "contour",
    "detour",
    "velour",
    "glamour",
    "paramour",
    "troubadour",
    "bonjour",
    "seymour",
    "hour",
)

_OUR_TAIL = r"(?:s|ed|ing|er|ers|ite|ites|able|ably|ful|fully|less|ly|al|ally)?"

LOCALE = LOCALE_NAMED + (
    # -ise where American takes -ize. The character before `is` must be a consonant, which errors
    # `noise`, `raise`, `praise`, `guise`, `cruise`, `tortoise` and `malaise` without naming one of
    # them. `w` is out of the class, which does not match `otherwise`, `likewise` and every
    # `-wise` compound. The {2,} floor does not match `wise`, `rise`, `prise`, `arise` and `anise`, which are too
    # short to reach it. What is left is the suffix attached to a stem, and _ISE_STEMS names the
    # English words that reach that shape and are not British.
    r"\b(?![A-Za-z]*(?:%s)%s\b)[A-Za-z]{2,}[bcdfghjklmnpqrstvxz]is%s\b"
    % ("|".join(_ISE_STEMS), _ISE_TAIL, _ISE_TAIL),
    # -isation, -isations and -isational, where American takes -ization. This arm needs no
    # exemption list: no American word ends in those characters.
    r"\b[A-Za-z]{3,}isation(?:al|s)?\b",
    # -yse where American takes -yze. `analys` is here for the forms the named pattern above cannot
    # reach, which are `analyser`, `analysers` and `analysing`. The two overlap on `analysed` and
    # banned_hits reports the site once.
    r"\b(?:analys|paralys|catalys|dialys|electrolys|hydrolys)(?:e|ed|ing|er|ers)\b",
    # -our where American takes -or. The {3,} floor carries the majority of the work and _OUR_WORDS carries
    # the rest. The tail reaches `behavioural`, `colourful`, `favourite`, `labourer` and
    # `neighbourless`, which the two named patterns above never did.
    r"\b(?![A-Za-z]*(?:%s)%s\b)[A-Za-z]{3,}our%s\b"
    % ("|".join(_OUR_WORDS), _OUR_TAIL, _OUR_TAIL),
    # -re where American takes -er, as the named list the shape demands. The leading [A-Za-z]*
    # reaches `kilometres`, `millimetre`, `epicentre` and `multicentre`.
    r"\b[A-Za-z]*(?:centre|metre|theatre|fibre|litre|calibre|sabre|sombre|spectre"
    r"|lustre|meagre|manoeuvre|sceptre)s?\b",
    # The doubled l the standard names by name, plus the rest of the same class. A general rule is
    # impossible here: American doubles the l in `controlled`, `installed`, `enrolled`
    # and `called`. Only a list can separate them.
    r"\b(?:labell|modell|signall|travell|cancell|levell|totall|fuell|diall|marvell"
    r"|counsell|equall|initiall|spirall|tunnell|quarrell|refuell|shovell)"
    r"(?:ed|ing|er|ers|ors|or)\b",
    # The other half of the l rule, where British writes one and American writes two. Bounded to the
    # forms that differ: `fulfilled` and `appalling` are defined the same on both sides, and the
    # word boundary after `fulfil` retains them out.
    r"\b(?:fulfil|fulfils|fulfilment|fulfilments|enrol|enrols|enrolment|enrolments"
    r"|instal|instals|instalment|instalments|skilful|skilfully|wilful|wilfully"
    r"|enthral|enthrals|appal|appals|distil|distils|instil|instils)\b",
    # -ce where American takes -se, for the four nouns that differ. British usage splits `licence`
    # the noun from `license` the verb and American writes `license` for both. Only the -ce form
    # is ever wrong here and the verb needs no exemption.
    #
    # A `licence` inside an SPDX or copyright block is a legal artifact and a different question
    # from a house convention. That distinction belongs to the exclusions pass and is not made here:
    # this arm reports the site and a person decides it. A prose finding exists for that.
    # Prose ABOUT a license block, sitting beside a real SPDX header, is reported like any prose.
    r"\b(?:defence|offence|pretence|licence)s?\b",
    # -ogue for the two the standard names, `catalog` and `analog`. `dialogue`, `monologue`,
    # `epilogue` and `prologue` are standard American and are deliberately absent.
    r"\b(?:catalogue|analogue)[sd]?\b",
    # `programme`, but never `programmed` or `programming`, which are American. Widening this to
    # the stem matches both and buries the report.
    r"\bprogrammes?\b",
    # Named words with no shared shape to generalize over. Each one differs from its American
    # form in a way none of the arms above describes.
    r"\b(?:artefact|aluminium|sulphur|storey|tyre|cheque|draught|mould|speciality"
    r"|jewellery|woollen|aeroplane|moustache|pyjamas|kerb|plough|gaol)s?\b",
    # `gray` is named at code-documentation:149. Bounded so `greyhound` is untouched, and a proper
    # name defined Grey is a false positive a person decides, and a prose finding exists for that.
    r"\bgrey(?:scale|s|ish)?\b",
)
