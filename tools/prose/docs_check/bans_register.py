#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# The register tics: the sound of writing that is performing instead of explaining.
#

REGISTER = (
    # The register tics. These are not wrong facts, they are the sound of writing that is
    # performing instead of explaining, and they were found by reading a batch of this project's
    # own prose back and noticing the same shapes in every paragraph. Each one is a sentence
    # that exists for its rhythm: delete it and the paragraph loses nothing but its swagger.
    #
    # Raising a clause to a verdict.
    r"\bthe one that matters\b",
    r"\bis the one\b",
    r"\bthat is the whole\b",
    r"\bis the whole (of|rule|point|thing|question|claim|job|story)\b",
    r"\bthe whole (point|question|claim|rule|job|story) (is|was)\b",
    # Explaining the sentence just written instead of writing it once.
    r"\bwhich is (why|what|the)\b",
    r"\bthat is (what|why) \w+ (is|was|does|did)\b",
    r"\bis exactly (what|why|the)\b",
    r"\bis what makes\b",
    # Dramatic tags on the end of a clause.
    r"\band nothing else\b",
    r"\bon purpose\b",
    r"\bwhat survives\b",
    r"\bnever .{1,40}, always\b",
    #
    # Second pass. These were found by rewriting the first pass and watching which shapes the
    # rewrite reached for. A sweep that only removes the listed phrases moves the register into
    # whatever is one step away from them, and these are the steps it took.
    #
    # The is-what-VERBs construction, generalized from the makes variant above.
    # code-documentation section 126 names ten verbs and code-comments section 205 names six of
    # the same ten.
    #
    # The second alternation is this file's own, and it is where the bare carry, hold, cost,
    # read, buy, pay, spend, earn, win, price and slot bans went when they came out below.
    # Section 143 states the rule those bans were breaking: "a word that reads as a tic in one
    # construction only is bounded to that construction." This is the construction.
    r"\bis what (separates|retains|tells|puts|says|makes|supplies|gives|decides|holds|stops|lets"
    r"|carries|costs|reads|buys|pays|spends|earns|wins|prices|slots|books)\b",
    # The X-not-Y shape wearing never instead of not. The first sweep wrote these by hand.
    r"\b(is|was|are|were) an? [\w-]+ and never an? [\w-]+",
    # The same dramatic tag as the else variant above, one synonym over.
    r"\band nothing more\b",
    r"\band no more\b",
    # Raising a definite article into a verdict, which is-the-one catches in one tense only.
    r"\bthe one (thing|place|case|word|reason|table|file|grain|repair|mistake|addition|column)\b",
    r"\bthe whole of (the|what|it)\b",
    r"\bis precisely (what|why|the)\b",
    r"\b(what|that) matters (is|here|max)\b",
    #
    # The machine-prose vocabulary. None of these is wrong English and none is a claim about a
    # measurement. None of them breaks a build. They are the words a reader has learned
    # to read as unwritten, and a page carrying them gets skimmed instead of read. Nothing in a
    # library about memory, entropy or crystal axes needs any of them.
    #
    # Two rules retained words off this list. A term the field owns stays: a grammatical paradigm, a
    # robust estimator, a significant difference, an alignment. And a word that only reads as a
    # tic in one construction is bounded to that construction instead of banned outright.
    r"\bdelve",
    r"\btapestry\b",
    r"\brealm\b",
    r"\bmyriad\b",
    r"\bplethora\b",
    r"\bseamless",
    r"\bgame.?chang",
    r"\bcutting.edge\b",
    r"\bstate.of.the.art\b",
    # Bounded to the shape. A grammatical paradigm is the field's own word and six sites use it.
    r"\bparadigm shift\b",
    r"\bsynergy\b",
    r"\bholistic\b",
    r"\bmeticulous",
    r"\bintricate\b",
    r"\bprofound\b",
    r"\bpivotal\b",
    r"\bcrucial\b",
    r"\bvital\b",
    r"\bnuanced\b",
    r"\bmultifaceted\b",
    r"\bcompelling\b",
    r"\binvaluable\b",
    r"\bunparalleled\b",
    r"\bunwavering\b",
    r"\btransformative\b",
    r"\bgroundbreaking\b",
    r"\brevolutioni[sz]",
    r"\bvibrant\b",
    r"\bbustling\b",
    r"\bnestled\b",
    r"\bcaptivat",
    r"\bresonat(e|es|ed|ing)\b",
    r"\btreasure trove\b",
    r"\bwealth of\b",
    r"\babundance of\b",
    r"\becosystem\b",
    r"\btestament to\b",
    r"\bboasts\b",
    r"\bshowcase",
    r"\bempower",
    r"\bstreamline",
    r"\bembark\b",
    r"\bendeavor\b",
    r"\bfoster(s|ing)?\b",
    r"\bcultivat(e|es|ing)\b",
    r"\belevat(e|es|ing)\b",
    r"\bbolster",
    r"\bunderpin",
    r"\bspearhead",
    r"\bleverag(e|es|ed|ing)\b",
    r"\butili[sz](e|es|ed|ing|ation)\b",
    r"\bfacilitat(e|es|ed|ing)\b",
    r"\bunleash",
    r"\bsupercharg",
    r"\beffortless",
    r"\buser.friendly\b",
    r"\btop.notch\b",
    r"\bworld.class\b",
    r"\bmust-have\b",
    r"\bunlock(s|ing)? the\b",
    r"\bharness(es|ing)? the\b",
    r"\bnavigat(e|es|ing) the\b",
    r"\bthe landscape of\b",
    r"\bjourney\b",
    r"\bdeep div",
    r"\bdiv(e|es|ing) (into|in|deeper)\b",
    r"\bunpack (this|the|that)\b",
    r"\bcircle back\b",
    r"\bever.(evolving|changing)\b",
    r"\brich history\b",
    r"\bkey (takeaway|insight)",
    r"\bactionable\b",
    r"\bbest practices\b",
    r"\bcomprehensive guide\b",
    r"\baforementioned\b",
    r"\balbeit\b",
    r"\bpertaining to\b",
    # Verbs and phrases that assert significance without stating any.
    r"\b(underscores|highlights|showcases|illustrates|exemplifies) the\b",
    r"\bsheds light on\b",
    r"\bpaves the way\b",
    r"\bplays an? [\w-]*\s?role\b",
    r"\b(serves|is) as an?\b",
    r"\bstands out\b",
    r"\bsets it apart\b",
    r"\bat (its core|the heart of)\b",
    r"\blies at the\b",
    r"\bone of the max\b",
    r"\bsome of the max\b",
    # Filler that opens a sentence and carries nothing.
    r"\bit (is|'s) (important|worth|useful|helpful) (to note|noting|to remember|to mention|mentioning)\b",
    r"\bit should be noted\b",
    r"\b(furthermore|moreover|additionally)\b",
    r"\b(notably|interestingly|importantly|crucially|remarkably)\b",
    # Bounded to the punctuation the filler form carries. Unbounded, "in summary" matched
    # `for row in summary` and asked an agent to rewrite a list comprehension.
    r"\bin (conclusion|summary|essence)[,.:]",
    r"\bto sum up\b",
    r"\ball in all\b",
    r"\bthe bottom line\b",
    r"\bat the end of the day\b",
    r"\bin (today's|an era|the world of)\b",
    r"\bfirst and foremost\b",
    r"\blast but not least\b",
    r"\bneedless to say\b",
    r"\bit goes without saying\b",
    r"\bfor all intents and purposes\b",
    r"\bthat (being said|said),",
    r"\bon the other hand\b",
    r"\b(arguably|essentially|basically|fundamentally)\b",
    r"\bquite simply\b",
    r"\b(simply|put) put\b",
    r"\bin many ways\b",
    r"\bto some extent\b",
    r"\bwhen it comes to\b",
    r"\bin (order to|terms of)\b",
    r"\bdue to the fact that\b",
    r"\bwith regards? to\b",
    r"\ba (wide range|variety) of\b",
    r"\bthere is no denying\b",
    r"\blook no further\b",
    # Talking to the reader instead of writing for them. Bounded to the contraction: lets is a
    # verb.
    r"\blet['’]s\b",
    r"\blet us\b",
    r"\bwe (will|'ll) (explore|look|dive|examine|see)\b",
    r"\bas you can see\b",
    r"\bwhether you'?re\b",
    r"\bimagine (a|an|the|that|if)\b",
    r"\bpicture this\b",
    r"\bthink of it as\b",
    r"\bhere'?s (the thing|why|how)\b",
    r"\bthe truth is\b",
    r"\b(certainly|absolutely|of course)[!,]",
    r"\bgreat question\b",
    r"\b(hope this helps|happy to help|feel free to|let me know if)\b",
    r"\bas an ai\b",
    # The not-just-X-but-Y shape, the X-not-Y shape inflated.
    r"\bnot (just|only) .{1,40}\bbut (also )?\b",
    r"\bmore than just\b",
    r"\bit'?s not (just )?about\b",
    #
    # Superlative adjectives, which promise a size without giving one. A measurement states how
    # large it is; these state that it was large.
    r"\b(remarkable|noteworthy|impressive|exceptional|extraordinary|outstanding)\b",
    r"\b(stellar|superb|phenomenal|tremendous|immense|enormous|staggering)\b",
    r"\b(countless|endless|limitless|boundless|unmatched|unrivall?ed)\b",
    r"\b(premier|foremost|quintessential|iconic|legendary|timeless)\b",
    # sweeping came off this line. A parameter sweep is an operation this tree runs, and the
    # rule made three agents reword correct prose about one.
    r"\b(far.reaching|wide.ranging|all.encompassing|overarching)\b",
    # imperative came off this line. code-comments:99 requires "@brief Single-sentence
    # summary using imperative voice". The grammatical mood is the field's own word here and the
    # rule reported the standard for naming it. Section 143's first escape: a term the field
    # owns stays.
    r"\b(indispensable|paramount)\b",
    r"\b(sophisticated|layered|expansive|exhaustive)\b",
    r"\b(stunning|striking|breathtaking|awe.inspiring|mesmeri[sz]ing|dazzling)\b",
    r"\b(intuitive|elegant|sleek|polished|frictionless)\b",
    r"\b(rigorous|painstaking|diligent|thorough)\b",
    r"\b(innovative|disruptive|trailblazing|visionary)\b",
    # powerful came off this line. The thought experiments discuss power as a subject, and the
    # rule turned "selects for the powerful" into a rewrite of a claim about people.
    r"\b(scalable|versatile)\b",
    # Verbs that inflate what the code does.
    r"\b(uncover|unveil|illuminate|unearth|unravel|demystify)",
    # amplify, boost and maximize came off this line. All three are literal here: a controller
    # arm amplifies, an audio band is boosted, and a quantity is maximized in the ordinary math
    # sense.
    r"\b(enhance|augment)\b",
    # transform came off this line and cost the max. A Fourier transform, inverse transform
    # sampling and a reversible transform are all nouns of art in this tree, and the pattern
    # reported 22 correct sites. The inflating use is a marketing verb; it is not worth this.
    r"\b(redefine|reimagine|reinvent)(s|ed|ing)?\b",
    r"\b(traverse|venture|spotlight|champion|nurture)\b",
    r"\btap into\b",
    r"\bbridge the gap\b",
    r"\b(open the door|set the stage|position the foundation)\b",
    # Office idiom. Every one of these replaces a fact with a gesture at one.
    r"\ba double.edged sword\b",
    r"\bthe tip of the iceberg\b",
    r"\ba perfect storm\b",
    r"\bfood for thought\b",
    r"\bthe elephant in the room\b",
    r"\blow.hanging fruit\b",
    r"\bmov(e|es|ing) the needle\b",
    r"\bboil the ocean\b",
    r"\bnorth star\b",
    r"\bsecret sauce\b",
    r"\bsilver bullet\b",
    r"\bholy grail\b",
    r"\bforce multiplier\b",
    r"\btable stakes\b",
    r"\bquantum leap\b",
    r"\b(sea|step) change\b",
    r"\b(30|10),?000.foot view\b",
    r"\bon the same page\b",
    r"\bhit the ground running\b",
    r"\bthink outside the box\b",
    r"\bpush the envelope\b",
    r"\braise the bar\b",
    r"\bthe name of the game\b",
    r"\bin a nutshell\b",
    r"\bthe crux of\b",
    r"\bat its heart\b",
    # More of the array-of-things filler.
    r"\ba (wide array|host|multitude|spectrum|plethora) of\b",
    r"\ban array of\b",
    r"\bthe (intersection|convergence) of\b",
    # Transitions that announce a turn the sentence already made.
    r"\b(having said that|with that said)\b",
    r"\bin light of\b",
    r"\bas such,",
    r"\b(consequently|nevertheless|nonetheless|conversely)\b",
    r"\bindeed,",
    r"\bof note,",
    r"\b(it is here that|this is where)\b",
    r"\benter (the|a) \w+\.",
    # Blog scaffolding. None of it belongs in a comment or a research page.
    r"\bkey takeaways\b",
    r"\btl;?dr\b",
    r"\bpros and cons\b",
    r"\bin this (article|post|guide|section, we)\b",
    r"\bwe'?ll cover\b",
    r"\bby the end of this\b",
    r"\bwithout further ado\b",
    r"\bstay tuned\b",
    # Hedged attribution with nobody attached.
    r"\b(one might argue|some might say|it could be argued|it bears mentioning)\b",
    # The machine register. None of this is written by a person about their own code.
    r"\bas a language model\b",
    r"\bi (don'?t|do not) have (the ability|access|personal)\b",
    r"\bmy training data\b",
    r"\b(i apologi[sz]e|my apologies|sorry for the)\b",
)
