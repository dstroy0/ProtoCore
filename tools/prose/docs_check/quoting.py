#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# Quoted spans, named spans and the regions a page asks this check to stay out of.
#

import re



EM_DASH = "—"

# Definitions that are somebody's name and never this project's prose. The International Conference on
# Salish and Neighbouring Languages defines its own name that way, and thirteen extraction scripts
# cite it in their headers. Americanizing a title misquotes it. A hit inside one of these is
# dropped before it is reported.
QUOTED = (
    re.compile(r"neighbouring languages", re.IGNORECASE),
    # The same title, wrapped across two comment lines by half the extraction headers.
    re.compile(r"salish and neighbouring", re.IGNORECASE),
    # A bibliography entry, which tex_prose passes over as its src: key and then the entry's text. The
    # authors and the title are the cited work's own, in its own definitions. Anchored to the start
    # of the run, because an entry opens its paragraph and a citation inside running prose does not.
    re.compile(r"\A\s*src:[\w-]+\s.{0,800}"),
)

# A span the writing sets off as a citation of a form. A token inside one is a NAME and not a USE,
# and that is the same reasoning QUOTED already carries for a proper name: the document is pointing
# at the token, not reaching for it.
#
# THIS IS THE HIGHEST-LEVERAGE PRECISION RULE HERE, and the way to see it is to run the checker
# over the two documents that authorize it. The overwhelming majority of what it reports on them
# sits inside one of these spans, and every one of those is the standard writing out a token it
# bans so a reader can see which token is meant. A checker that reports a standard for naming its
# own bans is reporting the wrong thing.
#
# THREE MARKERS. The backtick span is the bulk of it. The italic run is how both standards quote a
# banned shape too long to be a token: code-comments:161 writes *That is the difference between a
# helper naming a step and a helper costing a call* to show what an aphoristic clause reads like.
# The quoted span is the same device with quotes, and its floor is one character against PASSAGE's
# sixteen, because the named forms are short: *"Certainly!"* is ten characters and *"load-bearing"*
# is twelve, and both walk past PASSAGE.
#
# BOLD IS NOT A FOURTH MARKER AND MUST NOT BE ADDED. Bold marks a heading here far more often than
# it marks a citation, so the arm silences real TIER A findings sitting inside heading labels:
# "**What survives from F1/F2:**", "**Why it is deferred rather than fixed:**". It is a net loss.
#
# The italic arm errors on a span holding a table cell separator, because two unrelated asterisks
# in different cells pair across a row and swallow the text between them. A citation of a form does
# not straddle a cell boundary.
#
# WHAT THE ARM COSTS. On the two standards it removes findings in bulk, TIER A and TIER B both, and
# every one is the document writing out the token it bans. On ordinary source it removes almost
# nothing, and what it does remove is correct: `realm` in a fenced `on_http_auth(..., realm, ...)`
# signature and in a `WWW-Authenticate: Basic realm="..."` header, which is an HTTP field name. NO
# TIER A FINDING IS SILENCED ANYWHERE OUTSIDE THE STANDARDS. A banned word that happens to sit in
# backticks anywhere goes quiet, the same trade QUOTED and PASSAGE already make. Bounded to
# markdown alongside PASSAGE, except the backtick span, which reads the same way in a comment and is
# where a comment names a symbol.
NAMED_SPAN = re.compile(r"`[^`\n]{1,300}`")
NAMED_IN_MARKDOWN = (
    # Italic, excluding a neighbouring asterisk so **bold** is not read as an italic span opening
    # on its second asterisk, and excluding a cell separator for the reason above.
    re.compile(r"(?<!\*)\*[^*\n|]{1,300}\*(?!\*)"),
    # The short quoted form. PASSAGE stays for the long quotation, which is a different thing: it
    # exempts somebody else's words, and this exempts the document's own citation of a form.
    re.compile(r"[\"“][^\"“”\n]{1,600}[\"”]"),
)


# Turns the scan off between the two markers, for the other files that have to quote a banned phrase
# to explain it. Hyphenating one into is-what-makes would satisfy the regex and cost the reader the
# phrase they came to see. The markers are explicit and a reader can see what is exempt and why,
# where a silent per-file exemption shows them neither.
QUIET_OPEN = "docs-check: quoting"
QUIET_CLOSE = "docs-check: end quoting"


def quieted(lines):
    """The same lines with anything between the two markers blanked, line numbers preserved."""
    retained = []
    quiet = False
    for line in lines:
        if QUIET_OPEN in line:
            quiet = True
            retained.append("")
            continue
        if QUIET_CLOSE in line:
            quiet = False
            retained.append("")
            continue
        retained.append("" if quiet else line)
    return retained
