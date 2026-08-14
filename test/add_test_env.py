#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
add_test_env.py - splice one env into test/test_matrix.json without reformatting the file.

Why not json.dump: a load/dump round trip rewrites all 250 KB of the table (12 KB of whitespace
churn measured), so every env addition would land as a whole-file diff. This inserts the new block
as text after an anchor env, then re-parses to prove nothing else moved.

Concurrent callers are serialized on a lock beside the table, and the read happens inside the lock,
so N writers each see the envs the others already added instead of overwriting them.

Usage:
    python test/add_test_env.py NAME --after ANCHOR --tests DIR [options]

    --clone ENV        take flags and src from ENV (then --src adds to them)
    --src PATH ...     build_src_filter entries, as repo-relative .c paths
    --flags FLAG ...   extra -D flags
    --extra-scripts S  PlatformIO extra_scripts entries, e.g. pre:test/gen_test_runners.py
    --desc TEXT        the comment emitted above the env
    --only             src is exactly what --src names, prefixed with -<*>

Writes nothing and exits 1 if the env exists, the anchor is missing, or any other env would change.
Run test/gen_test_envs.py afterwards to rebuild platformio.ini.
"""

import argparse
import errno
import json
import os
import re
import sys
import time

from tools import findroot

TABLE = findroot.at("test", "test_matrix.json")
LOCK = str(TABLE) + ".lock"

LOCK_TIMEOUT_S = 120.0  # a writer that cannot get in by then reports rather than racing
LOCK_STALE_S = 300.0    # a lock older than this belonged to a run that died
LOCK_POLL_S = 0.05


def lock_acquire():
    """Take the table's lock, or report why not. O_EXCL is the atomic part on both platforms."""
    deadline = time.time() + LOCK_TIMEOUT_S
    while True:
        try:
            fd = os.open(LOCK, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            os.write(fd, str(os.getpid()).encode())
            os.close(fd)
            return True
        except OSError as e:
            if e.errno != errno.EEXIST:
                raise
        try:
            if time.time() - os.path.getmtime(LOCK) > LOCK_STALE_S:
                os.unlink(LOCK)  # the holder is gone; take it on the next pass
                continue
        except OSError:
            continue  # it vanished between the test and the stat: retry
        if time.time() > deadline:
            return False
        time.sleep(LOCK_POLL_S)


def lock_release():
    try:
        os.unlink(LOCK)
    except OSError:
        pass


def mirror_dir(src_path):
    """The test directory that mirrors a src module: src/a/b/c.c -> unit/src/a/b."""
    return "unit/src/" + os.path.dirname(src_path).replace("\\", "/")


def resolve_tests(a):
    """Derive the suite path from the module under test, or check the given one mirrors it.

    The suite lives where its counterpart lives in src, so a reader finds the tests for a module
    at the same path under test/unit. The leaf may name the suite (test_ssh_aesgcm for
    crypto/aead/aesgcm.c); the directory above it may not drift.
    """
    if not a.src:
        return a.tests, None
    want = mirror_dir(a.src[0])
    if not a.tests:
        stem = os.path.basename(a.src[0])[:-2]  # drop the .c
        return ["%s/test_%s" % (want, stem)], None
    for t in a.tests:
        got = t.rstrip("/").rsplit("/", 1)[0]
        if got != want:
            return None, ("suite %s does not mirror %s\n"
                          "  the module is src/%s, so the suite belongs in test/%s/<test_name>"
                          % (t, a.src[0], a.src[0], want))
    return a.tests, None


def splice(text, anchor, name, entry):
    """Insert entry as text directly after the anchor env's closing brace.

    The anchor is matched at whatever indentation the table happens to carry, and the new block is
    re-indented to sit at the same depth, so the insert does not depend on how the file was last
    written.

    The brace scan steps over string literals. A desc is free text and several carry a lone brace
    ("'[' vs '{'"), which a counter that reads every character would take for structure: the depth
    would close on the envs object instead of the env, and the block would land beside "envs"
    rather than inside it.
    """
    m = re.search(r'^([ \t]*)"%s"\s*:\s*\{' % re.escape(anchor), text, re.M)
    if not m:
        raise KeyError(anchor)
    pad = m.group(1)
    depth = 0
    i = text.index("{", m.start())
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
                break
        i += 1
    block = json.dumps({name: entry}, indent=2)[1:-1].rstrip()
    block = "\n".join(pad + l[2:] if l.startswith("  ") else pad + l.strip() for l in block.split("\n"))
    return text[: i + 1] + ",\n" + block + text[i + 1 :]


def add_one(a):
    """The read-modify-write itself. Runs with the lock held."""
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

    tests, why = resolve_tests(a)
    if why:
        print(why)
        return 1

    src = list(envs[a.clone]["src"]) if a.clone else (["-<*>"] if a.only else [])
    flags = list(envs[a.clone].get("flags", [])) if a.clone else []
    src += ["+<%s>" % p for p in a.src if "+<%s>" % p not in src]
    flags += [f for f in a.flags if f not in flags]

    entry = {"desc": a.desc, "flags": flags, "src": src, "tests": list(tests)}
    if a.extra_scripts:
        entry["extra_scripts"] = list(a.extra_scripts)
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("name")
    ap.add_argument("--after", required=True, help="env to insert after")
    ap.add_argument("--tests", nargs="+", default=[],
                    help="test_filter entries; derived from the first --src when omitted")
    ap.add_argument("--clone", help="env whose flags and src to start from")
    ap.add_argument("--src", nargs="*", default=[], help="repo-relative .c paths to build")
    ap.add_argument("--flags", nargs="*", default=[])
    ap.add_argument("--extra-scripts", nargs="*", default=[], dest="extra_scripts")
    ap.add_argument("--desc", default="")
    ap.add_argument("--only", action="store_true", help="build only --src, nothing else")
    a = ap.parse_args()

    if not lock_acquire():
        print("could not take %s within %.0fs - another writer is holding it" % (LOCK, LOCK_TIMEOUT_S))
        return 1
    try:
        return add_one(a)
    finally:
        lock_release()


if __name__ == "__main__":
    sys.exit(main())
