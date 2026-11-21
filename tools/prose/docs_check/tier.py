#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# Which tier a pattern is in, and the note on why a regex never decides that.
#

from .bans import BANNED
from .grammar import GRAMMAR_AUTHORITY
from .locale import LOCALE



# ====================================================================
# THE TWO TIERS, AND WHAT DECIDES WHICH ONE A PATTERN IS IN
# ====================================================================
#
# TIER A is a NAMED-CONSTRUCTION BAN: a construction one of the two standards bans in a sentence,
# quoted below with the file and line it is on, or a phrase or construction the NARA Writing Style
# Guide names in a section, cited by the section in grammar.py. Tree-wide, no opt-in, no per-repo setting, every hit
# a finding. A per-repo switch on this tier would exempt a repository from a standard it is already
# under, which is backwards: the standard is the tree's, not each repository's.
#
# TIER B is FREQUENCY-SCORED VOCABULARY: a word or an idiom, reported with what it costs a human
# writer where that has been measured. The majority of it is the machine-prose vocabulary code-documentation
# section 135 through 141 lists by word. The rest is this file's own house style, calibrated on
# orior's theory research papers and named as such in the report.
#
# THE TIER IS DECIDED BY THE SENTENCE IN THE STANDARD, NEVER BY THE REGEX. This is the correction
# that matters and it runs both ways:
#
#   `rather` is one token and matches one word. Stage_of calls it a word. Its ban is stated
#   outright at code-documentation:110 and again at code-comments:200. It is TIER A.
#   `\bis what (separates|retains|...)` is a construction by shape and by authority alike, and the
#   verbs this file added to it beyond the standard's ten are TIER A all the same, because the
#   construction is the thing banned and the verb list is how far it reaches.
#   The X-not-Y patterns span several words and are TIER A. The superlative-adjective group spans
#   several words too and is TIER B, because section 137 lists adjectives.
#
# WHAT THE TIER CHANGES. Nothing about whether a run passes: prose never fails a build in either
# tier, --strict included, and the exit rule at the foot of main() is the whole of that contract.
# It changes what the report says, and it is the line an autofix would have to respect.
#
# A BANNED HINGE IS DISSOLVED, NEVER REPLACED. There is no --fix here and there must never be one
# for TIER A. code-documentation:110 bans `rather`; its obvious repair is the X-not-Y shape, which
# section 146 bans forty lines later in the same document: "The X-not-Y shape sounds decisive and
# carries almost nothing... State the thing that is true and let the contrast go unsaid, unless the
# reader would otherwise land on the wrong one." An automatic repair of the first rule produces the
# second at scale and reports a fix for every one. The two legal treatments are to give the second
# half its own plain sentence, or to drop the weaker half, and section 146's test decides which. A
# machine cannot run that test. Section 143 says the same thing from the other side: bans name
# CONSTRUCTIONS. Detection AND repair operate on constructions and never on words. TIER A is
# report-only, permanently. A token-for-token orthographic swap is the only class an autofix could
# ever own here, and that is the alphabet stage and not this table.
AUTHORITY = {
    # Three tokens banned outright by code-comments:200. Two of them, `so a` and `rather`, also
    # carry a documentation ban at code-documentation:110. The third is banned in every form and
    # on every page, and the define family is in for it.
    r"\brather\b": "code-documentation:110, code-comments:200",
    r"\bso an?\b": "code-documentation:110, code-comments:200",
    # The comma-so consequence clause code-documentation:112 describes, which the so-a token above
    # only partly reaches. The ban is on every comma-so clause whatever word follows. It is a named
    # construction and belongs in Tier A.
    r",\s+so\s+(?!that\b|far\b)": "code-documentation:112",
    r"\b(?:mis)?spell(?:s|ed|ing|ings)?\b": "code-comments:200, every page",
    r"\badd up\b": "code-documentation:110",
    # The measured tics. code-documentation:116 through :121 gives each one its rise.
    r"\bthe one that matters\b": "code-documentation:116",
    r"\bis the one\b": "code-documentation:116",
    r"\bwhich is (why|what|the)\b": "code-documentation:117, code-comments:206",
    r"\band nothing else\b": "code-documentation:118, code-comments:207",
    r"\bthat is the whole\b": "code-documentation:119",
    r"\bis the whole (of|rule|point|thing|question|claim|job|story)\b": "code-documentation:119",
    r"\bthe whole (point|question|claim|rule|job|story) (is|was)\b": "code-documentation:119",
    r"\bwhat survives\b": "code-documentation:120",
    # The second pass. code-documentation:126 through :131.
    r"\bis what makes\b": "code-documentation:126, code-comments:205",
    r"\bis what (separates|retains|tells|puts|says|makes|supplies|gives|decides|holds|stops|lets"
    r"|carries|costs|reads|buys|pays|spends|earns|wins|prices|slots|books)\b": "code-documentation:126, code-comments:205",
    r"\b(is|was|are|were) an? [\w-]+ and never an? [\w-]+": "code-documentation:127",
    r"\band nothing more\b": "code-documentation:128, code-comments:207",
    r"\band no more\b": "code-documentation:128, code-comments:207",
    r"\bthe one (thing|place|case|word|reason|table|file|grain|repair|mistake|addition|column)\b": "code-documentation:129, code-comments:207",
    r"\bthe whole of (the|what|it)\b": "code-documentation:130, code-comments:207",
    r"\bis precisely (what|why|the)\b": "code-documentation:131",
    r"\b(what|that) matters (is|here|max)\b": "code-documentation:131",
    # No rhetorical sentence shapes. code-documentation:146, code-comments:199.
    r"cost and not a defect": "code-documentation:146",
    r"\b(is|was|are|were) an? [\w-]+ and not an? [\w-]+": "code-documentation:146",
    r"\b(is|was|are|were) the [\w-]+ and not the [\w-]+": "code-documentation:146",
    r"(?m)(?:\A|(?<=[.!?] ))(?:(?:An?|The) )?[\w-]+, not (?:(?:an?|the) )?[\w-]+\.(?:\s|\Z)": "code-documentation:146",
    r"\bhas no call and no text:": "code-documentation:146",
    # Nothing inanimate speaks. code-documentation:147, code-comments:156.
    r"\b(name|definition|token|type|structure|constraint)s?\s+(says|say|signals|signal|encodes|encode|"
    r"conveys|convey|announces|announce|advertises|advertise|makes clear|make clear)\b": "code-documentation:147, code-comments:156",
    # No conversational filler. Four forms are named by name in both files.
    r"load-bearing": "code-documentation:148",
    r"\blet['’]s\b": "code-documentation:148, code-comments:155",
    r"\bdiv(e|es|ing) (into|in|deeper)\b": "code-documentation:148, code-comments:155",
    r"\b(certainly|absolutely|of course)[!,]": "code-documentation:148, code-comments:155",
    r"\bit (is|'s) (important|worth|useful|helpful) (to note|noting|to remember|to mention|mentioning)\b": "code-documentation:148, code-comments:155",
    # The machine register. code-documentation:141.
    r"\bas an ai\b": "code-documentation:141",
    r"\bas a language model\b": "code-documentation:141",
    r"\bmy training data\b": "code-documentation:141",
    r"\b(i apologi[sz]e|my apologies|sorry for the)\b": "code-documentation:141",
    # One author. The register code-documentation:141 bans, in the form that credits a session, a
    # role or orior with the author's own observation.
    r"\b(a|the|one|each|every|another) (later|earlier|previous|next|other|second|third|peer"
    r"|builder) sessions?\b": "code-documentation:141, one author",
    r"\bpeer sessions?\b": "code-documentation:141, one author",
    r"\b(every|each) session (builds|reads|takes|writes|runs)\b": "code-documentation:141, one author",
    r"\bin (one|a single|the same|an earlier|a later) session\b": "code-documentation:141, one author",
    r"\bthis session(?: (?:read|wrote|found|ran|measured|produced)\b|['’]s work\b)": "code-documentation:141, one author",
    r"\bsessions? (fed|relayed|reported|contributed)\b": "code-documentation:141, one author",
    r"\b(the|your|our) theorists?\b": "code-documentation:141, one author",
    r"\bproject architect\b": "code-documentation:141, one author",
    r"\bprecision measurement specialist\b": "code-documentation:141, one author",
    r"\blead of the private\b": "code-documentation:141, one author",
    r"\b(relayed|reported|flagged|found|caught|reproduced) by (the |a )?(\w+ )?(session|peer"
    r"|theorist|writer|specialist|architect)\b": "code-documentation:141, one author",
    r"\b(relayed|reported|flagged|found|caught|written) by orior\b": "code-documentation:141, one author",
    r"\borior['’]s (reading|framing|bounds?|why|formalization|hand conversion)\b": "code-documentation:141, one author",
    r"\b(agreed with|per) orior\b": "code-documentation:141, one author",
    r"\borior (confirmed|checked|changed|gave|framed)\b": "code-documentation:141, one author",
    r"\bbiohub-cell-tracking-\d+\b": "code-documentation:141, one author",
    r"\bleaderboard disruptor\b": "code-documentation:141, one author",
    # The which-is clause again, in the form the later tier wrote it.
    r"\bwhich is (what|why|how|the (difference|point|whole|answer|reason|rule))\b": "code-comments:206",
}

# The sections of the style guide, for the plain phrases and the constructions in grammar.py. A pattern
# a standard above already cites retains that citation.
for _pattern, _cited in GRAMMAR_AUTHORITY.items():
    AUTHORITY.setdefault(_pattern, _cited)

# A selector that names nothing is drift, and drift in a table like this is silent: the tier simply
# stops being claimed and every finding in it prints as house style. Raised at import rather than
# reported at runtime, because a tier that quietly holds no patterns reports clean forever.
_ORPHANS = tuple(one for one in AUTHORITY if one not in BANNED)
if _ORPHANS:
    raise SystemExit(
        "docs_check: AUTHORITY names %d pattern(s) that are not in BANNED. A tier "
        "selector matching nothing reports clean forever.\n  %s"
        % (len(_ORPHANS), "\n  ".join(_ORPHANS))
    )


# The tokens whose ban holds in comments only. None is: of the three code-comments:200 bans
# outright, `so a` and `rather` carry a documentation ban of their own at code-documentation:110,
# and the third is banned on every page. The set stays for a ban a standard scopes to comments.
COMMENT_ONLY = frozenset()


def tier_of(pattern):
    """A for a named-construction ban, B for frequency-scored vocabulary, alphabet for a locale form.

    Read AUTHORITY above for why this is not stage_of with different words.
    """
    if pattern in LOCALE:
        return "alphabet"
    return "A" if pattern in AUTHORITY else "B"
