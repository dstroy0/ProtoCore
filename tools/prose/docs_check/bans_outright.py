#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# The tokens the writing standard bans outright, with no construction around them.
#

OUTRIGHT = (
    r"\brather\b",
    r"\badd up\b",
    # Named by hand. The X-not-Y shape is banned generally by the writing standard and permitted
    # where a reader would otherwise land on the wrong one, which no regex can tell apart. This
    # is the instance that was called out, and the list grows one phrase at a time for that
    # reason.
    r"cost and not a defect",
    # The same shape, caught by its grammar. It reports where the standard permits it too. It is
    # a prose finding and never a breaking one: a person decides each site.
    r"\b(is|was|are|were) an? [\w-]+ and not an? [\w-]+",
    # The same shape with no copula and no article, which is how code-documentation section 146
    # writes both of the examples it bans by name: "Declared, not allocated." and "A number, not
    # a guess." Every X-not-Y pattern in this file wanted (is|was|are|were) and an article. The
    # two sentences the standard names walked past all three of them. Derived by running the
    # standard's own illustrations through the checker: 4 of 28 named phrases were missed and
    # two were these.
    #
    # Bounded to a whole short sentence of that shape, as both examples are. The
    # mid-sentence appositive, "the bound is read in the header, not in the .c", is left alone:
    # the standard permits the contrast where a reader would otherwise land on the wrong one,
    # and reaching for every comma costs more true sentences than the tic is worth.
    r"(?m)(?:\A|(?<=[.!?] ))(?:(?:An?|The) )?[\w-]+, not (?:(?:an?|the) )?[\w-]+\.(?:\s|\Z)",
    # Nothing inanimate speaks. The standard names the subjects it bans: a name, a definition, a
    # token or a type. A paper, a table and an entry are texts and legitimately say things, and
    # a pattern that includes them reports sites that are all correct, so they stay out of the
    # subject alternation.
    #
    # make clear belongs in the verb alternation. Both standards name it in the same list
    # as say, signal, encode, convey, advertise and announce -- code-documentation section 147
    # and code-comments section 156 -- and the alternation has to carry all seven. "The header
    # makes clear that the payload follows" needs the last of them to report.
    r"\b(name|definition|token|type|structure|constraint)s?\s+(says|say|signals|signal|encodes|encode|"
    r"conveys|convey|announces|announce|advertises|advertise|makes clear|make clear)\b",
    # `so an` is the same construction as `so a`. code-documentation:112 describes a CLAUSE, "the
    # personified consequence clause", so a pattern that implements a TOKEN measures the matcher
    # and not the tree. The inflection is the article agreeing with the next word, which is not a
    # property the ban turns on.
    r"\bso an?\b",
    # The comma-then-so consequence clause, widened from the two-token opener above. Every comma-so
    # clause is banned whatever word follows, not just
    # the article form: the-comma-so-the, comma-so-it and comma-so-every cases the opener
    # missed. runs() joins a comment block with a space, which places a comma ending one line
    # beside the so opening the next, and this pattern reads that join as one string, catching
    # the across-a-line-break case the same. Comma-so-that and comma-so-far are different
    # constructions and are excluded. The and-so-on, if-so and do-so forms carry no comma and
    # never reach this pattern.
    r",\s+so\s+(?!that\b|far\b)",
    # Banned outright by code-comments section 200, which names three tokens and bounds them in
    # the same sentence: "none has a legitimate use in a comment here". `so a` and `rather` are
    # banned for documentation as well by code-documentation section 110. The third is banned
    # in every form, in a comment and on every page alike, and the define family is in for it.
    r"\b(?:mis)?spell(?:s|ed|ing|ings)?\b",
    r"load-bearing",
)
