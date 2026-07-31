#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""report_stable.py - print TEST_REPORT.md with its per-run timing blanked out.

The report embeds wall-clock noise that changes every run even when the tests are identical:
the `**Generated:**` timestamp, the `- <N>s` total in `**Result:**`, and the Duration column of
the summary table. That made the committed report differ on every CI run, so it was re-committed
each time. The commit step normalizes the freshly generated report and the committed one through
this filter; if they match, the timing-only regeneration is discarded and nothing is committed.

Usage:  report_stable.py <path>      (prints the normalized report to stdout)
"""

import re
import sys

GENERATED = re.compile(r"^\*\*Generated:\*\*.*$")
RESULT_SECS = re.compile(r"(\*\*Result:\*\*.*?)\s*-\s*\d+s\s*$")
# A summary row:  | `suite` | `env` | 17 | ✅ | 00:00:08.959 |   -> blank the last (duration) cell.
SUMMARY_ROW = re.compile(r"^(\|.*\|.*\|.*\|.*\|)\s*[0-9:.]+\s*\|\s*$")


def normalize(text):
    out = []
    for ln in text.splitlines():
        if GENERATED.match(ln):
            ln = "**Generated:**"
        else:
            ln = RESULT_SECS.sub(r"\1", ln)
            m = SUMMARY_ROW.match(ln)
            if m:
                ln = m.group(1) + " |"
        out.append(ln)
    return "\n".join(out) + "\n"


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    if len(sys.argv) != 2:
        sys.exit("usage: report_stable.py <path>")
    with open(sys.argv[1], encoding="utf-8") as fh:
        sys.stdout.write(normalize(fh.read()))


if __name__ == "__main__":
    main()
