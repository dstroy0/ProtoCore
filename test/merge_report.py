#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""merge_report.py - overlay a partial TEST_REPORT.md onto the committed one.

An affected-only CI run tests just the envs the diff touched, so run_tests.sh emits a
report covering only those suites. This overlays that partial report onto the committed
full report: the touched (suite, env) rows and detail sections are replaced (new ones
appended), every other one is kept verbatim, and the header Generated / Result totals
are recomputed across the merged whole. The result is the up-to-date full report without
having rerun the ~200 untouched suites.

Both the summary rows and the detail-section headers carry the env, so a suite that runs
in several envs (e.g. test_websocket under native_app and native_ws_deflate) keys as
distinct (suite, env) entries and merges without colliding.

Usage:
    merge_report.py <committed.md> <partial.md> <out.md>
"""

import datetime
import re
import sys
from collections import OrderedDict

# The env column can be the bare `native` core-engine env as well as `native_<feature>`.
ROW_RE = re.compile(r"^\|\s*`(test_[^`]+)`\s*\|\s*`(native[^`]*)`\s*\|")
SEC_RE = re.compile(r"^##\s+(test_\S+)\s+-\s+(native\S*)\s+-\s")
CNT_RE = re.compile(r"(\d+)\s+passed(?:,\s+(\d+)\s+failed)?")
DUR_RE = re.compile(r"(\d+):(\d+):(\d+(?:\.\d+)?)\s*\|?\s*$")


def parse(text):
    """Split a report into head, summary_intro, rows{(suite,env):line}, mid, sections{(suite,env):text}."""
    lines = text.splitlines()
    i_sum = next((i for i, ln in enumerate(lines) if ln.strip() == "## Summary"), None)
    i_first_sec = next((i for i, ln in enumerate(lines) if SEC_RE.match(ln)), len(lines))

    rows = OrderedDict()
    row_idx = [i for i in range(i_sum or 0, i_first_sec) if ROW_RE.match(lines[i])]
    for i in row_idx:
        m = ROW_RE.match(lines[i])
        rows[(m.group(1), m.group(2))] = lines[i]
    first_row = row_idx[0] if row_idx else i_first_sec
    last_row = row_idx[-1] + 1 if row_idx else i_first_sec

    head = lines[:i_sum] if i_sum is not None else lines[:i_first_sec]
    summary_intro = lines[i_sum:first_row] if i_sum is not None else []
    mid = lines[last_row:i_first_sec]

    sections = OrderedDict()
    cur = None
    buf = []
    for ln in lines[i_first_sec:]:
        m = SEC_RE.match(ln)
        if m:
            if cur is not None:
                sections[cur] = "\n".join(buf).rstrip("\n")
            cur = (m.group(1), m.group(2))
            buf = [ln]
        else:
            buf.append(ln)
    if cur is not None:
        sections[cur] = "\n".join(buf).rstrip("\n")
    return head, summary_intro, rows, mid, sections


def dur_secs(row):
    m = DUR_RE.search(row.rstrip())
    if not m:
        return 0.0
    return int(m.group(1)) * 3600 + int(m.group(2)) * 60 + float(m.group(3))


def main():
    committed_p, partial_p, out_p = sys.argv[1], sys.argv[2], sys.argv[3]
    committed = open(committed_p, encoding="utf-8").read()
    partial = open(partial_p, encoding="utf-8").read()

    head, intro, rows, mid, sections = parse(committed)
    _, _, p_rows, _, p_sections = parse(partial)

    for key, row in p_rows.items():
        rows[key] = row  # replace in place, or append (OrderedDict keeps first-seen order)
    for key, sec in p_sections.items():
        sections[key] = sec

    # Recompute header totals across the merged whole.
    passed = failed = 0
    for sec in sections.values():
        m = CNT_RE.search(sec.splitlines()[0])
        if m:
            passed += int(m.group(1))
            failed += int(m.group(2)) if m.group(2) else 0
    total_secs = int(round(sum(dur_secs(r) for r in rows.values())))
    icon = "❌" if failed else "✅"
    result = f"{icon} {passed} passed"
    if failed:
        result += f", {failed} failed"
    result += f" - {total_secs}s"
    now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    out = []
    for ln in head:
        if ln.startswith("**Generated:**"):
            out.append(f"**Generated:** {now}")
        elif ln.startswith("**Result:**"):
            out.append(f"**Result:** {result}")
        else:
            out.append(ln)
    out += intro
    out += list(rows.values())
    out += mid
    for key in sections:
        out.append(sections[key])
        out.append("")
    open(out_p, "w", encoding="utf-8", newline="\n").write("\n".join(out).rstrip("\n") + "\n")
    print(f"merged {len(p_sections)} suite(s) into {out_p}: {len(sections)} suites, {passed} passed, {failed} failed")


if __name__ == "__main__":
    main()
