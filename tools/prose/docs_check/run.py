#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# The run itself: the roots, the walk, the report and the exit status.
#

import os
import re
import sys

from .changed import changed_paths, changed_scope, on_changed
from .files import walk_markdown
from .fixes import fix_plan
from .generated import generated_regions
from .ledger import Ledger
from .manifest import reconcile_command
from .markdown import dead_links, empty_tables, markdown_leftovers
from .printed import printed_hits
from .prose import marker_edges, prose_only
from .punctuation import smart_quotes
from .ratchet import (
    DEFAULT_RATCHET,
    ratchet_read,
    ratchet_slack,
    ratchet_write,
    staged_paths,
    staged_under,
)
from .readings import reading, show_lines, show_slack
from .refs import manifests_covering, refs_for
from .repository import DEFAULT_ROOTS, REPOSITORY
from .scan import attributed, banned_tokens, em_dashes
from .ste import ste_findings



def option_value(name):
    """The value of a --name=value option, or None where it was not given."""
    for one in sys.argv[1:]:
        if one.startswith(name + "="):
            return one[len(name) + 1:]
    return None


def main():
    # Findings quote the text they flag, which can hold any character (U+2212 in a formula). A
    # Windows console defaults to cp1252 and raised on it, ending the run before later findings
    # printed.
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    # The readings that answer from the tables or from a diff and never walk the tree. Dispatched
    # before anything else, because none of them wants the roots printed or the scan run.
    answered = reading(sys.argv[1:], option_value)
    if answered is not None:
        return answered

    # Structure fails a commit. Prose is reported and does not, because the prose backlog predates
    # this check and a hook nobody can satisfy is a hook somebody turns off. Pass --strict to fail
    # on everything, the setting a cleanup pass wants.
    #
    # PROSE NEVER FAILS A BUILD, IN EITHER TIER, IN ANY REPOSITORY, ORIOR INCLUDED. Both
    # standards state it in the same sentence that names this tool, verbatim and word for word:
    # "A hit is a prose finding. It never fails a build, because a person has to decide each site: a
    # proper name, a quoted title, or a term of art is left located and reported as a false
    # positive." That is code-documentation:145, and code-comments:208 says it again.
    #
    # So --strict is a cleanup-pass mode and is never a pre-commit setting anywhere. A tier does not
    # change this: TIER A carries more authority than TIER B and still only reports.
    strict = "--strict" in sys.argv
    ratchet = option_value("--ratchet")
    ratchet_out = option_value("--ratchet-write")
    staged = "--staged" in sys.argv
    counts = {}
    # --fix RUNS THE POLICY AND WRITES NOTHING. The rewriting half is deliberately not implemented;
    # read the note above FIX_TIERS for why the only tier it could ever reach is the alphabet one.
    # The gate is here first so whoever writes the other half has to come through fix_error and
    # cannot skip the manifest, verbatim, generated and normative-keyword errors already tested
    # beside it. Never a pre-commit setting in any repository, in either mode.
    planning = "--fix" in sys.argv
    # Two readings that ride along with the scan, because both want what the scan already computed.
    # --show needs the findings beside the lines they sit on, and --slack needs every file's count.
    showing = option_value("--show")
    showing = re.compile(showing) if showing else None
    trail = int(option_value("--show-after") or 0)
    slack = "--slack" in sys.argv
    where_given = [one for one in sys.argv[1:] if not one.startswith("-")]
    if ratchet and where_given:
        print("  --ratchet holds the whole repository to its ceilings and does not take roots.")
        return 4

    # --changed reads only what a change touched and reports only the findings sitting on the lines
    # it added. It sets the roots itself, so it cannot be combined with roots given by hand.
    added = None
    if "--changed" in sys.argv:
        if where_given:
            print("  --changed takes the roots from the diff and does not accept them by hand.")
            return 4
        added = changed_scope(sys.argv[1:], option_value)
        if added is None:
            print("  git could not name what changed. Nothing was checked, so nothing passed.")
            return 2
        if not added:
            print("  no changed lines. No prose was checked.")
            return 0
        where_given = changed_paths(added)
    # Every place this project retains prose, since a README beside the code is read the same way a
    # page under docs is. Checking only docs left the twelve engine and example READMEs unchecked,
    # and one of them was carrying a paragraph sitting inside a table.
    # A named root is taken as given, then tried against the repository. A hook runs from wherever
    # git puts it, and `docs` meaning nothing from there is how this came to check zero files.
    roots = []
    for one in where_given or DEFAULT_ROOTS:
        if os.path.exists(one):
            roots.append(one)
            continue
        beside = os.path.join(REPOSITORY, one)
        roots.append(beside if os.path.exists(beside) else one)

    # THE ROOTS GO FIRST, BEFORE ANY FINDING. A reader has to know the scope before the count, since
    # "0 findings" over two configured roots and "0 findings" over a whole tree are the same three
    # characters and mean opposite things.
    print(
        "  roots configured: %d, %s"
        % (
            len(roots),
            (
                "given on the command line"
                if where_given
                else "this tool's own defaults, inside this repository only"
            ),
        )
    )
    for one in roots:
        print(
            "    %s%s"
            % (one.replace("\\", "/"), "" if os.path.exists(one) else "   NOT FOUND")
        )

    for top, revision, reach, dirty in refs_for(roots):
        print(
            "  measured at %s %s, %s%s"
            % (
                os.path.basename(top),
                revision,
                reach,
                ", working tree has uncommitted changes" if dirty else "",
            )
        )

    ledger = Ledger()
    breaking = 0
    prose = 0
    checked = 0
    errors = []
    allowed = []
    # What --changed reports, counted apart from what the run found. The ratchet reads every finding
    # in a touched file and the status reads only the ones the change wrote, and the two have to be
    # separate numbers or one of them is wrong. Retained in two columns for the same reason the run's
    # own counts are: prose never fails anything, on a changed line as much as anywhere else.
    on_new_breaking = 0
    on_new_prose = 0

    # Under --staged the read is scoped to the staged files, since a pre-commit hook holds what the
    # commit carries and reading the rest of the tree is the whole cost it adds to a commit. The
    # ratchet already held only the staged files; now the walk reads only them too. --changed sets
    # its own roots from the diff and retains them.
    scan_roots = staged_under(roots) if (staged and added is None) else roots
    for path in sorted(walk_markdown(scan_roots, ledger)):
        with open(path, encoding="utf-8", errors="replace") as handle:
            lines = handle.read().splitlines()
        checked += 1
        said = prose_only(path, lines, ledger)

        # relpath raises across Windows drives. A file on another drive lies outside the repository,
        # and no ratchet entry names it by a relative path.
        try:
            name = os.path.relpath(path, REPOSITORY).replace("\\", "/")
        except ValueError:
            name = path

        # A generated region is reported and attributed, never skipped. A structural finding sits
        # inside one as readily as anywhere else, and a rule that skipped marked regions reports a
        # tree clean on the strength of a marker. The marker carries the generator, and the
        # attribution is read from the document, so it cannot go stale.
        regions = {}
        if path.endswith(".md"):
            regions, unclosed = generated_regions(lines)
            for at, complaint in unclosed:
                print("  BREAK %s:%d: %s" % (path.replace("\\", "/"), at, complaint))
                breaking += 1

        # A reader sees these as a broken page. They stop a commit. Tables and links exist only
        # in markdown; an em dash is wrong in a comment too.
        structural = em_dashes(said)
        if path.endswith(".md"):
            structural += empty_tables(lines) + dead_links(path, lines)
        # Markdown left in a .tex builds clean and reaches the reader as punctuation. That is the
        # same failure an em dash is and belongs in the same column.
        if path.endswith(".tex"):
            structural += markdown_leftovers(said)
        # These read wrong and render fine. marker_edges reads the raw lines, since quieted() has
        # already blanked the content the tell is measured on by the time prose_only returns.
        # comments says whether this file's prose sits in comments, the scope code-comments
        # section 200 gives its three outright tokens. A .md is a page and a .tex is prose all
        # the way down. Neither is a comment; everything else here is read for its comments.
        # smart_quotes reads the same blanked lines the ban table does. printed_hits reads the parse
        # tree instead, because the text a tool prints is the one type of prose prose_only blanks.
        # ste_findings reads the comments against ASD-STE100, the standard for every comment. A page
        # has its own ratchet under `docs ste`, and a .tex is not a comment.
        comments = not path.endswith((".md", ".tex"))
        wording = (
            banned_tokens(
                said,
                quotations=path.endswith(".md"),
                comments=comments,
                path=path,
                ledger=ledger,
                regions=regions,
            )
            + marker_edges(lines)
            + smart_quotes(said, path=path, ledger=ledger)
            + printed_hits(path, lines)
            + (ste_findings(said) if comments else [])
        )

        for at, what in sorted(structural):
            if added is not None and not on_changed(name, added, at):
                continue
            on_new_breaking += 1
            print(
                "  BREAK %s:%d: %s"
                % (path.replace("\\", "/"), at, attributed(what, at, regions))
            )
        for at, what in sorted(wording):
            if added is not None and not on_changed(name, added, at):
                continue
            on_new_prose += 1
            print("  prose %s:%d: %s" % (path.replace("\\", "/"), at, what))

        if showing is not None:
            show_lines(path, lines, wording, showing, trail)

        if planning:
            fix_plan(path, lines, said, regions, errors, allowed)

        breaking += len(structural)
        prose += len(wording)
        counts[name] = len(wording)

    print("  %d file(s) checked, %d breaking, %d prose" % (checked, breaking, prose))
    if added is not None:
        print(
            "  on the %d line(s) this change added across %d file(s): %d breaking, %d prose. The "
            "rest were already in the files it touched."
            % (
                sum(len(one) for one in added.values()),
                len(added),
                on_new_breaking,
                on_new_prose,
            )
        )

    risen = []
    if ratchet_out:
        ratchet_write(ratchet_out, counts)
        print("  ratchet written: %d file(s) carry prose findings" % sum(1 for n in counts.values() if n))

    if ratchet:
        ceilings = ratchet_read(ratchet)
        if ceilings is None:
            print("  no ratchet at %s. Nothing to compare against, and that is never a pass." % ratchet)
            print("  write one with: python %s --ratchet-write=%s" % (sys.argv[0], ratchet))
            return 4
        # Only what this commit carries is held to its ceiling. Other files in the working tree are
        # other people's work in progress, and stopping a commit over them stops the wrong person.
        scope = staged_paths() if staged else set(counts)
        lowered = 0
        for name in sorted(scope):
            if name not in counts:
                continue
            ceiling = ceilings.get(name, 0)
            if counts[name] > ceiling:
                risen.append((name, counts[name], ceiling))
            elif counts[name] < ceiling:
                ceilings[name] = counts[name]
                lowered += 1
        if lowered:
            ratchet_write(ratchet, ceilings)
        print(
            "  ratchet: %d file(s) retained at their ceiling, %d lowered, %d risen"
            % (len([one for one in scope if one in counts]), lowered, len(risen))
        )
        for name, count, ceiling in risen:
            print("  RISEN %s: %d prose finding(s), ceiling %d" % (name, count, ceiling))

    if slack:
        where = ratchet or DEFAULT_RATCHET
        ceilings = ratchet_read(where)
        if ceilings is None:
            print("  no ratchet at %s, so there is no slack to measure." % where)
            return 4
        show_slack(ratchet_slack(ceilings, counts), where)

    # What was excluded and why. Printed even when nothing was, because "excluded: 0" and a silence
    # are the same two states, and a reader has to be able to tell them apart.
    print("  excluded: %d" % ledger.total())
    for line in ledger.report():
        print(line)

    if planning:
        print(
            "  --fix is a plan only. Nothing was written, and the rewriting half of it is "
            "deliberately not implemented."
        )
        print("    would rewrite: %d" % len(allowed))
        for where in allowed[:20]:
            print("      %s" % where)
        if len(allowed) > 20:
            print("      ... and %d more" % (len(allowed) - 20))
        print("    ERROR: %d" % len(errors))
        for where in errors[:20]:
            print("      %s" % where)
        if len(errors) > 20:
            print("      ... and %d more" % (len(errors) - 20))
        for one in manifests_covering(roots):
            print(
                "    this tree carries a signed manifest, %s. After anything writes here: %s"
                % (os.path.basename(one), reconcile_command(one))
            )

    # The one line that retains this report from claiming more than it measured. Four independent
    # reasons a finding survives a gate, each sufficient alone: the hook is not installed, the
    # declared roots exclude the file, no rule matches it, and the extension cannot be opened. This
    # tool answers for the last two. The first two are per-repository decisions and belong to
    # whoever owns that repository.
    print(
        "  this run read %d file(s) under %d root(s) and nothing outside them. Whether a hook is "
        "installed and which roots a repository declares are that repository's decisions, not "
        "this tool's: a gate that is correct, installed nowhere, and scoped to two paths still "
        "catches nothing." % (checked, len(roots))
    )

    # Checking nothing is not passing. A run that reads no files and reports success is the failure
    # a commit hook cannot see, and it is how a wrong path goes unnoticed for as long as it takes
    # somebody to wonder why the count never moves.
    if checked == 0:
        # Under --staged, no file read means the commit stages nothing under the roots this gate
        # governs. There is nothing to hold, and that is a pass. Outside --staged the roots are the
        # whole scope, and reading nothing is the misconfiguration the next line names.
        if staged and added is None:
            print("  no staged file falls under the checked roots. Nothing to hold.")
            return 0
        print("  no files were read. Nothing was checked. Nothing passed.")
        for one in roots:
            print(
                "    %s%s" % (one, "" if os.path.exists(one) else "   does not exist")
            )
        return 2

    # One for an error and two for the sentinel, never a count. A count makes a run with exactly
    # two breaking findings indistinguishable from a run that read nothing, and the commit hook
    # tests for 2 by name: it would print "the docs check read nothing" over a real pair of em
    # dashes.
    #
    # A count is the wrong shape for an exit status besides. They wrap at 256, so 256 findings
    # exit 0.
    #
    # Under --changed the status answers for what the change wrote and not for what it inherited. A
    # commit that touches a file carrying an older em dash did not write that em dash, and failing
    # on it makes the flag useless for the one job it has.
    if added is not None:
        return 1 if (on_new_breaking or (strict and on_new_prose)) else 0
    if breaking or (strict and prose):
        return 1
    if risen:
        return 3
    return 0
