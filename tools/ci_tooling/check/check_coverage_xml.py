#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Validate test/coverage.xml against the SonarQube generic test coverage format.

The format is specified at
https://docs.sonarsource.com/sonarqube-server/analyzing-source-code/test-coverage/generic-test-data.md
(and the sonarqube-cloud page of the same name), retrieved 2026-08-14:

  root      <coverage version="1">
  file      mandatory  path            xs:string, absolute or relative to the module root
  line      mandatory  lineNumber      xs:positiveInteger
            mandatory  covered         xs:boolean
            optional   branchesToCover xs:nonNegativeInteger
            optional   coveredBranches xs:nonNegativeInteger

Checks 1-6 are that specification. Checks 7-10 are this project's own invariants: the
specification is silent on duplicate lines, on branch arithmetic, and on whether the paths
resolve, and a report can satisfy the schema while describing a tree that no longer exists.

Checks:
  1. root element is `coverage` and carries version="1"
  2. every element under the root is `file`, and every element under a file is `lineToCover`
  3. every file carries a non-empty `path`
  4. every lineToCover carries lineNumber and covered
  5. lineNumber is a positive integer; branchesToCover / coveredBranches, when present, are
     non-negative integers
  6. covered is an xs:boolean spelling (true / false / 1 / 0)
  7. no lineNumber appears twice within one file. gcovr --merge-mode-functions=separate emits a
     header's inline lines once per translation unit that included it, which is what
     dedupe_sonar_cov folds; a duplicate here means that pass did not run.
  8. coveredBranches <= branchesToCover
  9. every `path` resolves to a file in the tree
 10. no lineNumber exceeds its file's line count - the signature of a report measured before an
     edit shifted the lines it describes

Exit 0 when the report is clean, 1 otherwise. A missing report is not a failure: coverage is
produced by a full-matrix run, and not every checkout has one.
"""

import argparse
import os
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
DEFAULT = os.path.join("test", "coverage.xml")

BOOLS = ("true", "false", "1", "0")


def _int(value, allow_zero):
    """Parse an xs:(positive|nonNegative)Integer, or None when it is neither."""
    if value is None or not value.lstrip("+").isdigit():
        return None
    n = int(value)
    if n < 0 or (n == 0 and not allow_zero):
        return None
    return n


def line_count(path):
    """Lines of text in the file. A trailing newline ends the last line, it does not start a new
    one; counting it as one more leaves the check a line too lenient, which is exactly the width an
    edit that removes one line slips through."""
    with open(path, "rb") as fh:
        data = fh.read()
    if not data:
        return 0
    return data.count(b"\n") + (0 if data.endswith(b"\n") else 1)


def check(report):
    """Every violation in the report, as a list of strings."""
    bad = []
    try:
        root = ET.parse(report).getroot()
    except ET.ParseError as e:
        return ["not well-formed XML: %s" % e]

    if root.tag != "coverage":
        bad.append("root element is <%s>, must be <coverage>" % root.tag)
    if root.get("version") != "1":
        bad.append('root must carry version="1", found %r' % root.get("version"))

    for fe in root:
        if fe.tag != "file":
            bad.append("<%s> under the root, only <file> is allowed" % fe.tag)
            continue
        path = fe.get("path")
        if not path:
            bad.append("<file> without a path attribute")
            continue

        full = os.path.join(ROOT, *path.split("/"))
        exists = os.path.isfile(full)
        if not exists:
            bad.append("%s: path does not resolve to a file in the tree" % path)
        nlines = line_count(full) if exists else None

        seen = set()
        for le in fe:
            if le.tag != "lineToCover":
                bad.append("%s: <%s> under <file>, only <lineToCover> is allowed" % (path, le.tag))
                continue

            raw = le.get("lineNumber")
            num = _int(raw, allow_zero=False)
            if num is None:
                bad.append("%s: lineNumber %r is not a positive integer" % (path, raw))
                continue
            if num in seen:
                bad.append("%s: line %d is described more than once" % (path, num))
            seen.add(num)

            cov = le.get("covered")
            if cov is None:
                bad.append("%s:%d: covered is mandatory" % (path, num))
            elif cov not in BOOLS:
                bad.append("%s:%d: covered=%r is not a boolean" % (path, num, cov))

            btc = le.get("branchesToCover")
            cb = le.get("coveredBranches")
            nb = _int(btc, allow_zero=True) if btc is not None else None
            nc = _int(cb, allow_zero=True) if cb is not None else None
            if btc is not None and nb is None:
                bad.append("%s:%d: branchesToCover=%r is not a non-negative integer" % (path, num, btc))
            if cb is not None and nc is None:
                bad.append("%s:%d: coveredBranches=%r is not a non-negative integer" % (path, num, cb))
            if nb is not None and nc is not None and nc > nb:
                bad.append("%s:%d: coveredBranches %d exceeds branchesToCover %d" % (path, num, nc, nb))

            if nlines is not None and num > nlines:
                bad.append("%s: line %d cited, the file has %d - the report predates an edit"
                           % (path, num, nlines))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("report", nargs="?", default=DEFAULT, help="coverage report (default %s)" % DEFAULT)
    ap.add_argument("-q", "--quiet", action="store_true", help="print nothing when the report is clean")
    a = ap.parse_args()

    report = a.report if os.path.isabs(a.report) else os.path.join(ROOT, a.report)
    if not os.path.isfile(report):
        if not a.quiet:
            print("no coverage report at %s - nothing to check" % a.report)
        return 0

    bad = check(report)
    if bad:
        rel = os.path.relpath(report, ROOT).replace("\\", "/")
        print("%s: %d violation(s) of the SonarQube generic coverage format" % (rel, len(bad)),
              file=sys.stderr)
        for b in bad[:40]:
            print("  " + b, file=sys.stderr)
        if len(bad) > 40:
            print("  ... and %d more" % (len(bad) - 40), file=sys.stderr)
        return 1
    if not a.quiet:
        print("%s is a valid generic coverage report." % a.report)
    return 0


if __name__ == "__main__":
    sys.exit(main())
