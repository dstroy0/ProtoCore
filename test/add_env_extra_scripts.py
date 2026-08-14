#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
add_env_extra_scripts.py - splice an extra_scripts entry into an env that already exists.

add_test_env.py writes an env once and refuses a second write, so an env created before the runner
generator existed has no way to gain "pre:test/gen_test_runners.py". This adds that one key as text,
inside the same lock, and re-parses to prove no other env moved.

Usage:
    python test/add_env_extra_scripts.py NAME [NAME ...] --script pre:test/gen_test_runners.py
"""

import argparse
import json
import re
import sys

from tools import findroot

sys.path.insert(0, findroot.root())
from test.add_test_env import LOCK, TABLE, lock_acquire, lock_release  # noqa: E402


def env_span(text, name):
    """Byte range of the env's object, from its opening brace to its matching close."""
    m = re.search(r'^([ \t]*)"%s"\s*:\s*\{' % re.escape(name), text, re.M)
    if not m:
        raise KeyError(name)
    depth = 0
    i = text.index("{", m.start())
    start = i
    while i < len(text):
        c = text[i]
        if c == '"':
            i += 1
            while i < len(text) and text[i] != '"':
                i += 2 if text[i] == "\\" else 1
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return m.group(1), start, i
        i += 1
    raise KeyError(name)


def add_one(text, name, script):
    pad, _, close = env_span(text, name)
    block = ',\n%s "extra_scripts": [\n%s  "%s"\n%s ]\n%s' % (pad, pad, script, pad, pad)
    tail = text.rfind("\n", 0, close)  # the newline before the closing brace
    return text[:tail] + block + text[close:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="+")
    ap.add_argument("--script", default="pre:test/gen_test_runners.py")
    a = ap.parse_args()

    if not lock_acquire():
        print("could not take", LOCK)
        return 1
    try:
        with open(TABLE, "r", encoding="utf-8") as fh:
            text = fh.read()
        before = json.loads(text)["envs"]
        for name in a.names:
            if name not in before:
                print("env not found:", name)
                return 1
            if a.script in before[name].get("extra_scripts", []):
                print("already present:", name)
                continue
            text = add_one(text, name, a.script)
        after = json.loads(text)["envs"]
        for k, v in before.items():
            want = dict(v)
            if k in a.names:
                want["extra_scripts"] = list(v.get("extra_scripts", [])) + [a.script]
            if after[k] != want:
                print("collateral change in", k)
                return 1
        with open(TABLE, "w", encoding="utf-8", newline="") as fh:
            fh.write(text)
        print("patched:", " ".join(a.names))
        return 0
    finally:
        lock_release()


if __name__ == "__main__":
    sys.exit(main())
