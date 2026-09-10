#!/usr/bin/env python3
# repotools-stamp: lib/repotools/findings.py bb1158878b9e2ca0
# repo_tools - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
"""One report shape for every check in the toolkit, and one rule about exit codes.

A check yields findings and returns a Report. The Report decides the exit code, so a caller never
writes `sys.exit(1 if bad else 0)` and never gets that backwards.

    from repotools import findings

    report = findings.Report("docs_check")
    report.breaking(path, line, "empty table")
    report.note(path, line, "banned token: rather")
    raise SystemExit(report.done())

TWO SEVERITIES, AND ONLY ONE STOPS A COMMIT

`breaking` is something a reader meets as a broken page or a tool meets as a crash: an empty table,
a link to a file that is gone, a stamp that does not match its file. These refuse.

`note` is something that reads wrong and works fine. These print and let the commit through. A gate
that refused a commit over 169 instances of one banned word gets turned off inside a day, and the
broken tables then go through with it. `--strict` promotes notes to breaking, which is the setting a
cleanup pass wants and the setting a hook does not.

READING NOTHING IS A THIRD OUTCOME

A Report that saw zero files exits 2, separately from both other outcomes. A check that reads no
files and prints success is a failure a hook cannot see. A moved directory then goes unnoticed for
as long as it takes somebody to wonder why the count never moves.
"""

import sys

EXIT_OK = 0
EXIT_BREAKING = 1
EXIT_READ_NOTHING = 2


class Report:
    """Findings from one check, with the counts a caller prints and the code it exits on."""

    def __init__(self, name, strict=False):
        self.name = name
        self.strict = strict
        self.breaking_findings = []
        self.notes = []
        self.checked = 0

    def saw(self, count=1):
        """Record that `count` files were read. Reporting the count is what turns a zero into news."""
        self.checked += count

    def breaking(self, path, line, what):
        self.breaking_findings.append((str(path).replace("\\", "/"), int(line), what))

    def note(self, path, line, what):
        self.notes.append((str(path).replace("\\", "/"), int(line), what))

    def print_findings(self, stream=None):
        stream = stream or sys.stdout
        for path, line, what in sorted(self.breaking_findings):
            print("  BREAK %s:%d: %s" % (path, line, what), file=stream)
        for path, line, what in sorted(self.notes):
            print("  note  %s:%d: %s" % (path, line, what), file=stream)

    def done(self, stream=None):
        """Print the findings and the count, and return the exit code.

        The count prints on every run, including a clean one. A number that never moves is the only
        signal a reader has that a root went missing.
        """
        stream = stream or sys.stdout
        self.print_findings(stream)
        print(
            "  %s: %d file(s) checked, %d breaking, %d note(s)"
            % (self.name, self.checked, len(self.breaking_findings), len(self.notes)),
            file=stream,
        )
        if self.checked == 0:
            print("  %s read no files. Nothing was checked, so nothing passed." % self.name, file=stream)
            return EXIT_READ_NOTHING
        if self.breaking_findings:
            return EXIT_BREAKING
        if self.strict and self.notes:
            return EXIT_BREAKING
        return EXIT_OK
