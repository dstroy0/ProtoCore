#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""gen_test_readme.py - refresh the generated sections of test/README.md from the test suite itself.

test/README.md keeps hand-written prose (the philosophy, mocking strategy, how-to, sanitizer notes)
and two GENERATED, marker-delimited sections that this script rewrites in place so they never drift:

  * test-environments - one row per native env scraped from test/test_matrix.json (feature flags, test
    suites, purpose). Regenerates when the matrix changes.
  * test-directory    - a collapsible directory of every suite and test case, scraped from the
    test/test_*/test_*.cpp files (objective comment + Unity assertions per test).

Everything outside the markers is left untouched. Run after adding an env or a test:

    python3 test/gen_test_readme.py            # rewrite the generated sections in place
    python3 test/gen_test_readme.py --check    # exit 1 if they are out of date (CI guard)
"""

import argparse
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
README = os.path.join(HERE, "README.md")
MATRIX = os.path.join(HERE, "test_matrix.json")

ENV_BEGIN = "<!-- BEGIN GENERATED test-environments (edit test/test_matrix.json, run test/gen_test_readme.py) -->"
ENV_END = "<!-- END GENERATED test-environments -->"
DIR_BEGIN = "<!-- BEGIN GENERATED test-directory (run test/gen_test_readme.py) -->"
DIR_END = "<!-- END GENERATED test-directory -->"


# ── env matrix (scraped from test_matrix.json) ────────────────────────────────


def _cell(text):
    return re.sub(r"\s+", " ", (text or "").replace("|", "\\|")).strip()


def _first_sentence(desc):
    desc = _cell(desc)
    m = re.match(r"(.+?[.!?])(\s|$)", desc)
    out = m.group(1) if m else desc
    return (out[:200] + "...") if len(out) > 203 else out


def build_env_table():
    matrix = json.load(open(MATRIX, encoding="utf-8"))
    envs = matrix.get("envs", matrix)
    rows = []
    for name in sorted(envs):
        e = envs[name]
        flags = e.get("flags", [])
        flag_txt = ", ".join(f"`{f.lstrip('-D')}`" for f in flags) if flags else "default"
        tests = ", ".join(f"`{t}`" for t in e.get("tests", [])) or "-"
        rows.append(f"| `{name}` | {flag_txt} | {tests} | {_first_sentence(e.get('desc', ''))} |")
    out = [
        f"The native test matrix has **{len(envs)} environments**, one per feature, generated from "
        "[test_matrix.json](test_matrix.json) into [platformio.ini](../platformio.ini) by "
        "[gen_test_envs.py](gen_test_envs.py). Each compiles a strict per-feature slice of `src/` with its "
        "own flags and runs that feature's suite in isolation, so \"this feature builds and tests on its "
        'own" stays guaranteed.\n',
        "| Environment | Feature flag(s) | Test suite(s) | Purpose |",
        "| :--- | :--- | :--- | :--- |",
    ]
    out.extend(rows)
    return "\n".join(out)


# ── per-test directory (scraped from the test_*.cpp files) ────────────────────


def clean_name(name):
    prefix = ""
    if name.startswith("stress_"):
        prefix, name = "Stress - ", name[7:]
    elif name.startswith("race_"):
        prefix, name = "Race - ", name[5:]
    elif name.startswith("test_"):
        name = name[5:]
    words = name.split("_")
    if words:
        words[0] = words[0].capitalize()
    return prefix + " ".join(words)


def extract_assertions(lines):
    out = []
    for line in lines:
        line = line.strip()
        if "TEST_ASSERT" in line:
            m = re.search(r"TEST_ASSERT_([A-Z_]+)\((.*)\)", line)
            out.append(f"Assert {m.group(1).replace('_', ' ').lower()} ({m.group(2)})" if m else line)
    return out


def parse_test_file(filepath):
    content = open(filepath, encoding="utf-8", errors="ignore").read()
    run_tests = re.findall(r"RUN_TEST\(([a-zA-Z0-9_]+)\)", content)
    if not run_tests:
        run_tests = [
            t
            for t in re.findall(r"void\s+([a-zA-Z0-9_]+)\s*\(", content)
            if t.startswith(("test_", "stress_", "race_"))
        ]
    seen = set()
    run_tests = [x for x in run_tests if not (x in seen or seen.add(x))]
    lines = content.splitlines()

    cases = []
    for fn in run_tests:
        idx0 = next((i for i, ln in enumerate(lines) if re.match(r"^\s*void\s+" + re.escape(fn) + r"\s*\(", ln)), -1)
        if idx0 == -1:
            continue
        braces = 0
        started = False
        body = []
        for ln in lines[idx0:]:
            body.append(ln)
            if "{" in ln:
                braces += ln.count("{")
                started = True
            if "}" in ln and started:
                braces -= ln.count("}")
                if braces <= 0:
                    break
        comment = ""
        for ln in body:
            t = ln.strip()
            if t.startswith("//"):
                c = re.sub(r"^//\s*", "", t)
                if not any(x in c.lower() for x in ("copyright", "spdx", "license")):
                    comment = c
                    break
        cases.append({"fn_name": fn, "description": comment or clean_name(fn), "assertions": extract_assertions(body)})
    return cases


def build_test_directory():
    suites = {}
    for fp in sorted(glob.glob(os.path.join(HERE, "test_*", "test_*.cpp"))):
        cases = parse_test_file(fp)
        if cases:
            suites[os.path.basename(os.path.dirname(fp))] = cases
    total = sum(len(v) for v in suites.values())
    md = [
        f"A thorough directory of all **{total} test cases** across **{len(suites)} suites**. Expand a suite "
        "to see its test cases, and a test case to see its objective and assertions.\n"
    ]
    for suite, tests in sorted(suites.items()):
        md.append("<details>")
        md.append(f"<summary><b>{suite} ({len(tests)} tests)</b></summary>\n")
        for t in tests:
            md.append('  <details style="margin-left: 20px;">')
            md.append(f"    <summary><b>{t['fn_name']}</b> &mdash; <i>{_cell(t['description'])}</i></summary>\n")
            md.append(f"    * **Objective**: {_cell(t['description'])}")
            if t["assertions"]:
                md.append("    * **Assertions**:")
                for a in t["assertions"]:
                    safe = a.replace("\\", "\\\\").replace("<", "&lt;").replace(">", "&gt;")
                    md.append(f"      * <code>{safe}</code>")
            md.append("  </details>\n")
        md.append("</details>\n")
    return "\n".join(md)


# ── marker splice ─────────────────────────────────────────────────────────────


def splice(text, begin, end, body, label):
    i = text.find(begin)
    j = text.find(end)
    if i == -1 or j == -1 or j < i:
        sys.exit(f"error: markers for '{label}' not found in test/README.md")
    return text[: i + len(begin)] + "\n\n" + body.rstrip() + "\n\n" + text[j:]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="exit 1 if the generated sections are stale")
    args = ap.parse_args()

    text = open(README, encoding="utf-8").read()
    new = splice(text, ENV_BEGIN, ENV_END, build_env_table(), "test-environments")
    new = splice(new, DIR_BEGIN, DIR_END, build_test_directory(), "test-directory")

    if args.check:
        if new != text:
            sys.exit("test/README.md is out of date; run: python3 test/gen_test_readme.py")
        print("test/README.md is up to date.")
        return
    open(README, "w", encoding="utf-8", newline="\n").write(new)
    print("regenerated test/README.md generated sections (test-environments, test-directory).")


if __name__ == "__main__":
    main()
