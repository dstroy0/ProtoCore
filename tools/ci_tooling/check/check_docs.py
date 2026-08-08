#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Doc staleness guard: fail when the prose cites something the tree no longer has.

Documentation rots faster than code when the tree moves quickly, and it rots SILENTLY -
nothing fails, the doc just starts lying. This checks the citations that can be
mechanically verified:

  1. relative links that resolve to nothing
  2. ``PC_ENABLE_*`` / ``PC_*`` flags that no longer exist in the config header
  3. ``pc_*`` functions cited in prose that no longer exist in src/
  4. ``src/`` paths cited inline (outside a link) that no longer exist
  5. ``native_*`` test envs cited that platformio.ini no longer defines

Deliberately NOT checked, because they produce false positives rather than findings:
Doxygen ``@ref`` targets (resolved by Doxygen against parsed source, not the filesystem),
C++ lambdas ``[](args)`` which are syntactically identical to a markdown link, and URLs.

Run: python -m tools.ci_tooling.check.check_docs [--all]
"""

import os
import re
import subprocess
import sys
from tools.ci_tooling.lib import doc_region as dr

ROOT = dr.repo_root(__file__)

LINK = re.compile(r"\[[^\]]*\]\(([^)]+)\)")
FLAG = re.compile(r"`(PC_[A-Z0-9_]+)`")
FUNC = re.compile(r"`(pc_[a-z0-9_]+)\(\)`")
SRCPATH = re.compile(r"`(src/[A-Za-z0-9_./-]+)`")
ENV = re.compile(r"`(native_[a-z0-9_]+)`")


def sh(*a):
    return subprocess.run(a, cwd=ROOT, capture_output=True, text=True).stdout


def read(p):
    try:
        return open(os.path.join(ROOT, p), encoding="utf-8", errors="replace").read()
    except OSError:
        return ""


def main() -> int:
    mds = [f for f in sh("git", "ls-files", "*.md").split() if f]

    # Every PC_ / pc_ token that exists anywhere in src/. Deliberately broader than
    # "#define": PC_OK and PC_OP_SEND are enum MEMBERS, and PC_ names also arrive via
    # namespacing structs of static constexpr. The question is "does this symbol exist",
    # not "is it a macro".
    # test/ and penetration_testing/ define their own PC_ symbols (PC_SSH_BENCH,
    # PC_SSH_TEST_HOST_KEY_DER); a doc citing those is not stale.
    src_blob = "".join(
        read(f)
        for f in sh(
            "git",
            "ls-files",
            "src/*.h",
            "src/*.c",
            "src/*.cpp",
            "test/*.h",
            "test/*.c",
            "test/*.cpp",
            "test/penetration_testing/*.h",
            "test/penetration_testing/*.c",
            "test/penetration_testing/*.cpp",
        ).split()
    )
    known_flags = set(re.findall(r"\b(PC_[A-Z0-9_]+)\b", src_blob))
    known_funcs = set(re.findall(r"\b(pc_[a-z0-9_]+)\s*\(", src_blob))

    # Symbol and env checks apply to docs written in the PRESENT tense - those describing
    # the library as it is now. Two other kinds legitimately name things absent from the
    # current tree, and checking them produces noise rather than findings:
    #
    #   forward - ROADMAP.md names PC_ENABLE_* flags for features not yet built.
    #   historical - BUGS.md and AUDIT.md record what happened. An entry citing a
    #                function since removed, or a test env since merged away, is accurate
    #                history; "fixing" it would falsify the record.
    #   illustrative - SYMBOLS.md documents the naming rules, so its examples are chosen to
    #                  SHOW a shape, not to name a real symbol. PC_HTTP_PARSER_H is introduced
    #                  as "a plausible name for ANOTHER library's guard"; making it resolve
    #                  would defeat the point it is making.
    #
    # Dead LINKS are still checked everywhere: a broken link is broken regardless of tense.
    NOT_PRESENT_TENSE = {
        "docs/ROADMAP.md",  # forward-looking
        "docs/BUGS.md",  # historical
        "docs/AUDIT.md",  # historical
        "docs/CHANGELOG.md",  # historical, generated
        "docs/DELIVERED.md",  # historical
        "docs/SYMBOLS.md",  # illustrative
        # historical: a sweep record's whole job is to say what a refactor renamed or deleted,
        # so it names the OLD symbol on purpose - "PC_ENABLE_VFS -> PC_ENABLE_MNT", "PC_POOL_SLOTS
        # used to be", "the rule that removed PC_LIT". Resolving those would erase the record.
        "SWEEP_NOTES.md",
    }

    known_envs = set(re.findall(r"^\[env:(native[A-Za-z0-9_]*)\]", read("platformio.ini"), re.M))

    bad = []
    for f in mds:
        text = read(f)
        d = os.path.dirname(f)

        for m in LINK.finditer(text):
            t = m.group(1).split("#")[0].strip()
            if not t or t.startswith(("http://", "https://", "mailto:", "#", "@")) or " " in t or "*" in t:
                continue  # url, doxygen @ref, or a C++ lambda
            if not os.path.exists(os.path.join(ROOT, os.path.normpath(os.path.join(d, t)))):
                bad.append((f, "dead link", t))

        if f not in NOT_PRESENT_TENSE:
            for m in FLAG.finditer(text):
                if m.group(1) not in known_flags:
                    bad.append((f, "unknown flag", m.group(1)))
            for m in FUNC.finditer(text):
                if m.group(1) not in known_funcs:
                    bad.append((f, "unknown function", m.group(1)))
            for m in ENV.finditer(text):
                if m.group(1) not in known_envs:
                    bad.append((f, "unknown test env", m.group(1)))

        for m in SRCPATH.finditer(text):
            p = m.group(1)
            # `src/main.cpp` in a bench/example README is generic prose about a project
            # skeleton, not a path into THIS repo. Only check paths with real depth.
            if p.count("/") < 2:
                continue
            if not os.path.exists(os.path.join(ROOT, p)):
                bad.append((f, "missing src path", p))

    if bad:
        print("check_docs: documentation cites things the tree does not have:\n", file=sys.stderr)
        by_file = {}
        for f, kind, what in bad:
            by_file.setdefault(f, []).append((kind, what))
        for f in sorted(by_file):
            print(f"  {f}", file=sys.stderr)
            for kind, what in sorted(set(by_file[f]))[:12]:
                print(f"      [{kind}] {what}", file=sys.stderr)
            if len(set(by_file[f])) > 12:
                print(f"      ... and {len(set(by_file[f])) - 12} more", file=sys.stderr)
        print(f"\n{len(bad)} stale citation(s) in {len(by_file)} file(s).", file=sys.stderr)
        return 1

    print(f"check_docs: OK - {len(mds)} markdown files, every checked citation resolves.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
