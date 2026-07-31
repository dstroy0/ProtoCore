#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Bulk-triage the SonarCloud code smells that conflict with this library's deliberate design
# (zero-heap, no-STL, terse C++11 - see docs/SONARQUBE.md), plus a few provable false positives.
#
# The free/public SonarCloud plan cannot assign a custom quality profile to the project, so
# per-issue marking is the only way to keep the board down to genuine reviewer signals. New code
# re-flags the same style rules, so re-run this after a scan.
#
# The genuine complexity / review rules are intentionally NOT touched, so they stay visible:
#   S134 (deep nesting), S3776 (cognitive complexity), S107 (too many params),
#   S5813 (bounds when computing string length). Fix real BUG/VULNERABILITY findings at source.
#
# Usage: SONAR_TOKEN=<token> python ci_tooling/sonar/accept_style_conflicts.py [--dry-run]

import json
import os
import sys
import urllib.parse
import urllib.request

PROJECT = "dstroy0_ProtoCore"
BASE = "https://sonarcloud.io"

# Style rules that contradict a deliberate design choice -> transition "accept" (won't fix).
ACCEPT_RULES = {
    "cpp:S5028": "macros define constants (the whole compile-time feature/config system is #define, #if-gated)",
    "cpp:S5350": "const-qualify local ptr/ref",
    "cpp:S5827": "use auto to avoid repeating types",
    "cpp:S5817": "const-qualify non-mutating member fn",
    "cpp:S995": "const-qualify ptr/ref params",
    "cpp:S1905": "redundant cast (kept: documents a wrap-safe/narrowing conversion)",
    "cpp:S5566": "prefer STL algorithms / range-for",
    "cpp:S3230": "prefer in-class member initializer (members are initialized in the ctor list)",
    "cpp:S3628": "prefer raw string literals",
    "cpp:S924": "more than one break/goto per loop (protocol parsers)",
    "cpp:S886": "for-statement expressions (kept: bounded scan idiom)",
    "cpp:S5008": "void* (zero-heap generic pools / callback context)",
    "cpp:S1820": "too many struct fields (owned per-subsystem Ctx)",
    "cpp:S1066": "mergeable if (kept: explicit bound check reads clearer)",
    "cpp:S3642": "prefer scoped enumerations",
    "cpp:S1481": "unused variable (kept: documents a hardware register-map address)",
    "cpp:S954": "#include not at top (kept: co-located with the section that uses it)",
    "cpp:S3358": "nested conditional operator (kept: a clean 3-way route-match dispatch)",
}
ACCEPT_COMMENT = (
    "By design: conflicts with the library's deliberate zero-heap, no-STL, terse C++ style "
    "(see docs/SONARQUBE.md). Reviewed; keeping as-is."
)

# Rules whose flagged sites are provable non-issues -> transition "falsepositive".
FP_RULES = {
    "cpp:S5816": "strncpy is followed at every site by an explicit NUL terminator (dst[N-1]=0)",
    "cpp:S125": "protocol field-annotation comments (wire-format markers), not commented-out code",
    "cpp:S1103": 'the "/*" is inside the doc example "/api/*", a path pattern, not a nested comment',
}
FP_COMMENT = (
    "False positive: the flagged construct is provably safe/correct at every site "
    "(see the per-rule note in ci_tooling/sonar/accept_style_conflicts.py)."
)


def api(path, params, token, post=False):
    url = BASE + path
    data = urllib.parse.urlencode(params).encode()
    req = urllib.request.Request(url + ("" if post else "?" + data.decode()), data=data if post else None)
    req.add_header("Authorization", "Bearer " + token)
    with urllib.request.urlopen(req) as r:
        return json.load(r)


def open_keys_for_rules(rules, token):
    keys = {}
    for rule in rules:
        page, out = 1, []
        while True:
            d = api(
                "/api/issues/search",
                {"componentKeys": PROJECT, "resolved": "false", "rules": rule, "ps": 500, "p": page},
                token,
            )
            out += [i["key"] for i in d["issues"]]
            if page * 500 >= d["total"] or not d["issues"]:
                break
            page += 1
        if out:
            keys[rule] = out
    return keys


def transition(keys, do, comment, token, dry):
    for i in range(0, len(keys), 400):
        chunk = keys[i : i + 400]
        if dry:
            print(f"    [dry-run] would {do} {len(chunk)} issue(s)")
            continue
        api(
            "/api/issues/bulk_change",
            {"issues": ",".join(chunk), "do_transition": do, "comment": comment},
            token,
            post=True,
        )
        print(f"    {do}: {len(chunk)} issue(s)")


def main():
    dry = "--dry-run" in sys.argv
    token = os.environ.get("SONAR_TOKEN", "")
    if not token:
        sys.exit("SONAR_TOKEN not set")

    for label, rules, do, comment in (
        ("False positives", FP_RULES, "falsepositive", FP_COMMENT),
        ("Design-conflict style smells", ACCEPT_RULES, "accept", ACCEPT_COMMENT),
    ):
        print(label + ":")
        groups = open_keys_for_rules(rules, token)
        total = 0
        for rule, ks in sorted(groups.items()):
            print(f"  {rule} ({rules[rule]}): {len(ks)}")
            transition(ks, do, comment, token, dry)
            total += len(ks)
        print(f"  -> {total} issue(s) in this group\n")


if __name__ == "__main__":
    main()
