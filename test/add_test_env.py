#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
add_test_env.py - splice one env into test/test_matrix.json without reformatting the file.

Why not json.dump: a load/dump round trip rewrites all 250 KB of the table (12 KB of whitespace
churn measured), so every env addition would land as a whole-file diff. This inserts the new block
as text after an anchor env, then re-parses to prove nothing else moved.

Usage:
    python test/add_test_env.py NAME --after ANCHOR --tests DIR [options]

    --clone ENV        take flags and src from ENV (then --src adds to them)
    --src PATH ...     build_src_filter entries, as repo-relative .c paths
    --flags FLAG ...   extra -D flags
    --desc TEXT        the comment emitted above the env
    --only             src is exactly what --src names, prefixed with -<*>

Writes nothing and exits 1 if the env exists, the anchor is missing, or any other env would change.
Run test/gen_test_envs.py afterwards to rebuild platformio.ini.
"""

import argparse
import json
import sys

from tools import findroot

TABLE = findroot.at("test", "test_matrix.json")


def splice(text, anchor, name, entry):
    """Insert entry as text directly after the anchor env's closing brace."""
    at = text.index('    "%s": {' % anchor)
    depth = 0
    i = text.index("{", at)
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                break
        i += 1
    block = json.dumps({name: entry}, indent=2)[1:-1].rstrip()
    block = "\n".join("  " + l if l.strip() else l for l in block.split("\n"))
    return text[: i + 1] + ",\n" + block + text[i + 1 :]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("name")
    ap.add_argument("--after", required=True, help="env to insert after")
    ap.add_argument("--tests", nargs="+", required=True, help="test_filter entries")
    ap.add_argument("--clone", help="env whose flags and src to start from")
    ap.add_argument("--src", nargs="*", default=[], help="repo-relative .c paths to build")
    ap.add_argument("--flags", nargs="*", default=[])
    ap.add_argument("--desc", default="")
    ap.add_argument("--only", action="store_true", help="build only --src, nothing else")
    a = ap.parse_args()

    with open(TABLE, "r", encoding="utf-8") as fh:
        text = fh.read()
    before = json.loads(text)
    envs = before["envs"]

    if a.name in envs:
        print("env already present:", a.name)
        return 1
    if a.after not in envs:
        print("anchor env not found:", a.after)
        return 1

    src = list(envs[a.clone]["src"]) if a.clone else (["-<*>"] if a.only else [])
    flags = list(envs[a.clone].get("flags", [])) if a.clone else []
    src += ["+<%s>" % p for p in a.src if "+<%s>" % p not in src]
    flags += [f for f in a.flags if f not in flags]

    entry = {"desc": a.desc, "flags": flags, "src": src, "tests": list(a.tests)}
    text = splice(text, a.after, a.name, entry)

    after = json.loads(text)
    if after["envs"][a.name] != entry:
        print("the spliced env did not round-trip")
        return 1
    for k in envs:
        if envs[k] != after["envs"][k]:
            print("collateral change in", k)
            return 1

    with open(TABLE, "w", encoding="utf-8", newline="") as fh:
        fh.write(text)
    print("added %s (%d src entries); run test/gen_test_envs.py" % (a.name, len(src)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
