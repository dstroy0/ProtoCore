#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# What was excluded and why, counted so a silence and a zero can be told apart.
#



# ====================================================================
# THE EXCLUSION LAYER. EVERY EXCLUSION ERRORS AND SAYS SO
# ====================================================================
#
# THE FOUR STAGES CALL IN HERE AND NONE OF THEM CARRIES A SKIP OF ITS OWN. A skip written four times
# is four places to forget it and four places for the four copies to drift apart. That is the same
# fault the note above LOCALE records about a rule table duplicated into the table that enforces it.
# There are three call sites and no others: prose_only applies the blanking rules once, and
# em_dashes, markdown_leftovers and banned_tokens therefore inherit them without knowing they exist;
# banned_hits applies the run-level context rules beside QUOTED; main() applies the file-level and
# region-level rules and prints what fired.
#
# NOTHING IS SKIPPED QUIETLY. "Scanned none" and "there are none" must never print the same
# nothing. So every exclusion below records what it dropped and why, into a Ledger, and main()
# prints that ledger under the counts. A file this tool declines to read is
# named by the run that declined to read it.
#
# EVERY EXCLUSION STATES ITS REASON IN THE SOURCE AND NOT ONLY ITS RULE. A bare list of paths is the
# type of thing a later maintainer deletes as overcautious, and they are right to: a rule nobody can
# check is a rule nobody can retain. A list saying why survives.
#
# EVERY RULE HERE IS A FIRST-CLASS RULE AND NOT A SPECIAL CASE. Each one has a name, a reason, a
# measured cost and a test. A special case is a rule with none of those, and it is the thing the
# next person deletes.


class Ledger(object):
    """What a run excluded and why, retained so the run can say so.

    Held in first-appearance order, because the order an exclusion first fires is the order a reader
    meets the tree. Sites are retained whole and not only counted: the question after a surprising count
    is always which ones, and a ledger that cannot answer it sends a reader back to the source.

    A caller passing no ledger still gets the exclusion. Recording is the optional part, never the
    rule. A caller that has not been taught about the ledger cannot turn an exclusion off by
    forgetting to pass one.
    """

    def __init__(self):
        self.order = []
        self.held = {}

    def note(self, rule, reason, detail):
        """Record one exclusion: its class, why the class exists, and the site it fired on."""
        key = (rule, reason)
        if key not in self.held:
            self.order.append(key)
            self.held[key] = []
        self.held[key].append(detail)

    def total(self):
        return sum(len(one) for one in self.held.values())

    def report(self, displayed=8):
        """Printable lines naming each rule, its reason, its count and the sites it fired on."""
        out = []
        for key in self.order:
            sites = self.held[key]
            out.append("    %s: %d, %s" % (key[0], len(sites), key[1]))
            for one in sites[:displayed]:
                out.append("      %s" % one)
            if len(sites) > displayed:
                out.append("      ... and %d more" % (len(sites) - displayed))
        return out
