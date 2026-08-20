#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
harness.py - the one entry point for the native test matrix.

`run` IS CMAKE AND CTEST. 0.06 s per test, and the build is incremental:

    harness.py run                  the whole matrix
    harness.py run native_telnet    one env, and every suite it runs

which is `tools/harness.py build cmake`, `cmake --build build/native -j -- -k`, then `ctest`. Those
three are the same thing spelled out, and are what to reach for when reading a build error:

    cmake --build build/native -j -- -k    `-k` is load bearing: without it make stops at the
                                           first failing target and the other 30 stay hidden

BUILD WHAT YOU CHANGED, NOT THE MATRIX. A whole-matrix build is 9556 objects and about twelve
minutes; a handful of targets is seconds. While a change is still being iterated on, name them:

    cmake --build build/native -j --target native_sha384_kat native_x509_verify -- -k 0
    ctest --test-dir build/native -R "^(native_sha384_kat|native_x509_verify)$"

The full sweep is the ACCEPTANCE GATE at the end of a change, run once - not the edit loop. Reach
for it when a shared header moved (protocore_config.h, buffer_sizing.h, feature_en_error.h) or when
the change is finished and needs proving.

READ THE MODULE BEFORE WRITING THE TEST, AND READ IT BLIND FIRST:

    python tools/harness.py view read blind  src/crypto/hash/sha256/sha256.h
    python tools/harness.py view read code   src/crypto/hash/sha256/sha256.h
    python tools/harness.py view read claims src/crypto/hash/sha256/sha256.h

In that order, and not the other one. `blind` strips every comment AND replaces every name this
project chose, so the code is read for what it DOES rather than for what it is called - a suite
written after reading a doc block asserts the doc block, and passes against code that does
something else. Assume nothing: a name is a claim, and it is the one claim nothing ever checks.
`claims` is LAST, and it pairs each comment with the line it sits above precisely so the prose is
met with its subject instead of being taken on trust.

A TEST THAT ASSERTS A REMOVED CONTRACT IS THE STALE HALF OF THE CHANGE. When a suite fails because
src/ no longer does something, read git log for the commit that stopped doing it before "fixing"
src/ back. The borrow null-checks are the worked example: 350 `if (!work)` branches were deleted in
one commit, and the cases still asserting a null borrow is refused segfaulted because of it. The
answer was to delete those cases, not to restore 350 dead branches.

`run --direct` is the old path - it compiles each env by itself and never reads the CMake, so one
env through it costs more than building and running the whole matrix. --debug, --coverage and
--report-out imply it because each reads one env's own build or its own Unity output.

A HWCAP IS NEVER REMOVED FROM AN ENV. An unresolved symbol that a test/core_setup/hal/<vendor>/
arm defines is the CAP BEING OFF: turn it on and build that arm. Never swap in the hal/portable
half, never drop the hal source, never drop the flag - the env goes green and the arm the hardware
runs stops being tested, and nothing reports it. A hw arm in src/ MOVES to core_setup and is
guarded there; it is not deleted.

THE HOST DRIVES EVERYTHING - BOTH ARMS. The halves are mutually exclusive, so one env cannot hold
both. Give each its own env, the same suite in both `tests` lists:

    harness.py env add native_x_sw --after native_x --clone native_x
    harness.py env update native_x_sw --drop-src <host arm> --src <portable arm> \
        --drop-flags=-DPROTOCORE_HAS_HW_X=1 --flags=-DPROTOCORE_HAS_HW_X=0

Audit before finishing. Every hit must be a path rename, not a removal:

    git diff test/test_matrix.json | grep "^-" | grep -iE "hal/|_hw|HW_"

Everything the harness can do is a subcommand here, so `harness.py -h` is the whole surface:

  env add       splice a new env into test/test_matrix.json
  env update    change an existing env's flags / src / tests / extra_scripts (adds; --drop-* removes)
  env addsrc    add the same deps to EVERY env that builds a given .c
  env addflag   add the same flags to EVERY env that builds a given .c (how a gate is wired up)
  env gen       regenerate the generated block of platformio.ini from the matrix
  env select    map changed files to the envs they affect
  env list      print the envs the matrix defines
  env deps      rebuild test/dep_graph.json from the compiler include closure

  run           build the matrix with CMake and run it with ctest; --direct compiles here instead
  bare          cross-compile the core, and boot it on the part under QEMU
  bench         the microbenchmark matrix
  runners gen   the bridge to Unity's generate_test_runner.rb: what makes a suite runnable
  keys ensure   put the SSH test host key in place
  readme gen    refresh the generated sections of test/README.md
  report merge  overlay a partial TEST_REPORT.md onto the committed one
  report stable print TEST_REPORT.md with per-run timing blanked out

WRITING A TEST is four steps, all of them here:

  1. write test/<mirror>/test_<name>/test_<name>.c - file-scope `void test_<case>(void)`
     functions plus setUp/tearDown. The mirror path is the module's: src/a/b/c.c is tested
     from test/unit/src/a/b/test_c/, and `env add` refuses a suite that sits anywhere else.
  2. harness.py runners gen <suite dir>   -> Unity's generator writes unity_runner.c beside it
  3. harness.py env add <env> --after <anchor> --src <the .c files it needs>
  4. harness.py run <env>

Step 2 is the one with teeth: Unity's generator registers `void test_*` and NOTHING else, and a
function it walks past is not an error - the suite just passes without ever running it. `runners
gen` names any such function rather than letting the silence stand. Step 3 also wires the
pre-build hook (extra_scripts: pre:test/gen_test_runners.py), so step 2 re-runs on every build.

A DRIVER FOR A PART ON A BUS is not tested by priming bytes. test/core_setup/hal/host/devices/ holds a
device model per part, written from its datasheet with the section beside every constant, and the
host bus consults it by address:

    #include "devices/ads1115_device.h"
    static protocore_ads1115_dev s_part;
    protocore_ads1115_dev_place(&s_part, PROTOCORE_ADS1115_I2C_ADDR); // resets it, puts it on the bus
    s_part.ain_uv[0] = 1000000;                                      // 1.000 V on AIN0

The driver then composes its own transfers and the model answers them with its own arithmetic, so
a read is the part's response to what was written rather than a queue filled in transfer order.
protocore_bus_host_preload still works for a part that has no model yet; a model, once attached,
takes precedence for that address. protocore_bus_host_detach_all() empties the table.

Every test activity starts here. `bare` and `bench` are separate matrices in their own files
(test/bare.py, test/performance_benching/bench.py) because they answer different questions, but
neither is invoked directly: this is the one entry point, so what a session learns about driving
the tests does not have to be re-derived. `harness.py bare help` and `harness.py bench help` are
their full surfaces.

NEVER WRITE test/test_matrix.json WITH json.dump. Not from a script, not once, not "just this
one field". A whole-file re-render is valid JSON and reformats all 400 envs, so the diff stops
naming what changed and a reviewer cannot see the one env that did. There is no case where that is
the answer, and no flag to json.dump that avoids it.

Mutate it through a subcommand. `env update` ADDS to flags / src / tests (its --drop-* twins
remove), `env addsrc` fans one set of deps out across every env that builds a file, `env add` and
`env remove` splice a whole env in or out. If the change you need has no subcommand, ADD ONE here
next to these - that is cheaper than the script, and the next session gets it too.

A script that must touch the table imports this module and calls splice_replace / splice_after /
splice_remove, then write_verified, which re-parses and refuses a write that moved any env it was
not asked to move. Those five are the whole supported surface.

The matrix is the single source of truth. Every mutation takes the table's lock, splices text so
the file is not reformatted, and re-parses to prove no other env moved.

THE BUILD IS GENERATED CMAKE. src/ carries one CMakeLists.txt per module directory and test/ carries
one target plus one ctest per env, both written by tools/harness.py (build modules / build cmake).
They say "GENERATED ... Do not edit" and mean it - change the generator.

    cmake -S test -B build/native && cmake --build build/native -j && ctest --test-dir build/native

`run` regenerates that CMake first, so adding an env is `env add` and then `run` - nothing else.
`run --direct` is the old compile-it-here path: it reads test_matrix.json and invokes gcc itself,
so it never goes through the CMake and never proves it builds.
"""

import argparse
import concurrent.futures
import errno
import fnmatch
import json
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from tools import findroot  # noqa: E402

ROOT = findroot.root()
INI = findroot.at("platformio.ini")
TABLE = findroot.at("test", "test_matrix.json")
DEP_GRAPH = findroot.at("test", "dep_graph.json")

BEGIN = "; >>> GENERATED TEST ENVS - do not edit below; edit test/test_matrix.json and run test/harness.py env gen >>>"
END = "; <<< END GENERATED TEST ENVS <<<"

LOCK_TIMEOUT_S = 120.0  # a writer that cannot get in by then reports rather than racing
LOCK_STALE_S = 300.0  # a lock older than this belonged to a run that died
LOCK_POLL_S = 0.05


# ---------------------------------------------------------------------------
# table locking
# ---------------------------------------------------------------------------


def lock_acquire(table):
    """Take the table's lock, or report why not. O_EXCL is the atomic part on both platforms."""
    lock = str(table) + ".lock"
    deadline = time.time() + LOCK_TIMEOUT_S
    while True:
        try:
            fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            os.write(fd, str(os.getpid()).encode())
            os.close(fd)
            return lock
        except OSError as e:
            if e.errno != errno.EEXIST:
                raise
        try:
            if time.time() - os.path.getmtime(lock) > LOCK_STALE_S:
                os.unlink(lock)  # the holder is gone; take it on the next pass
                continue
        except OSError:
            continue  # it vanished between the test and the stat: retry
        if time.time() > deadline:
            return None
        time.sleep(LOCK_POLL_S)


def lock_release(lock):
    try:
        os.unlink(lock)
    except OSError:
        pass


# ---------------------------------------------------------------------------
# text splicing: the table is edited as text so a write is a minimal diff
# ---------------------------------------------------------------------------


def env_span(text, name):
    """(pad, key_start, close) for an env: the indent it sits at, the index of its opening quote,
    and the index of its object's matching close brace.

    The brace scan steps over string literals. A desc is free text and several carry a lone brace
    ("'[' vs '{'"), which a counter that reads every character would take for structure.
    """
    m = re.search(r'^([ \t]*)"%s"\s*:\s*\{' % re.escape(name), text, re.M)
    if not m:
        raise KeyError(name)
    depth = 0
    start = m.start() + len(m.group(1))
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
                return m.group(1), start, i
        i += 1
    raise KeyError(name)


def reindent(block, pad):
    return "\n".join(pad + l[2:] if l.startswith("  ") else pad + l.strip() for l in block.split("\n"))


def unescape_flag(f):
    """Drop the `=` a caller prefixes so argparse does not read `-DNAME=1` as an option.

    The metavar says `=-DNAME=VALUE`, and the prefix is for argparse alone - stored, it becomes part
    of the flag and the compiler is handed `=-DNAME=1`, which it takes as a file name and the define
    never happens. Two envs were written that way before this stripped it, and both compiled and
    passed while silently building the wrong arm.
    """
    return f[1:] if f.startswith("=-") else f


def splice_after(text, anchor, name, entry):
    """Insert entry as text directly after the anchor env's closing brace."""
    pad, _, close = env_span(text, anchor)
    block = json.dumps({name: entry}, indent=2)[1:-1].rstrip()
    return text[: close + 1] + ",\n" + reindent(block, pad) + text[close + 1 :]


def splice_replace(text, name, entry):
    """Replace an env's whole `"name": {...}` in place, rendered at the indent it already sits at.

    The same render as splice_after, so an updated env and a new one are indented identically. The
    leading pad is dropped because the text kept ahead of key_start already carries it.
    """
    pad, key_start, close = env_span(text, name)
    block = json.dumps({name: entry}, indent=2)[1:-1].rstrip()
    return text[:key_start] + reindent(block, pad).lstrip() + text[close + 1 :]


def splice_remove(text, name):
    """Cut an env's whole `"name": {...}` out, taking the one comma that joined it to its neighbours.

    The comma sits after the close brace for every env but the last, where it sits before the key.
    Removing the wrong one, or neither, leaves the table unparseable.
    """
    _, key_start, close = env_span(text, name)
    end = close + 1
    tail = text[end:]
    lead = len(tail) - len(tail.lstrip(" \t\r\n"))
    if tail[lead : lead + 1] == ",":
        end += lead + 1  # not the last env: take the comma that follows it
        while end < len(text) and text[end] in " \t":
            end += 1
        if text[end : end + 1] == "\n":
            end += 1
        return text[:key_start] + text[end:]
    head = text[:key_start]
    cut = len(head.rstrip(" \t\r\n"))
    if head[cut - 1 : cut] == ",":
        cut -= 1  # the last env: take the comma that preceded it
    return text[:cut] + text[end:]


def read_table(path):
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()
    return text, json.loads(text)


def write_verified(path, text, before, changed, expect):
    """Write only if the reparsed table matches `expect` for `changed` and is untouched elsewhere."""
    after = json.loads(text)
    for name, want in expect.items():
        if after["envs"].get(name) != want:
            print("the spliced env did not round-trip:", name)
            return 1
    for k in before["envs"]:
        if k in changed:
            continue
        if before["envs"][k] != after["envs"].get(k):
            print("collateral change in", k)
            return 1
    with open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write(text)
    return 0


# ---------------------------------------------------------------------------
# env add / update
# ---------------------------------------------------------------------------


def module_path(src_path):
    """Where a src entry actually is. A first segment naming a directory beside src/ rather than
    inside it (test/core_setup/, include/) is already repo-relative; anything else is src-relative."""
    p = src_path.replace("\\", "/")
    head = p.split("/", 1)[0]
    if not os.path.isdir(os.path.join(ROOT, "src", head)) and os.path.isdir(os.path.join(ROOT, head)):
        return p
    return "src/" + p


def mirror_dir(src_path):
    """The test directory that mirrors a module: src/a/b/c.c -> unit/src/a/b."""
    return "unit/" + os.path.dirname(module_path(src_path))


def resolve_tests(tests, src):
    """Derive the suite path from the module under test, or check the given one mirrors it."""
    if not src:
        return tests, None
    want = mirror_dir(src[0])
    if not tests:
        stem = os.path.basename(src[0])[:-2]  # drop the .c
        return ["%s/test_%s" % (want, stem)], None
    for t in tests:
        got = t.rstrip("/").rsplit("/", 1)[0]
        if got != want:
            return None, (
                "suite %s does not mirror %s\n"
                "  the module is %s, so the suite belongs in test/%s/<test_name>"
                % (t, src[0], module_path(src[0]), want)
            )
    return tests, None


HWCAP_REFUSAL = """refusing to update %s: this drops a capability arm and puts nothing in its place.

  %s

A capability is two mutually exclusive arms - test/core_setup/hal/<vendor>/ under
PROTOCORE_HAS_HW_<X>, and test/core_setup/hal/portable/ under !PROTOCORE_HAS_HW_<X>. Both ship, and
the host drives BOTH. An env left with neither still links, still goes green, and has stopped
testing the arm the hardware runs - which nothing reports.

An unresolved symbol a hal arm defines is the CAP BEING OFF. Turn it on and build that arm; do not
swap in the other half.

To cover the other arm, give it its OWN env and keep this one as it is:

  harness.py env add <name>_sw --after <name> --clone <name>
  harness.py env update <name>_sw --drop-src <hal arm> --src <other arm> \\
      --drop-flags=-DPROTOCORE_HAS_HW_<X>=1 --flags=-DPROTOCORE_HAS_HW_<X>=0

Dropping one arm and adding the other IN THE SAME CALL is what this allows: it is a swap, not a
removal, and it is how the twin env above is pointed at its arm."""


def hal_arm(path):
    """The hal vendor directory a src entry names, or None. `+<test/core_setup/hal/host/x.c>` -> host."""
    m = re.search(r"test/core_setup/hal/([^/]+)/", path.replace("\\", "/"))
    return m.group(1) if m else None


def hwcap_orphaned(before, after):
    """Lines naming every hal file the edit removes while adding no arm in its place.

    A swap - one arm out, another in - is the legitimate edit and reports nothing. A bare removal
    leaves the env with no arm at all, which is the one that goes green having stopped testing.
    """
    was = {s for s in before.get("src", []) if hal_arm(s)}
    now = {s for s in after.get("src", []) if hal_arm(s)}
    gone = was - now
    if not gone:
        return []
    # `platform` is the per-env host seam every env carries, not a capability's arm.
    gone = {g for g in gone if "_platform.c" not in g}
    if not gone or (now - was):
        return []
    return sorted("dropped: %s" % g for g in gone)


def src_filter(p):
    """A path in the matrix's own src spelling, whichever form it arrived in.

    The matrix writes a source as `+<path>`, and a caller reading the matrix passes it back already
    written that way. Wrapping that a second time gives `+<+<path>>`, which matches no file and is
    only found when the link fails.
    """
    p = p.strip()
    return p if p.startswith(("+<", "-<")) else "+<%s>" % p


def cmd_env_add(a):
    lock = lock_acquire(TABLE)
    if not lock:
        print("could not take the table lock within %.0fs" % LOCK_TIMEOUT_S)
        return 1
    try:
        text, before = read_table(TABLE)
        envs = before["envs"]
        if a.name in envs:
            print("env already present:", a.name)
            return 1
        if a.after not in envs:
            print("anchor env not found:", a.after)
            return 1
        tests, why = resolve_tests(a.tests, a.src)
        if why:
            print(why)
            return 1
        if a.base and a.clone:
            print("--base and --clone are alternatives: extend a stack, or copy one env's src")
            return 1
        if a.base and a.base.replace("env:", "") not in envs:
            print("base env not found:", a.base)
            return 1
        src = list(envs[a.clone]["src"]) if a.clone else (["-<*>"] if a.only else [])
        flags = list(envs[a.clone].get("flags", [])) if a.clone else []
        src += [p for p in (src_filter(x) for x in a.src) if p not in src]
        flags += [f for f in (unescape_flag(x) for x in a.flags) if f not in flags]
        # A --base env EXTENDS a stack: it inherits that stack's flags and build_src_filter through
        # platformio, so it states only what it adds. Copying the stack's src instead (--clone)
        # loses the ini-level inheritance the stack's own children rely on.
        entry = {"desc": a.desc}
        if a.base:
            entry["base"] = a.base if a.base.startswith("env:") else "env:" + a.base
        if flags or not a.base:
            entry["flags"] = flags
        if src or not a.base:
            entry["src"] = src
        entry["tests"] = list(tests)
        if a.extra_scripts:
            entry["extra_scripts"] = list(a.extra_scripts)
        text = splice_after(text, a.after, a.name, entry)
        rc = write_verified(TABLE, text, before, {a.name}, {a.name: entry})
        if rc == 0:
            print("added %s (%d src entries); run: harness.py env gen" % (a.name, len(src)))
        return rc
    finally:
        lock_release(lock)


def cmd_env_update(a):
    """Change an existing env. Every list option adds unless its --drop- twin removes."""
    lock = lock_acquire(TABLE)
    if not lock:
        print("could not take the table lock within %.0fs" % LOCK_TIMEOUT_S)
        return 1
    try:
        text, before = read_table(TABLE)
        envs = before["envs"]
        if a.name not in envs:
            print("env not found:", a.name)
            return 1
        entry = json.loads(json.dumps(envs[a.name]))  # a copy the splice is verified against

        def merge(key, add, drop, wrap=None):
            cur = list(entry.get(key, []))
            for v in drop:
                v = wrap(v) if wrap else v
                if v in cur:
                    cur.remove(v)
                else:
                    print("not present in %s.%s: %s" % (a.name, key, v))
            for v in add:
                v = wrap(v) if wrap else v
                if v not in cur:
                    cur.append(v)
            if cur or key in entry:
                entry[key] = cur

        merge("flags", a.flags, a.drop_flags, wrap=unescape_flag)
        merge("src", a.src, a.drop_src, wrap=src_filter)
        merge("tests", a.tests, a.drop_tests)
        merge("extra_scripts", a.extra_scripts, a.drop_extra_scripts)
        left = hwcap_orphaned(envs[a.name], entry)
        if left:
            print(HWCAP_REFUSAL % (a.name, "\n  ".join(left)))
            return 1
        if a.desc is not None:
            entry["desc"] = a.desc
        if a.base is not None:
            if a.base == "":
                entry.pop("base", None)  # stand alone again
            else:
                if a.base not in envs:
                    print("base env not found:", a.base)
                    return 1
                entry["base"] = "env:" + a.base
        if entry == envs[a.name]:
            print("no change:", a.name)
            return 0
        text = splice_replace(text, a.name, entry)
        rc = write_verified(TABLE, text, before, {a.name}, {a.name: entry})
        if rc == 0:
            print("updated %s; run: harness.py env gen" % a.name)
        return rc
    finally:
        lock_release(lock)


def cmd_env_addsrc(a):
    """Add the same deps to every env that already builds a given .c.

    A module that starts taking a borrow calls into the pool, and the pool calls into the arena and
    the platform, so every env that built it before needs those three added at once. Doing that by
    hand is what makes a session reach for a script and rewrite the whole table.
    """
    lock = lock_acquire(TABLE)
    if not lock:
        print("could not take the table lock within %.0fs" % LOCK_TIMEOUT_S)
        return 1
    try:
        text, before = read_table(TABLE)
        changed, expect = set(), {}
        for name, e in before["envs"].items():
            src = e.get("src")
            if not src or not any(a.builds in x for x in src):
                continue
            add = [src_filter(d) for d in a.dep]
            add = [x for x in add if x not in src]
            if not add:
                continue
            entry = json.loads(json.dumps(e))
            entry["src"] = src + add
            text = splice_replace(text, name, entry)
            changed.add(name)
            expect[name] = entry
            print("%-40s + %s" % (name, ", ".join(x[2:-1] for x in add)))
        if not changed:
            print("no env builds %s that is missing those deps" % a.builds)
            return 0
        rc = write_verified(TABLE, text, before, changed, expect)
        if rc == 0:
            print("\n%d envs updated; run: harness.py env gen" % len(changed))
        return rc
    finally:
        lock_release(lock)


def cmd_env_addflag(a):
    """Add the same flags to every env that already builds a given .c.

    Giving a module its enable gate is this operation: the header wraps in
    PROTOCORE_ENABLE_<X>, which defaults off, so every env that was compiling the module without
    ever naming it has to start naming it. Doing that by hand across 40 envs is what makes a
    session reach for a script and rewrite the whole table.
    """
    lock = lock_acquire(TABLE)
    if not lock:
        print("could not take the table lock within %.0fs" % LOCK_TIMEOUT_S)
        return 1
    try:
        text, before = read_table(TABLE)
        changed, expect = set(), {}
        for name, e in before["envs"].items():
            src = e.get("src")
            if not src or not any(a.builds in x for x in src):
                continue
            flags = list(e.get("flags", []))
            add = [f for f in (unescape_flag(x) for x in a.flag) if f not in flags]
            if not add:
                continue
            entry = json.loads(json.dumps(e))
            entry["flags"] = flags + add
            text = splice_replace(text, name, entry)
            changed.add(name)
            expect[name] = entry
            print("%-40s + %s" % (name, ", ".join(add)))
        if not changed:
            print("no env builds %s that is missing those flags" % a.builds)
            return 0
        rc = write_verified(TABLE, text, before, changed, expect)
        if rc == 0:
            print("\n%d envs updated; run: harness.py env gen" % len(changed))
        return rc
    finally:
        lock_release(lock)


def cmd_env_remove(a):
    """Cut an env out of the matrix. Its tests are not deleted: they run wherever else they are named,
    so an env whose tests no env still names takes its coverage with it and is refused unless --force."""
    lock = lock_acquire(TABLE)
    if not lock:
        print("could not take the table lock within %.0fs" % LOCK_TIMEOUT_S)
        return 1
    try:
        text, before = read_table(TABLE)
        envs = before["envs"]
        missing = [n for n in a.name if n not in envs]
        if missing:
            print("env not found:", " ".join(missing))
            return 1
        if len(envs) - len(set(a.name)) < 1:
            print("refusing to empty the table")
            return 1
        kept = {n: e for n, e in envs.items() if n not in set(a.name)}
        still_run = set()
        for e in kept.values():
            still_run.update(e.get("tests") or [])
        orphaned = []
        for n in set(a.name):
            for t in envs[n].get("tests") or []:
                if t not in still_run:
                    orphaned.append("%s (only in %s)" % (t, n))
        if orphaned and not a.force:
            print("these suites would stop running - name them in another env first, or pass --force:")
            for o in sorted(set(orphaned)):
                print("   ", o)
            return 1
        if a.verbose:
            # Where each suite still runs once these envs are gone. A removal is only safe because
            # something else names the same suite, so that is what is printed rather than a count.
            covers = {}
            for n, e in kept.items():
                for t in e.get("tests") or []:
                    covers.setdefault(t, []).append(n)
            for n in sorted(set(a.name)):
                tests = envs[n].get("tests") or []
                print("%s (%d suite%s)" % (n, len(tests), "" if len(tests) == 1 else "s"))
                for t in tests:
                    where = covers.get(t) or []
                    print("    %-52s %s" % (t, ("still in: " + ", ".join(sorted(where))) if where else "NOWHERE ELSE"))
        for n in set(a.name):
            text = splice_remove(text, n)
        left = [n for n in set(a.name) if n in json.loads(text)["envs"]]
        if left:
            print("still present after the splice:", " ".join(left))
            return 1
        rc = write_verified(TABLE, text, before, set(a.name), {})
        if rc == 0:
            print("removed %d env(s); run: harness.py env gen" % len(set(a.name)))
        return rc
    finally:
        lock_release(lock)


# ---------------------------------------------------------------------------
# env gen: platformio.ini from the matrix
# ---------------------------------------------------------------------------

NATIVE_BASE = """; Shared flags for all native environments
[native_base]
platform = native
build_flags =
    ; src/ is C11 (docs/SRC_LAW.md section 0).
    -std=c11
    ; strnlen is POSIX 2008, not ISO C11, and -std=c11 asks glibc for ISO C alone - so <string.h>
    ; declares strlen and not strnlen. Ban #1 (docs/SRCBANNED.md) requires strnlen everywhere, so
    ; without this the whole tree compiles it as an implicit declaration returning int, and every
    ; `size_t n = strnlen(...)` truncates through 32 bits with nothing but a warning. Naming the
    ; POSIX level is what makes the bounded string functions visible; the language stays C11.
    -D_POSIX_C_SOURCE=200809L
    ; src/ contains no throw / try / catch (no-heap, no-stdlib, deterministic - and the ESP32
    ; target builds without exceptions anyway), so this only strips what the host toolchain would
    ; add on its own. It matters for coverage: with exceptions on, g++ emits an unwind edge at
    ; every call to a function that is not noexcept, and gcov counts each one as a BRANCH. Those
    ; edges are unreachable by construction in a codebase that never throws, so they are pure
    ; noise in the branch numbers and they make 100% branch coverage unattainable - wamp.cpp alone
    ; carried 114 of them (280 branches -> 166, and 57 of its 84 "uncovered" branches vanished).
    ; Keep this here rather than only in the coverage run so the tested build and the measured
    ; build are the same build.
    -fno-exceptions
    ; Link-time optimization, required rather than preferred. The transport's slot state lives
    ; behind an incomplete type, so its accessors are defined in one translation unit and called
    ; from others. Measured on gcc 13.2, gcc 16.1 and clang 22 (tools/dev_env/pimpl_bench): without
    ; -flto those calls are NEVER inlined at any level - -O0 through -Os and -Oz all emit the call.
    ; With -flto they inline from -O1 (gcc) / -O2 (clang), and the hot loop comes out the same size
    ; or smaller than the fully-public struct it replaced.
    -flto
    ; The host branch of the platform: the stand-ins protocore_platform.h reaches for by bare name
    ; (protocore_net_host.h, Arduino.h, freertos/, lwip/) when no vendor macro matched. It sits on
    ; the include path as a root of its own, so those names resolve the way a vendor SDK's do.
    ; The host has no bounded DRAM, so the arenas are sized far past any suite's worst case. A
    ; span that fails to allocate is then a real defect rather than a budget the env forgot to
    ; raise: the route table, the persist end and the scratch end all come out of this one pool.
    -DPROTOCORE_SECURE_ARENA_SIZE=262144
    -I test/core_setup/hal/host
    -I test/support
    -I src
    ; test/core_setup/ sits beside src/, not inside it, so the board profile an include names as
    ; vendor/board_profiles/... resolves from the repo root rather than from src/.
    -I .
    ; PROTOCORE_HOST is NOT defined here. board_profiles/protocore_platform.h derives it from the vendor
    ; axis: nothing on a native build matches a vendor, so its else-arm defines it. Passing it on
    ; the command line as well made a second source of truth that the vendor axis could not
    ; libm is a separate library to the C driver. g++ pulled it in behind libstdc++, so nothing here
    ; ever named it; gcc does not, and every env whose sources call sin / cos / sqrt / atan2 / fabs
    ; fails at the link with an undefined reference instead. It sits in the shared block because
    ; which envs reach a math call is a property of their sources, not of their configuration.
    -lm
; The filesystem the device actually runs, so a host test asserts against real directory
; semantics rather than a hand-rolled tree that agrees with itself. Only the envs whose sources
; include lfs.h build it - the dependency finder does not compile what nothing reaches for.
lib_deps =
    anurag3301/littlefs@^2.11.6
; Link library objects directly instead of collecting them into a .a first. Unity is meant to be
; compiled from source with the project's own flags (UnityConfigurationGuide.md), and an archive
; built without LTO IR cannot satisfy an LTO link: the plugin drops UnityAssertEqualNumber and
; UnityFail and every suite fails to link.
lib_archive = no
test_build_src = yes"""


def render_env(name, e, bases=frozenset()):
    # A stack base carries flags and a build_src_filter for the envs that extend it and owns no
    # suite. It is emitted as a plain section rather than [env:...] so pio never treats it as a
    # target: an env with no test_filter runs every suite in test/, and test_ignore on it would be
    # inherited by every child, suppressing theirs. A section is neither.
    base = e.get("base", "native_base")
    if base.startswith("env:") and base[len("env:") :] in bases:
        base = base[len("env:") :]
    lines = []
    desc = e.get("desc", "").strip()
    if desc:
        for dl in desc.split("\n"):
            lines.append(f"; {dl}".rstrip())
    lines.append(f"[{name}]" if name in bases else f"[env:{name}]")
    lines.append(f"extends = {base}")
    flags = e.get("flags", [])
    if flags:
        lines.append("build_flags =")
        lines.append(f"    ${{{base}.build_flags}}")
        for fl in flags:
            lines.append(f"    {fl}")
    src = e.get("src", [])
    if src:
        lines.append("build_src_filter =")
        for s in src:
            for b in bases:
                s = s.replace(f"${{env:{b}.", f"${{{b}.")
            # build_src_filter resolves against src/, and test/core_setup/ sits beside it, so a table
            # entry naming the repo-relative path matches nothing and the file is silently left
            # out of the link. The table states the path from the repo root, the same way an
            # #include does; the step up is what the filter needs to reach it.
            s = s.replace("<test/core_setup/", "<../test/core_setup/")
            lines.append(f"    {s}")
    tests = e.get("tests", [])
    if tests:
        lines.append("test_filter =")
        for t in tests:
            lines.append(f"    {t}")
    if e.get("test_build_src"):
        lines.append(f"test_build_src = {e['test_build_src']}")
    if e.get("extra_scripts"):
        lines.append("extra_scripts =")
        for s in e["extra_scripts"]:
            lines.append(f"    {s}")
    return "\n".join(lines)


def render_block(table):
    envs = table["envs"]
    # An entry that names no suite is a stack base: a section others extend, never a target.
    bases = frozenset(n for n, e in envs.items() if not e.get("tests"))
    parts = [
        BEGIN,
        "; Single source of truth: test/test_matrix.json  ("
        + str(len(envs) - len(bases))
        + " native envs, "
        + str(len(bases))
        + " stack bases)",
        "",
        NATIVE_BASE,
    ]
    for name, e in envs.items():
        parts.append("")
        parts.append(render_env(name, e, bases))
    parts.append("")
    parts.append(END)
    return "\n".join(parts) + "\n"


def strip_native_base(head):
    """Drop a [native_base] left in the head; render_block emits it now."""
    lines = head.split("\n")
    out = []
    i = 0
    while i < len(lines):
        if lines[i].strip() == "[native_base]":
            while out and (out[-1].strip() == "" or out[-1].lstrip().startswith(";")):
                out.pop()  # its own comment header goes with it
            i += 1
            while i < len(lines) and not lines[i].startswith("["):
                i += 1
            continue
        out.append(lines[i])
        i += 1
    return "\n".join(out)


def split_head(text):
    """Return the static head (everything above the generated region)."""
    if BEGIN in text:
        return strip_native_base(text.split(BEGIN, 1)[0]).rstrip("\n") + "\n\n"
    lines = text.split("\n")
    first = next((i for i, l in enumerate(lines) if l.startswith("[env:native")), len(lines))
    j = first
    while j > 0 and (lines[j - 1].strip() == "" or lines[j - 1].lstrip().startswith(";")):
        j -= 1
    return "\n".join(lines[:j]).rstrip("\n") + "\n\n"


def cmd_env_gen(a):
    with open(TABLE, encoding="utf-8") as f:
        table = json.load(f)
    with open(INI, encoding="utf-8") as f:
        cur = f.read()
    new = split_head(cur) + render_block(table)
    if a.check:
        if cur != new:
            print("platformio.ini is out of date; run: harness.py env gen", file=sys.stderr)
            return 1
        print("platformio.ini is up to date.")
        return 0
    with open(INI, "w", encoding="utf-8", newline="\n") as f:
        f.write(new)
    print(f"Wrote {len(table['envs'])} native envs to platformio.ini")
    return 0


def cmd_env_list(a):
    with open(TABLE, encoding="utf-8") as f:
        envs = json.load(f)["envs"]
    for name, e in envs.items():
        if a.bases_only and e.get("tests"):
            continue
        if a.targets_only and not e.get("tests"):
            continue
        if a.verbose:
            print(
                "%-32s %2d flags %3d src %s"
                % (name, len(e.get("flags", [])), len(e.get("src", [])), " ".join(e.get("tests", [])))
            )
        else:
            print(name)
    return 0


# ---------------------------------------------------------------------------
# env select: changed files -> affected envs
# ---------------------------------------------------------------------------

FULL_FRACTION = 0.9
NEVER_SELECT = {"native_pentest", "native_codeql"}
ADDITIVE_SAFE = {"src/protocore_config.h", "test/test_matrix.json", "platformio.ini"}
FORCE_FULL_EXACT = {
    "platformio.ini",
    "test/test_matrix.json",
    "test/harness.py",
    "sonar-project.properties",
    "library.json",
    "library.properties",
}
FORCE_FULL_PREFIX = (
    "tools/",
    ".github/workflows/",
    # The host branch of the platform: every native env resolves its vendor stand-ins here.
    "test/core_setup/",
    "test/support/",
    "test/fixtures/",
    "test/servers/",
)
IGNORE_PREFIX = ("docs/", "examples/", ".vscode/", ".github/ISSUE_TEMPLATE/")
IGNORE_EXACT = {
    "README.md",
    "CHANGELOG.md",
    "LICENSE",
    ".gitignore",
    ".clang-format",
    ".prettierignore",
    ".editorconfig",
    "CONTRIBUTING.md",
    "SECURITY.md",
    "test/TEST_REPORT.md",
    "test/coverage.xml",
    "test/dep_graph.json",
}


def parse_ini_envs(ini_path):
    """Return {env: {"src": [globs], "tests": [dirs], "flags": [str]}} for native_* envs."""
    envs = {}
    cur = None
    section = None
    with open(ini_path, encoding="utf-8") as fh:
        for raw in fh:
            line = raw.rstrip("\n")
            # A stack base is a bare [native_stack_l46]-style section: it owns no suite and carries
            # the flags and sources for the envs that extend it, so it has to be read too.
            m = re.match(r"^\[(?:env:)?(native[A-Za-z0-9_]*)\]\s*$", line)
            if m:
                cur = m.group(1)
                envs[cur] = {"src": [], "tests": [], "flags": [], "extends": None}
                section = None
                continue
            if line.startswith("["):
                cur = None
                section = None
                continue
            if cur is not None:
                x = re.match(r"^\s*extends\s*=\s*(?:env:)?([A-Za-z0-9_]+)\s*$", line)
                if x:
                    envs[cur]["extends"] = x.group(1)
                    section = None
                    continue
            if cur is None:
                continue
            # A blank line or a comment ends the current list. The next env's desc is emitted as
            # ';' lines above its header, and without this they are read as more list entries.
            if not line.strip() or line.lstrip().startswith(";"):
                section = None
                continue
            if re.match(r"^\s*build_src_filter\s*=", line):
                section = "src"
                rest = line.split("=", 1)[1].strip()
                if rest:
                    _add_src(envs[cur], rest)
                continue
            if re.match(r"^\s*test_filter\s*=", line):
                section = "tests"
                rest = line.split("=", 1)[1].strip()
                if rest:
                    envs[cur]["tests"].append(rest)
                continue
            if re.match(r"^\s*build_flags\s*=", line):
                section = "flags"
                continue
            if re.match(r"^\s*[A-Za-z_]+\s*=", line):
                section = None
                continue
            if section == "src" and line.strip():
                _add_src(envs[cur], line.strip())
            elif section == "tests" and line.strip():
                envs[cur]["tests"].append(line.strip())
            elif section == "flags" and line.strip():
                envs[cur]["flags"].append(line.strip())

    # Fold each base chain in. `extends` is what carries a stack base's sources and flags to the
    # envs built on it; without this a direct compile builds the suite against nothing and every
    # namespace the suite drives comes back undefined at the link. The base goes first so the env's
    # own entries are the later word. A ${base.build_flags} interpolation is dropped: the flags it
    # names are now present literally.
    def fold(name, key, seen):
        e = envs.get(name)
        if not e or name in seen:
            return []
        seen.add(name)
        base = e.get("extends")
        out = fold(base, key, seen) if base else []
        return out + [v for v in e[key] if not v.startswith("${")]

    for name in list(envs):
        for key in ("src", "flags"):
            envs[name][key] = fold(name, key, set())
    return envs


def _add_src(env, token):
    m = re.match(r"^\+<(.+)>$", token)
    if m:
        env["src"].append(m.group(1))


def _match_glob(rel, glob):
    return fnmatch.fnmatch(rel, glob) or fnmatch.fnmatch(rel, glob.replace("*", "**"))


def load_graph():
    try:
        with open(DEP_GRAPH, encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return {}


def _services_dir(path):
    m = re.match(r"^src/(services/[^/]+/)", path)
    return m.group(1) if m else None


def _src_hits(f, src_globs):
    rel = f[len("src/") :]
    hits = {name for name, g in src_globs if _match_glob(rel, g)}
    if hits:
        return hits
    sd = _services_dir(f)
    if sd:
        return {name for name, g in src_globs if g.startswith(sd)}
    return set()


def _classify_additive(f, base, head, known_envs):
    """Content-aware verdict for an ADDITIVE_SAFE file: a set of affected envs, or "FULL"."""
    # Shared with tools/ci_tooling/generate/example_footprints.py, so it stays a library rather
    # than being copied in here.
    sys.path.insert(0, os.path.join(ROOT, "tools", "ci_tooling", "lib"))
    import affected_common as ac

    old = ac.file_at(base, f)
    new = ac.file_at(head, f)
    if f == "src/protocore_config.h":
        return set() if ac.config_header_additive(old, new) else "FULL"
    if f == "test/test_matrix.json":
        res = ac.matrix_changed_envs(old, new)
    else:  # platformio.ini
        res = ac.ini_changed_envs(old, new)
    if res == "FULL":
        return "FULL"
    return {e for e in res if e in known_envs}  # a new env is present in the HEAD ini we parsed


def classify(changed, envs, graph, total_testable, base=None, head=None):
    """Return "FULL", "NONE", or a sorted list of affected env names."""
    affected = set()
    src_globs = [(name, g) for name, e in envs.items() for g in e["src"]]
    known_envs = set(envs) - NEVER_SELECT
    test_map = {}
    for name, e in envs.items():
        for t in e["tests"]:
            test_map.setdefault(t, set()).add(name)

    for f in changed:
        f = f.strip().replace("\\", "/")
        if not f:
            continue
        # An additive gate / env change (with a diff base) selects only the new env(s), never FULL.
        if base is not None and f in ADDITIVE_SAFE:
            res = _classify_additive(f, base, head, known_envs)
            if res == "FULL":
                return "FULL"
            if res:
                affected |= res
            continue
        if f.endswith(".md"):
            continue  # markdown never affects compilation (even under a FORCE_FULL_ prefix)
        if f in FORCE_FULL_EXACT or f.startswith(FORCE_FULL_PREFIX):
            return "FULL"
        if f in IGNORE_EXACT or f.startswith(IGNORE_PREFIX):
            continue
        if f.startswith(("src/", "include/")):
            # The compiler dep graph maps a source OR header to exactly the envs whose include
            # closure contains it - so a feature header hits only its feature's envs, not FULL.
            if f in graph:
                affected |= set(graph[f]) - NEVER_SELECT
                continue
            # Absent from the graph (a brand-new file): attribute a source by its build_src_filter
            # glob and a new header by its services/<sub>/ dir.
            hits = _src_hits(f, src_globs) - NEVER_SELECT
            if hits:
                affected |= hits
                continue
            return "FULL"
        if f.startswith("test/"):
            m = re.match(r"^test/(test_[A-Za-z0-9_]+)(/|$)", f)
            if m:
                dirs = test_map.get(m.group(1))
                if not dirs:
                    return "FULL"  # a test dir with no env owning it
                affected |= dirs
                continue
            return "FULL"  # shared test infra under test/ not caught above
        # Anything else is a top-level path outside src/ + test/ + the build-config allowlist
        # handled above. It cannot affect what the native suite compiles.
        continue

    affected -= NEVER_SELECT
    if not affected:
        return "NONE"
    # A shared header hits almost every env; run FULL so coverage regenerates from scratch.
    if total_testable and len(affected) >= FULL_FRACTION * total_testable:
        return "FULL"
    return sorted(affected)


def cmd_env_select(a):
    if a.full:
        print("FULL")
        return 0
    changed = list(a.changed_file)
    if not sys.stdin.isatty():
        changed += sys.stdin.read().splitlines()
    changed = [c for c in changed if c.strip()]
    if not changed:
        print("NONE")
        return 0
    envs = parse_ini_envs(INI)
    result = classify(changed, envs, load_graph(), len(set(envs) - NEVER_SELECT), base=a.base, head=a.head)
    print(" ".join(result) if isinstance(result, list) else result)
    return 0


# ---------------------------------------------------------------------------
# env deps: the include closure every env compiles
# ---------------------------------------------------------------------------


def cmd_env_deps(a):
    """Map every src/ header and source to the envs whose translation units include it.

    Runs the compiler's own dependency scan (-MM) per env with that env's flags, so the answer is
    the include closure the env really compiles rather than a guess from the src filter.
    """
    envs = parse_ini_envs(INI)
    names = a.envs or sorted(n for n in envs if envs[n]["tests"] and n not in NEVER_SELECT)
    graph = {}
    cc = shutil.which("gcc") or shutil.which("cc")
    if not cc:
        print("no gcc on PATH", file=sys.stderr)
        return 1
    # One -MM per (env, TU). They share nothing and only the join writes graph, so the whole cross
    # product runs at once and the scan is bound by cores rather than by the number of envs.
    jobs = []
    for name in names:
        e = envs.get(name)
        if not e:
            print("unknown env:", name, file=sys.stderr)
            return 1
        incs, defs = _flag_split(e["flags"])
        for tu in _resolve_src(e["src"]):
            jobs.append((name, [cc, "-MM", "-std=c11", "-D_POSIX_C_SOURCE=200809L"] + defs + incs + [tu]))

    def scan(job):
        name, cmd = job
        r = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
        if r.returncode != 0:
            return name, ()  # an env whose TU does not preprocess alone contributes nothing
        return name, tuple(d for d in _parse_mm(r.stdout) if d.startswith("src/"))

    done = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        for name, deps in pool.map(scan, jobs):
            for dep in deps:
                graph.setdefault(dep, set()).add(name)
            done += 1
            if a.progress and (done % 100 == 0 or done == len(jobs)):
                print("  [%d/%d] scans" % (done, len(jobs)), file=sys.stderr)
    # A named subset re-scans only those envs, so it carries every other env's entries over from the
    # graph on disk: drop the rescanned names, then merge what this run found.
    if a.envs:
        rescanned = set(names)
        try:
            with open(DEP_GRAPH, encoding="utf-8") as fh:
                prior = json.load(fh)
        except (OSError, ValueError):
            prior = {}
        for dep, owners in prior.items():
            keep = [o for o in owners if o not in rescanned]
            if keep:
                graph.setdefault(dep, set()).update(keep)
    out = {k: sorted(v) for k, v in sorted(graph.items())}
    with open(DEP_GRAPH, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(out, fh, indent=1)
        fh.write("\n")
    print("wrote %s: %d files across %d envs" % (DEP_GRAPH, len(out), len(names)))
    return 0


def _parse_mm(text):
    text = text.replace("\\\n", " ")
    parts = text.split(":", 1)
    if len(parts) != 2:
        return []
    out = []
    for tok in parts[1].split():
        p = os.path.relpath(os.path.abspath(os.path.join(ROOT, tok)), ROOT).replace("\\", "/")
        out.append(p)
    return out


def _flag_split(flags):
    """Split an env's build_flags into include args and define args, dropping ini interpolation."""
    incs, defs = [], []
    it = iter(flags)
    for f in it:
        if f.startswith("${") or f.startswith(";"):
            continue
        if f.startswith("-I"):
            incs.append(f if len(f) > 2 else "-I" + next(it, ""))
        elif f.startswith("-D"):
            defs.append(f)
    # -Iinclude is PlatformIO's implicit include_dir, where protocore.h lives. Nothing here is pio,
    # so every path that compiles or scans a TU names it.
    for base in ("-Itest/core_setup/hal/host", "-Itest/support", "-Isrc", "-Iinclude", "-I."):
        if base not in incs:
            incs.append(base)
    return incs, defs


def _resolve_src(globs):
    """Expand an env's build_src_filter '+<glob>' entries into repo-relative .c paths.

    An entry is read under src/ first, then from the repo root: test/core_setup/ is a root directory, so
    the backends an env names there ("test/core_setup/hal/portable/portable_bignum.c") resolve from the
    root and nowhere else. An entry matching neither is reported rather than dropped.

    A path is returned once however many entries reach it. An env that extends a stack base inherits
    that base's filter and then names files of its own, and the two overlap; one .c compiled twice is
    every symbol in it defined twice, which the linker refuses.
    """
    import glob as _glob

    out = []
    seen = set()
    for g in globs:
        stem = g[3:] if g.startswith("../") else g
        stem = stem.replace("../", "")
        hits = []
        for rel in ("src/" + stem, stem):
            full = os.path.join(ROOT, rel)
            if os.path.isfile(full):
                hits = [rel.replace("\\", "/")]
                break
            found = [
                os.path.relpath(h, ROOT).replace("\\", "/")
                for h in _glob.glob(full, recursive=True)
                if h.endswith(".c")
            ]
            if found:
                hits = found
                break
        if not hits:
            print("  src filter matches nothing: %s" % g, file=sys.stderr)
            continue
        for h in hits:
            if h not in seen:
                seen.add(h)
                out.append(h)
    return out


# ---------------------------------------------------------------------------
# runners / keys: the logic the PlatformIO pre-build hooks call
# ---------------------------------------------------------------------------

GENERATED_RUNNER = "unity_runner.c"

RUBY_GLOBS = (
    r"C:\tools\ruby*\bin\ruby.exe",
    r"C:\Ruby*\bin\ruby.exe",
    r"C:\Program Files\Ruby*\bin\ruby.exe",
    "/usr/bin/ruby",
    "/usr/local/bin/ruby",
    "/opt/homebrew/bin/ruby",
)


def find_ruby():
    """Ruby runs the generator. A missing one is an error, never a silent skip."""
    found = shutil.which("ruby")
    if found:
        return found
    import glob as _glob

    for pattern in RUBY_GLOBS:
        for hit in sorted(_glob.glob(pattern), reverse=True):  # newest install first
            if os.path.isfile(hit):
                return hit
    return None


def find_unity_generator(libdeps=None, envname=None):
    """Unity is a lib_dep, so the generator lives under an env's libdeps."""
    libdeps = libdeps or os.path.join(ROOT, ".pio", "libdeps")
    candidates = []
    if envname:
        candidates.append(os.path.join(libdeps, envname, "Unity", "auto", "generate_test_runner.rb"))
    if os.path.isdir(libdeps):
        for d in sorted(os.listdir(libdeps)):
            candidates.append(os.path.join(libdeps, d, "Unity", "auto", "generate_test_runner.rb"))
    for c in candidates:
        if os.path.isfile(c):
            return c
    return None


# What Unity's generate_test_runner.rb collects, and the shape a case has to have to be collected.
# A function that does not match is not an error to the generator - it is simply never registered,
# so the suite passes while the case never ran. That silence is what the reports below break.
UNITY_CASE = re.compile(r"^[ \t]*void[ \t]+(test_\w+)[ \t]*\([ \t]*(?:void)?[ \t]*\)", re.M)
NEAR_MISS = re.compile(r"^[ \t]*void[ \t]+(\w+)[ \t]*\([ \t]*(?:void)?[ \t]*\)[ \t]*\r?\n[ \t]*\{", re.M)
NOT_A_CASE = ("setUp", "tearDown", "main", "suiteSetUp", "suiteTearDown")
RUNNER_CASE = re.compile(r"^\s*run_test\(\s*(\w+)\s*,", re.M)


def runner_cases(path):
    """The cases Unity's generator will register in @p path, and the ones it will walk past."""
    s = open(path, encoding="utf-8").read()
    found = UNITY_CASE.findall(s)
    missed = [n for n in NEAR_MISS.findall(s) if n not in found and n not in NOT_A_CASE]
    return found, missed


def suite_source(suite_dir):
    """The one .c in a suite that holds its cases, or None."""
    if not os.path.isdir(suite_dir):
        return None
    for f in sorted(os.listdir(suite_dir)):
        p = os.path.join(suite_dir, f)
        if f.endswith(".c") and f != GENERATED_RUNNER and runner_cases(p)[0]:
            return p
    return None


def generate_runner(suite_dir, libdeps=None, envname=None):
    """Emit suite_dir/unity_runner.c from the one source that holds the cases.

    This is the bridge to Unity's generate_test_runner.rb: it is what turns a suite source into a
    runnable test, so writing a test means writing the cases and then coming through here.
    """
    if not os.path.isdir(suite_dir):
        return None
    candidates = [f for f in sorted(os.listdir(suite_dir)) if f.endswith(".c") and f != GENERATED_RUNNER]
    # A suite that writes its own main() registers its own cases, so a generated runner beside it is
    # a second main() in the same link. The build compiles every .c in the suite directory, so the
    # file existing at all is what breaks it - not anything the caller asked for.
    for f in candidates:
        with open(os.path.join(suite_dir, f), encoding="utf-8", errors="replace") as fh:
            if "int main(" in fh.read():
                stale = os.path.join(suite_dir, GENERATED_RUNNER)
                if os.path.isfile(stale):
                    os.remove(stale)
                    print("removed %s: %s defines its own main()" % (os.path.relpath(stale, ROOT), f))
                return None
    sources = [f for f in candidates if runner_cases(os.path.join(suite_dir, f))[0]]
    if not sources:
        # A .c with no collectable case is the common way a new suite silently does nothing.
        for f in candidates:
            _, missed = runner_cases(os.path.join(suite_dir, f))
            if missed:
                raise SystemExit(
                    "runners: %s holds no case Unity's generator will register.\n"
                    "  It collects file-scope `void test_<name>(void)` and nothing else, so these\n"
                    "  are walked past and never run: %s\n"
                    "  Rename each to test_<name> - a case the generator skips costs coverage in\n"
                    "  silence, because the suite still passes."
                    % (os.path.relpath(os.path.join(suite_dir, f), ROOT), ", ".join(missed))
                )
        return None
    # The generator takes one input file and emits one main(), so a suite whose cases are spread
    # across several sources cannot be registered from any single one of them. Refused here rather
    # than generating from the first and dropping the rest.
    if len(sources) > 1:
        raise SystemExit(
            "runners: %s holds test cases in %d sources (%s).\n"
            "  Unity's generator registers one source per runner, so the rest would never run.\n"
            "  Put the cases in one file, or give each file its own suite directory."
            % (os.path.relpath(suite_dir, ROOT), len(sources), ", ".join(sources))
        )
    ruby = find_ruby()
    generator = find_unity_generator(libdeps, envname)
    if not ruby:
        raise SystemExit(
            "runners: ruby not found on PATH or in %s - install it "
            "(choco install ruby, or winget install RubyInstallerTeam.Ruby.3.4)" % ", ".join(RUBY_GLOBS[:3])
        )
    if not generator:
        raise SystemExit("runners: Unity's generate_test_runner.rb not found under .pio/libdeps")
    src = os.path.join(suite_dir, sources[0])
    out = os.path.join(suite_dir, GENERATED_RUNNER)
    subprocess.run([ruby, generator, src, out], check=True)
    # The generator's own pattern does not read the parameter list, so a file-scope helper named
    # test_* is registered as a case and called with no arguments. Whatever it registers that is
    # not a `void test_<name>(void)` in the source is that, and it is refused here: the runner is
    # removed so the next build cannot link it.
    registered = set(RUNNER_CASE.findall(open(out, encoding="utf-8").read()))
    over = sorted(registered - set(runner_cases(src)[0]))
    if over:
        os.remove(out)
        raise SystemExit(
            "runners: %s registers %s, which is not a case.\n"
            "  Unity's generator collects on the name alone, so a helper called test_<something>\n"
            "  is registered and then called with no arguments. A case takes (void) and nothing\n"
            "  else - rename the helper, or mark it static." % (os.path.relpath(src, ROOT), ", ".join(over))
        )
    return out


def cmd_runners_gen(a):
    for d in a.suite:
        full = d if os.path.isabs(d) else os.path.join(ROOT, d)
        out = generate_runner(full)
        if not out:
            print(
                "no runner for %s\n"
                "  This is the bridge to Unity's generate_test_runner.rb, and it found nothing to\n"
                "  register. A suite is <dir>/test_<name>.c holding file-scope\n"
                "  `void test_<case>(void)` functions plus setUp/tearDown; the generator collects\n"
                "  those and writes %s beside them.\n"
                "  Write the cases first, then run this, then `harness.py run <env>`." % (d, GENERATED_RUNNER)
            )
            continue
        src = suite_source(full)
        cases, missed = runner_cases(src) if src else ([], [])
        print("generated %s (%d cases)" % (os.path.relpath(out, ROOT), len(cases)))
        if missed:
            print(
                "  NOT REGISTERED, and so never run: %s\n"
                "  Unity's generator collects file-scope `void test_<name>(void)` only. Rename each\n"
                "  to test_<name>, or the suite passes without them." % ", ".join(missed)
            )
    return 0


def ensure_keys(if_absent=True):
    args = [sys.executable, "-m", "tools.crypto.gen_ssh_test_keys"]
    if if_absent:
        args.append("--if-absent")
    subprocess.run(args, cwd=ROOT, check=True)


def cmd_keys_ensure(a):
    ensure_keys(if_absent=not a.force)
    print("ssh test host key in place")
    return 0


# ---------------------------------------------------------------------------
# report merge / stable
# ---------------------------------------------------------------------------

ROW_RE = re.compile(r"^\|\s*`(test_[^`]+)`\s*\|\s*`(native[^`]*)`\s*\|")
SEC_RE = re.compile(r"^##\s+(test_\S+)\s+-\s+(native\S*)\s+-\s")
CNT_RE = re.compile(r"(\d+)\s+passed(?:,\s+(\d+)\s+failed)?")
DUR_RE = re.compile(r"(\d+):(\d+):(\d+(?:\.\d+)?)\s*\|?\s*$")

GENERATED_RE = re.compile(r"^\*\*Generated:\*\*.*$")
RESULT_SECS = re.compile(r"(\*\*Result:\*\*.*?)\s*-\s*\d+s\s*$")
SUMMARY_ROW = re.compile(r"^(\|.*\|.*\|.*\|.*\|)\s*[0-9:.]+\s*\|\s*$")


def report_parse(text):
    from collections import OrderedDict

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


def cmd_report_merge(a):
    import datetime

    committed = open(a.committed, encoding="utf-8").read()
    partial = open(a.partial, encoding="utf-8").read()
    head, intro, rows, mid, sections = report_parse(committed)
    _, _, p_rows, _, p_sections = report_parse(partial)
    for key, row in p_rows.items():
        rows[key] = row
    for key, sec in p_sections.items():
        sections[key] = sec
    passed = failed = 0
    for sec in sections.values():
        m = CNT_RE.search(sec.splitlines()[0])
        if m:
            passed += int(m.group(1))
            failed += int(m.group(2)) if m.group(2) else 0
    total_secs = int(round(sum(dur_secs(r) for r in rows.values())))
    icon = "\u274c" if failed else "\u2705"
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
    open(a.out, "w", encoding="utf-8", newline="\n").write("\n".join(out).rstrip("\n") + "\n")
    print(f"merged {len(p_sections)} suite(s) into {a.out}: {len(sections)} suites, {passed} passed, {failed} failed")
    return 0


def cmd_report_stable(a):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    with open(a.path, encoding="utf-8") as fh:
        text = fh.read()
    out = []
    for ln in text.splitlines():
        if GENERATED_RE.match(ln):
            ln = "**Generated:**"
        else:
            ln = RESULT_SECS.sub(r"\1", ln)
            m = SUMMARY_ROW.match(ln)
            if m:
                ln = m.group(1) + " |"
        out.append(ln)
    sys.stdout.write("\n".join(out) + "\n")
    return 0


# ---------------------------------------------------------------------------
# readme: the generated sections of test/README.md
# ---------------------------------------------------------------------------

README = findroot.at("test", "README.md")
ENV_BEGIN = "<!-- BEGIN GENERATED test-environments (edit test/test_matrix.json, run test/harness.py readme gen) -->"
ENV_END = "<!-- END GENERATED test-environments -->"
DIR_BEGIN = "<!-- BEGIN GENERATED test-directory (run test/harness.py readme gen) -->"
DIR_END = "<!-- END GENERATED test-directory -->"


def _cell(text):
    return re.sub(r"\s+", " ", (text or "").replace("|", "\\|")).strip()


def _first_sentence(desc):
    desc = _cell(desc)
    m = re.match(r"(.+?[.!?])(\s|$)", desc)
    out = m.group(1) if m else desc
    return (out[:200] + "...") if len(out) > 203 else out


def build_env_table():
    matrix = json.load(open(TABLE, encoding="utf-8"))
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
        "[harness.py](harness.py). Each compiles a strict per-feature slice of `src/` with its "
        "own flags and runs that feature's suite in isolation, so \"this feature builds and tests on its "
        'own" stays guaranteed.\n',
        "| Environment | Feature flag(s) | Test suite(s) | Purpose |",
        "| :--- | :--- | :--- | :--- |",
    ]
    out.extend(rows)
    return "\n".join(out)


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
    import glob as _glob

    # Suites live at any depth under test/ and are C, not C++: test/unit/<area>/test_x/test_x.c.
    suites = {}
    for fp in sorted(_glob.glob(os.path.join(ROOT, "test", "**", "test_*.c"), recursive=True)):
        if os.path.basename(fp) == GENERATED_RUNNER:
            continue
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


def marker_splice(text, begin, end, body, label):
    i = text.find(begin)
    j = text.find(end)
    if i == -1 or j == -1 or j < i:
        sys.exit(f"error: markers for '{label}' not found in test/README.md")
    return text[: i + len(begin)] + "\n\n" + body.rstrip() + "\n\n" + text[j:]


def cmd_readme_gen(a):
    text = open(README, encoding="utf-8").read()
    new = marker_splice(text, ENV_BEGIN, ENV_END, build_env_table(), "test-environments")
    new = marker_splice(new, DIR_BEGIN, DIR_END, build_test_directory(), "test-directory")
    if a.check:
        if new != text:
            sys.exit("test/README.md is out of date; run: harness.py readme gen")
        print("test/README.md is up to date.")
        return 0
    open(README, "w", encoding="utf-8", newline="\n").write(new)
    print("regenerated test/README.md generated sections (test-environments, test-directory).")
    return 0


# ---------------------------------------------------------------------------
# run: build and run an env natively, without pio
# ---------------------------------------------------------------------------


def unity_src():
    """Unity's own sources, from any env's libdeps."""
    base = os.path.join(ROOT, ".pio", "libdeps")
    if not os.path.isdir(base):
        return None
    for d in sorted(os.listdir(base)):
        c = os.path.join(base, d, "Unity", "src")
        if os.path.isfile(os.path.join(c, "unity.c")):
            return c
    return None


def suite_dirs(env_entry):
    return [os.path.join(ROOT, "test", *t.split("/")) for t in env_entry["tests"]]


def inherited(envs, name, key, seen=None):
    """An env's @p key with its base chain in front of it.

    A stack base carries the flags and the sources for the envs that extend it and owns no suite of
    its own, so an env that names `base: env:native_stack_l46` and no src of its own gets everything
    from there. `gen` walks that chain to write `extends =`; a direct compile has to walk it too, or
    it builds the suite against nothing and every namespace the suite drives comes back undefined.
    The base goes first so the env's own entries are the later word.
    """
    seen = seen or set()
    if name in seen or name not in envs:
        return []
    seen.add(name)
    e = envs[name]
    base = e.get("base", "")
    out = []
    if base.startswith("env:"):
        out = inherited(envs, base[len("env:") :], key, seen)
    return out + list(e.get(key, []))


def lib_packages(envname):
    """A lib_dep's include dir and sources, which `pio` passes and a direct compile does not.

    Unity is handled on its own because every env links it. Anything else under an env's libdeps is
    a package the env asked for: littlefs is the one in the tree, reached through the host mount
    mock. The include dir costs nothing to add, so it always is; the sources are only compiled when
    the suite actually reaches the package, because pio installs a package per env whether that
    env's suite uses it or not.
    """
    base = os.path.join(ROOT, ".pio", "libdeps", envname)
    if not os.path.isdir(base):
        return [], []
    incs, pkgs = [], []
    for d in sorted(os.listdir(base)):
        if d == "Unity":
            continue
        inc = os.path.join(base, d, "include")
        src = os.path.join(base, d, "src")
        if not os.path.isdir(inc) or not os.path.isdir(src):
            continue
        incs.append(inc)
        heads = [h for h in os.listdir(inc) if h.endswith(".h")]
        srcs = [os.path.join(src, f) for f in sorted(os.listdir(src)) if f.endswith(".c")]
        pkgs.append((heads, srcs))
    return incs, pkgs


def _reached_headers(sdir):
    """Header names a suite includes, one level on from the host mocks it names.

    A suite reaches littlefs through test/core_setup/hal/host/lfs_mock.h rather than by naming lfs.h, so
    the includes of the headers it does name are read too.
    """
    names = set()
    text = []
    try:
        for f in os.listdir(sdir):
            if f.endswith((".c", ".h")):
                text.append(open(os.path.join(sdir, f), encoding="utf-8", errors="replace").read())
    except OSError:
        return names
    hal = os.path.join(ROOT, "test", "core_setup", "hal", "host")
    seen = set()
    i = 0
    while i < len(text):  # appended headers are scanned too, which is what "one level on" means
        t = text[i]
        i += 1
        for m in re.findall(r'#\s*include\s*[<"]([^">]+)[">]', t):
            base = os.path.basename(m)
            names.add(base)
            cand = os.path.join(hal, base)
            if base not in seen and os.path.isfile(cand):
                seen.add(base)
                text.append(open(cand, encoding="utf-8", errors="replace").read())
    return names


COV_BUILD = ".pio_cov"  # per-env .gcno/.gcda from the instrumented build
COV_REPORTS = "coverage_reports"  # per-env gcovr tracefiles, unioned once every env has run


def gcovr_cmd():
    """A runnable gcovr: the interpreter that can `-m gcovr`, else the console script.

    The interpreter running this harness is often PlatformIO's venv, which has no gcovr, so
    sys.executable alone silently produces no report.
    """
    for cand in (sys.executable, shutil.which("python3"), shutil.which("python")):
        if not cand:
            continue
        p = subprocess.run([cand, "-m", "gcovr", "--version"], capture_output=True, text=True)
        if p.returncode == 0:
            return [cand, "-m", "gcovr"]
    exe = shutil.which("gcovr")
    if exe:
        return [exe]
    return None


def _obj_for(objdir, src):
    """One object per source, named after its whole path: basenames repeat across src/."""
    return os.path.join(objdir, src.replace("/", "_").replace("\\", "_")[:-2] + ".o")


_OWED = None


def owed_includes(srcs):
    """`-include` args for what a yanked header no longer carries (test/yanked_includes.json).

    tools/dev_env/yank_includes.py takes an include out of a header and records, per source file,
    what that file now owes. CMake forces it onto exactly that file; this path compiles an env in one
    command, so the union over the env's sources is forced instead. An include guard makes the wider
    reach a no-op, and a file that already had it is not listed.
    """
    global _OWED
    if _OWED is None:
        p = os.path.join(ROOT, "test", "yanked_includes.json")
        try:
            with open(p, encoding="utf-8") as f:
                _OWED = json.load(f).get("owed", {})
        except (OSError, ValueError):
            _OWED = {}
    if not _OWED:
        return []
    want = []
    for s in srcs:
        for w in _OWED.get(s.replace("\\", "/"), []):
            h = w.strip('"<>')
            if h not in want:
                want.append(h)
    out = []
    for h in want:
        out += ["-include", h]
    return out


def build_and_run(name, e, jobs, keep, verbose, debug=False, coverage=False):
    """Compile an env's sources + its suite into one binary and run it. Returns (rc, output).

    Under coverage each translation unit is compiled to its own object under .pio_cov/<env>/ so the
    .gcno files land somewhere gcovr can find, and the .gcda the run writes land beside them. The
    plain path stays one compile-and-link command.
    """
    # The ccache masquerade dir first: it keys on the preprocessed source and flags, so the library
    # and Unity compiles repeated across envs hit the cache instead of running the compiler again.
    cc = None
    for c in ("/usr/lib/ccache/gcc", "/usr/lib/ccache/cc"):
        if os.path.isfile(c):
            cc = c
            break
    cc = cc or shutil.which("gcc") or shutil.which("cc")
    if not cc:
        return 1, "no gcc on PATH"
    usrc = unity_src()
    if not usrc:
        return 1, "Unity sources not found under .pio/libdeps - run `pio pkg install` once"
    incs, defs = _flag_split(e["flags"])
    incs.append("-I" + os.path.relpath(usrc, ROOT).replace("\\", "/"))
    lib_incs, lib_pkgs = lib_packages(name)
    for d in lib_incs:
        incs.append("-I" + os.path.relpath(d, ROOT).replace("\\", "/"))
    tus = _resolve_src(e["src"])
    tus.append(os.path.relpath(os.path.join(usrc, "unity.c"), ROOT).replace("\\", "/"))
    out_lines = []
    rc_total = 0
    for sd in suite_dirs(e):
        if not os.path.isdir(sd):
            out_lines.append("missing suite dir: " + os.path.relpath(sd, ROOT))
            rc_total = 1
            continue
        cases = [f for f in sorted(os.listdir(sd)) if f.endswith(".c") and f != GENERATED_RUNNER]
        srcs = list(tus) + [os.path.relpath(os.path.join(sd, f), ROOT).replace("\\", "/") for f in cases]
        # A lib_dep the suite reaches has to be compiled in: pio links it, a direct build does not.
        reached = _reached_headers(sd)
        for heads, lib_srcs in lib_pkgs:
            if reached & set(heads):
                srcs += [os.path.relpath(s, ROOT).replace("\\", "/") for s in lib_srcs]
        # A suite with no main() of its own is registered by Unity's generator, the same way the
        # PlatformIO hook does it.
        has_main = any("int main(" in open(os.path.join(sd, f), encoding="utf-8").read() for f in cases)
        if not has_main:
            gen = generate_runner(sd)
            if gen:
                srcs.append(os.path.relpath(gen, ROOT).replace("\\", "/"))
        exe = os.path.join(ROOT, ".pio", "native", name + ".exe")
        os.makedirs(os.path.dirname(exe), exist_ok=True)
        opt = ["-g", "-O0"] if debug else ["-O1"]
        # Coverage counters are what is being measured, so the optimizer stays out of the way.
        if coverage:
            opt = ["-g", "-O0", "--coverage"]
        base = (
            [cc, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-fno-exceptions"]
            + opt
            + defs
            + incs
            + ["-I" + os.path.relpath(sd, ROOT).replace("\\", "/")]
            + owed_includes(srcs)
        )
        if coverage:
            objdir = os.path.join(ROOT, COV_BUILD, name)
            os.makedirs(objdir, exist_ok=True)
            objs = []
            failed = None
            for src in srcs:
                obj = _obj_for(objdir, src)
                c = base + ["-c", src, "-o", obj]
                if verbose:
                    out_lines.append(" ".join(c))
                p = subprocess.run(c, capture_output=True, text=True, cwd=ROOT)
                if p.returncode != 0:
                    failed = "BUILD FAILED %s (%s)\n%s" % (name, src, p.stderr.strip())
                    break
                objs.append(obj)
            if failed:
                out_lines.append(failed)
                rc_total = 1
                continue
            cmd = [cc, "--coverage"] + objs + ["-o", exe, "-lm"]
        else:
            cmd = base + srcs + ["-o", exe, "-lm"]
        if verbose:
            out_lines.append(" ".join(cmd))
        b = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
        if b.returncode != 0:
            out_lines.append("BUILD FAILED %s\n%s" % (name, b.stderr.strip()))
            rc_total = 1
            continue
        r = subprocess.run([exe], capture_output=True, text=True, cwd=ROOT)
        out_lines.append(r.stdout.strip())
        if r.returncode != 0:
            # A suite that dies mid-run prints no Unity line to read and says nothing on stdout, so
            # without the exit status and stderr the env fails with an empty record and the only way
            # to find out which suite it was is to run them again one at a time.
            note = "RUN FAILED %s (%s) exit=%d" % (name, os.path.relpath(sd, ROOT).replace("\\", "/"), r.returncode)
            if r.stderr.strip():
                note += "\n" + r.stderr.strip()
            out_lines.append(note)
            rc_total = 1
        if not keep:
            try:
                os.unlink(exe)
            except OSError:
                pass
    return rc_total, "\n".join(out_lines)


UNITY_LINE = re.compile(r"^(?P<file>[^:]*[/\\][^:]+\.c):(?P<line>\d+):(?P<test>[A-Za-z_]\w*):(?P<st>PASS|FAIL|IGNORE)")


def parse_unity(text):
    """Per-test results out of a suite's Unity output: [(suite, test, status)]."""
    out = []
    for ln in text.splitlines():
        m = UNITY_LINE.match(ln.strip())
        if not m:
            continue
        suite = os.path.basename(os.path.dirname(m.group("file").replace("\\", "/")))
        out.append((suite, m.group("test"), m.group("st")))
    return out


def describe_suite(suite):
    """fn_name -> humanized description for one suite, from its own source."""
    import glob as _glob

    for fp in _glob.glob(os.path.join(ROOT, "test", "**", suite, "*.c"), recursive=True):
        if os.path.basename(fp) == GENERATED_RUNNER:
            continue
        cases = parse_test_file(fp)
        if cases:
            return {c["fn_name"]: c["description"] for c in cases}
    return {}


def write_report(path, results, envs_run, passed, failed, secs):
    """The run's TEST_REPORT.md: a header, then one collapsible section per suite."""
    mark = "✅" if not failed else "❌"
    md = [
        "# Test Report",
        "",
        "**Generated:** " + time.strftime("%Y-%m-%d %H:%M:%S"),
        "**Command:** `harness.py run` over %d native envs" % envs_run,
        "**Result:** %s %d passed, %d failed - %ds" % (mark, passed, failed, secs),
        "",
        "---",
        "",
        "## Summary",
        "",
        "| Suite | Environment | Tests | Status | Duration |",
        "| :---- | :---------- | ----: | :----: | -------: |",
    ]
    for env, suite, cases in results:
        nf = sum(1 for _, s in cases if s == "FAIL")
        md.append("| %s | %s | %d | %s | - |" % (suite, env, len(cases), "✅" if not nf else "❌"))
    md.append("")
    for env, suite, cases in results:
        nf = sum(1 for _, s in cases if s == "FAIL")
        head = "✅ %d passed" % len(cases) if not nf else "❌ %d of %d failed" % (nf, len(cases))
        desc = describe_suite(suite)
        md += [
            "---",
            "",
            "## %s - %s - %s" % (suite, env, head),
            "",
            "<details>",
            "<summary><b>Expand Suite Details</b></summary>",
            "",
            "|   # | Test | Status | Description |",
            "| --: | :--- | :----: | :---------- |",
        ]
        for i, (test, st) in enumerate(cases, 1):
            icon = {"PASS": "✅", "FAIL": "❌", "IGNORE": "⚠️"}[st]
            md.append("| %3d | `%s` | %s | %s |" % (i, test, icon, _cell(desc.get(test, clean_name(test)))))
        md += ["", "</details>", ""]
    full = os.path.join(ROOT, *path.split("/")) if not os.path.isabs(path) else path
    with open(full, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(md) + "\n")


def cov_one(gc, name):
    """gcovr one env's instrumented build into a tracefile. True when it produced one.

    JSON, not the SonarQube format: that format keeps only a per-line (branchesToCover,
    coveredBranches) aggregate, so two envs covering different branches of one condition cannot be
    unioned. gcovr's JSON keeps the per-branch counts --add-tracefile needs.
    """
    os.makedirs(os.path.join(ROOT, COV_REPORTS), exist_ok=True)
    js = "%s/%s.json" % (COV_REPORTS, name)
    p = subprocess.run(
        gc
        + [
            "--root",
            ".",
            "--filter",
            "src/.*",
            "--gcov-ignore-parse-errors",
            "--json",
            js,
            "%s/%s" % (COV_BUILD, name),
        ],
        capture_output=True,
        text=True,
        cwd=ROOT,
    )
    full = os.path.join(ROOT, js)
    return p.returncode == 0 and os.path.isfile(full) and os.path.getsize(full) > 0


def cov_merge(gc, out_rel):
    """Union every per-env tracefile into one SonarQube report, then fold duplicate lines.

    merge-mode-functions=separate, not the default strict: a build knob can put two definitions of
    one function in one file, and envs compiling different arms report it at different lines, which
    strict calls a conflict. separate keeps one entry per definition. It also emits a header's
    inline lines once per including TU, and the generic format requires each line once per file, so
    the dedupe pass folds those.
    """
    out = os.path.join(ROOT, *out_rel.split("/"))
    p = subprocess.run(
        gc + ["--add-tracefile", "%s/*.json" % COV_REPORTS, "--merge-mode-functions=separate", "--sonarqube", out],
        capture_output=True,
        text=True,
        cwd=ROOT,
    )
    if p.returncode != 0 or not os.path.isfile(out):
        print((p.stdout or "")[-2000:] + (p.stderr or "")[-2000:], file=sys.stderr)
        return False
    sys.path.insert(0, ROOT)
    from tools.ci_tooling.coverage import dedupe_sonar_cov

    dedupe_sonar_cov.dedupe(out)
    return True


def find_pio():
    """PlatformIO Core, from PATH or the places its installers put it."""
    p = shutil.which("pio") or shutil.which("platformio")
    if p:
        return p
    home = os.path.expanduser("~")
    for c in (
        os.path.join(home, ".platformio", "penv", "Scripts", "pio.exe"),
        os.path.join(home, ".platformio", "penv", "bin", "pio"),
        os.path.join(home, ".pio-venv", "bin", "pio"),
        os.path.join(home, ".local", "bin", "pio"),
    ):
        if os.path.isfile(c):
            return c
    return None


def run_pio(a):
    """Run envs through `pio test`, one env per invocation."""
    pio = find_pio()
    if not pio:
        print("error: PlatformIO Core not found - install it or put pio on PATH")
        return 1
    with open(TABLE, encoding="utf-8") as f:
        table = json.load(f)["envs"]
    names = a.envs or [n for n, e in table.items() if e.get("tests") and n not in NEVER_SELECT]
    failed = []
    total = len(names)
    for i, name in enumerate(names, 1):
        r = subprocess.run([pio, "test", "-e", name], capture_output=True, text=True, cwd=ROOT)
        status = "PASS" if r.returncode == 0 else "FAIL"
        print("[%d/%d] %-34s %s" % (i, total, name, status))
        if r.returncode != 0 or a.verbose:
            print(r.stdout.strip())
            if r.stderr.strip():
                print(r.stderr.strip())
        if r.returncode != 0:
            failed.append(name)
    print("\n%d/%d envs passed" % (total - len(failed), total))
    if failed:
        print("failed: " + " ".join(failed))
    return 1 if failed else 0


# ---------------------------------------------------------------------------
# bare: hand off to the bare-metal tool
# ---------------------------------------------------------------------------


def cmd_bare(a):
    """Run test/bare.py with whatever followed `bare` on the command line.

    The bare-metal build is its own tool because it answers a different question than the native
    suite: not "do the tests pass" but "what does an image owe that no library provides", and then
    whether it runs on the part. It is reached from here so the harness stays the one launcher, and
    its arch table, its runtime list and its help live in one file rather than two.
    """
    import bare  # here, not at import time: bare.py imports this module

    return bare.main(a.rest)


def cmd_bench(a):
    """Run test/performance_benching/bench.py with whatever followed `bench`.

    Same reason as `bare`: the microbenchmarks are their own matrix and their own tool, but nothing
    should have to know that to run them. One entry point is what keeps a session's knowledge of how
    to drive the tests from being re-derived every time.
    """
    sys.path.insert(0, os.path.join(ROOT, "test", "performance_benching"))
    import bench  # here, not at import time: bench.py imports this module

    return bench.main(a.rest)


def run_cmake(a):
    """Build the matrix with CMake and run it with ctest.

    Regenerates test/CMakeLists.txt from the matrix through tools/harness.py, configures
    build/native if it is not there, builds keep-going so every failing target is named in one
    pass, then runs ctest. A ctest name is a target name: <env>, or <env>__<suite> where an env
    runs several. Named envs select by that prefix.
    """
    tools = os.path.join(ROOT, "tools", "harness.py")
    build = os.path.join(ROOT, "build", "native")
    jobs = ["-j", str(a.jobs)] if a.jobs else ["-j"]

    r = subprocess.run([sys.executable, tools, "build", "cmake"], cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout + r.stderr)
        return r.returncode
    print(r.stdout.strip().splitlines()[-1] if r.stdout.strip() else "test/CMakeLists.txt regenerated")

    if not os.path.exists(os.path.join(build, "CMakeCache.txt")):
        r = subprocess.run(["cmake", "-S", os.path.join(ROOT, "test"), "-B", build], cwd=ROOT)
        if r.returncode != 0:
            return r.returncode

    r = subprocess.run(
        ["cmake", "--build", build] + jobs + ["--", "-k"], cwd=ROOT, capture_output=not a.verbose, text=True
    )
    if r.returncode != 0:
        broke = sorted(set(re.findall(r"CMakeFiles/(\w+)\.dir/all\] Error", (r.stdout or "") + (r.stderr or ""))))
        print("%d target(s) did not build: %s" % (len(broke), " ".join(broke)))
        print("  cmake --build build/native -j -- -k     to read the errors")

    cmd = ["ctest", "--test-dir", build, "--output-on-failure"] + jobs
    if a.envs:
        cmd += ["-R", "^(%s)(__|$)" % "|".join(re.escape(n) for n in a.envs)]
    return subprocess.run(cmd, cwd=ROOT).returncode


def cmd_run(a):
    if a.pio:
        return run_pio(a)
    # --debug, --coverage and --report-out each read one env's own build or its own Unity output,
    # which only the direct compile below produces.
    if not (a.direct or a.debug or a.coverage or a.report_out):
        return run_cmake(a)
    with open(TABLE, encoding="utf-8") as f:
        table = json.load(f)["envs"]
    names = a.envs or [n for n, e in table.items() if e.get("tests") and n not in NEVER_SELECT]
    envs = parse_ini_envs(INI)
    gc = None
    if a.coverage:
        gc = gcovr_cmd()
        if not gc:
            print("error: no gcovr (tried `-m gcovr` on this interpreter, python3, python, and PATH)")
            return 3
        shutil.rmtree(os.path.join(ROOT, COV_REPORTS), ignore_errors=True)
        shutil.rmtree(os.path.join(ROOT, COV_BUILD), ignore_errors=True)
    failed = []
    cov_failed = []
    results = []
    n_pass = n_fail = 0
    t0 = time.time()
    total = len(names)
    for i, name in enumerate(names, 1):
        e = envs.get(name)
        if not e:
            print("unknown env: %s" % name)
            failed.append(name)
            continue
        rc, out = build_and_run(name, e, a.jobs, a.keep or a.debug, a.verbose, a.debug, a.coverage)
        status = "PASS" if rc == 0 else "FAIL"
        print("[%d/%d] %-34s %s" % (i, total, name, status))
        if rc != 0 or a.verbose:
            print(out)
        if rc != 0:
            failed.append(name)
        if a.report_out:
            by_suite = {}
            for suite, test, st in parse_unity(out):
                by_suite.setdefault(suite, []).append((test, st))
                n_pass += st == "PASS"
                n_fail += st == "FAIL"
            for suite, cases in by_suite.items():
                results.append((name, suite, cases))
        if a.coverage and not cov_one(gc, name):
            print("  ERROR: gcovr produced no coverage report for %s" % name, file=sys.stderr)
            cov_failed.append(name)
    print("\n%d/%d envs passed" % (total - len(failed), total))
    if failed:
        print("failed: " + " ".join(failed))
    if a.report_out:
        write_report(a.report_out, results, total, n_pass, n_fail, int(time.time() - t0))
        print("Report written: %s" % a.report_out)
    if a.coverage:
        # A silently-missing per-env tracefile is the worst failure here: the merge only unions, so
        # an env that stops contributing freezes its files at whatever the baseline last said
        # instead of lowering the number. Refuse the merge rather than publish that.
        if cov_failed:
            print(
                "ERROR: no coverage report from %d env(s): %s" % (len(cov_failed), " ".join(cov_failed)),
                file=sys.stderr,
            )
            print("Refusing to merge - a partial merge would silently freeze those files' coverage.", file=sys.stderr)
            shutil.rmtree(os.path.join(ROOT, COV_REPORTS), ignore_errors=True)
            return 3  # distinct from a test failure: the caller may commit a report but not this
        if not cov_merge(gc, a.coverage_out):
            return 3
        shutil.rmtree(os.path.join(ROOT, COV_REPORTS), ignore_errors=True)
        print("Coverage written: %s" % a.coverage_out)
    return 1 if failed else 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def _subcommands(parser):
    """{name: subparser} for a parser's one subparser group."""
    for action in parser._actions:
        if isinstance(action, argparse._SubParsersAction):
            return action.choices
    return {}


CORE_SETUP = findroot.at("test/core_setup")


def _brief_of(path):
    """The file's own @brief, which is where every file under core_setup says what it supplies."""
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            head = fh.read(4096)
    except OSError:
        return ""
    m = re.search(r"@brief\s+(.+)", head)
    if not m:
        return ""
    text = m.group(1).strip()
    # A brief that wraps continues on the next comment line until the first blank one.
    for line in head[m.end() :].split("\n")[1:]:
        line = line.strip().lstrip("*").strip()
        if not line or line.startswith("@"):
            break
        text += " " + line
    return " ".join(text.split())


def print_core_setup_tree(width=96):
    """The core_setup tree and what each file supplies.

    src/ reaches this layer only through protocore_config.h, so nothing under src/ names a path here
    and the arms a build actually links are not visible from a call site. This is where they are
    listed: the platform and board profiles that identify the die, the register drivers that own each
    accelerator, and the host arms that answer the same contracts in software so an accelerated arm is
    runnable and testable off target.
    """
    print("test/core_setup/ - the platform layer, reached from src/ only through protocore_config.h")
    for dirpath, dirnames, filenames in os.walk(CORE_SETUP):
        dirnames.sort()
        rel = os.path.relpath(dirpath, ROOT).replace("\\", "/")
        names = sorted(f for f in filenames if f.endswith((".c", ".h", ".cpp", ".S")))
        if not names:
            continue
        depth = rel.count("/")
        print("\n%s%s/" % ("  " * depth, rel.rsplit("/", 1)[-1] if depth else rel))
        pad = "  " * (depth + 1)
        wide = max(len(n) for n in names)
        for n in names:
            brief = _brief_of(os.path.join(dirpath, n))
            room = width - len(pad) - wide - 3
            if len(brief) > room > 3:
                brief = brief[: room - 3] + "..."
            print("%s%-*s   %s" % (pad, wide, n, brief))


def cmd_help(a):
    """Every command's own help in one call, or one command's.

    `-h` prints usage one level at a time, so reading the whole surface means invoking it once per
    subcommand, and the nested groups make that three levels deep. This prints all of it, and the
    core_setup tree after it, because that layer is invisible from any call site under src/.
    """
    subs = _subcommands(build_parser())
    if a.command:
        if a.command not in subs:
            print("no such command: %s (try `harness.py help`)" % a.command, file=sys.stderr)
            return 2
        subs[a.command].print_help()
        for name, nested in sorted(_subcommands(subs[a.command]).items()):
            print("\n### harness.py %s %s" % (a.command, name))
            print(nested.format_help().strip())
        return 0
    print(__doc__.strip())
    for name in sorted(subs):
        print("\n" + "-" * 78)
        print("### harness.py %s" % name)
        print(subs[name].format_help().strip())
        for nested_name, nested in sorted(_subcommands(subs[name]).items()):
            print("\n  ### harness.py %s %s" % (name, nested_name))
            print(nested.format_help().strip())
    print("\n" + "-" * 78)
    print_core_setup_tree()
    return 0


def build_parser():
    ap = argparse.ArgumentParser(
        prog="harness.py",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = ap.add_subparsers(dest="group", required=True)

    p = sub.add_parser("help", help="every command's help in one call, or one command's")
    p.add_argument("command", nargs="?")
    p.set_defaults(fn=cmd_help)

    # env ---------------------------------------------------------------
    env = sub.add_parser(
        "env",
        help="the native test matrix",
        description="Every mutation of test/test_matrix.json goes through one of these. NEVER "
        "rewrite the file with json.dump - a whole-file re-render is valid JSON and reformats all "
        "400 envs, so the diff stops naming what changed. If the change you need has no subcommand "
        "here, add one; a script that must touch the table imports this module and calls "
        "splice_replace / splice_after / splice_remove then write_verified.",
    ).add_subparsers(dest="cmd", required=True)

    p = env.add_parser("add", help="splice a new env into the matrix")
    p.add_argument("name")
    p.add_argument("--after", required=True, help="env to insert after")
    p.add_argument("--tests", nargs="+", default=[], help="test_filter entries; derived from --src when omitted")
    p.add_argument("--clone", help="env whose flags and src to COPY (the new env stands alone)")
    p.add_argument(
        "--base",
        help="stack env to EXTEND (e.g. native_stack_l46): its flags and build_src_filter are "
        "inherited through the ini, so the new env states only what it adds. Use this, not "
        "--clone, when the module needs a whole stack built around it.",
    )
    p.add_argument("--src", action="extend", nargs="*", default=[], help="repo-relative .c paths to build")
    # Attached form, one per flag: --flags=-DPROTOCORE_ENABLE_X=1 (a bare -D reads as an option).
    p.add_argument("--flags", action="append", default=[], metavar="=-DNAME=VALUE")
    p.add_argument("--extra-scripts", action="extend", nargs="*", default=[], dest="extra_scripts")
    p.add_argument("--desc", default="")
    p.add_argument("--only", action="store_true", help="build only --src, nothing else")
    p.set_defaults(fn=cmd_env_add)

    p = env.add_parser("update", help="change an existing env")
    p.add_argument("name")
    p.add_argument("--src", action="extend", nargs="*", default=[], help="repo-relative .c paths to add")
    p.add_argument("--drop-src", action="extend", nargs="*", default=[], dest="drop_src")
    # A flag value starts with '-', which argparse reads as an option, so these take one value at a
    # time in the attached form: --flags=-DPROTOCORE_ENABLE_MNT=1, repeated per flag.
    p.add_argument("--flags", action="append", default=[], metavar="=-DNAME=VALUE")
    p.add_argument("--drop-flags", action="append", default=[], dest="drop_flags", metavar="=-DNAME=VALUE")
    p.add_argument("--tests", action="extend", nargs="*", default=[])
    p.add_argument("--drop-tests", action="extend", nargs="*", default=[], dest="drop_tests")
    p.add_argument("--extra-scripts", action="extend", nargs="*", default=[], dest="extra_scripts")
    p.add_argument("--drop-extra-scripts", action="extend", nargs="*", default=[], dest="drop_extra_scripts")
    p.add_argument("--desc", default=None)
    p.add_argument(
        "--base",
        default=None,
        help="stack env to EXTEND from now on (e.g. native_stack_http): its flags and "
        "build_src_filter are inherited through the ini, so this env states only what it adds. "
        'Pass "" to stand alone again. Use this when a module stops being gated out and starts '
        "needing the stack built around it.",
    )
    p.set_defaults(fn=cmd_env_update)

    p = env.add_parser(
        "addsrc",
        help="add the same deps to every env that builds a given .c",
        description="A module that starts taking a borrow needs the pool, the arena and the "
        "platform in every env that already builds it. This adds them everywhere in one splice, "
        "so no session has to write a script that rewrites the table.",
    )
    p.add_argument("builds", help="the .c whose envs get the deps, e.g. udp/client/client.c")
    p.add_argument("--dep", action="extend", nargs="+", required=True, help="repo-relative .c paths to add")
    p.set_defaults(fn=cmd_env_addsrc)

    p = env.add_parser(
        "addflag",
        help="add the same flags to every env that builds a given .c",
        description="Giving a module its enable gate is this: the header wraps in "
        "PROTOCORE_ENABLE_<X>, which defaults off, so every env that was compiling the module "
        "without naming it has to start naming it. One splice, not a script.",
    )
    p.add_argument("builds", help="the .c whose envs get the flags, e.g. codec/multipart/multipart.c")
    p.add_argument(
        "--flag",
        action="append",
        required=True,
        metavar="=-DNAME=VALUE",
        help="attached form, one per flag: --flag=-DPROTOCORE_ENABLE_MULTIPART=1 (a bare -D reads as an option)",
    )
    p.set_defaults(fn=cmd_env_addflag)

    p = env.add_parser("remove", help="cut envs out of the matrix")
    p.add_argument("name", nargs="+")
    p.add_argument("--force", action="store_true", help="remove even if a suite stops running anywhere")
    p.add_argument("-v", "--verbose", action="store_true", help="print each env's suites and where they still run")
    p.set_defaults(fn=cmd_env_remove)

    p = env.add_parser("gen", help="regenerate platformio.ini from the matrix")
    p.add_argument("--check", action="store_true", help="exit 1 if the ini is out of date (no write)")
    p.set_defaults(fn=cmd_env_gen)

    p = env.add_parser("select", help="map changed files to the envs they affect")
    p.add_argument("--full", action="store_true", help="force FULL regardless of the diff")
    p.add_argument("--changed-file", action="append", default=[], help="a changed path (repeatable)")
    p.add_argument(
        "--base",
        help="diff base git ref; enables content-aware classification of "
        "protocore_config.h / test_matrix.json / platformio.ini",
    )
    p.add_argument("--head", help="diff head git ref for the NEW content (default: the working tree)")
    p.set_defaults(fn=cmd_env_select)

    p = env.add_parser("list", help="print the envs the matrix defines")
    p.add_argument("-v", "--verbose", action="store_true")
    p.add_argument("--bases-only", action="store_true")
    p.add_argument("--targets-only", action="store_true")
    p.set_defaults(fn=cmd_env_list)

    p = env.add_parser("deps", help="rebuild test/dep_graph.json from the compiler include closure")
    p.add_argument("envs", nargs="*")
    p.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4)
    p.add_argument("--progress", action="store_true")
    p.set_defaults(fn=cmd_env_deps)

    # run ---------------------------------------------------------------
    b = sub.add_parser(
        "bare",
        help="hand off to test/bare.py: cross-compile the core, and boot it on the part",
        description="Everything after `bare` is passed to test/bare.py unread, so its "
        "own surface is the authority: `harness.py bare help`.",
        add_help=False,
    )
    b.add_argument("rest", nargs=argparse.REMAINDER, metavar="...")
    b.set_defaults(fn=cmd_bare)

    b = sub.add_parser(
        "bench",
        help="hand off to test/performance_benching/bench.py: the microbenchmark matrix",
        description="Everything after `bench` is passed to bench.py unread, so its "
        "own surface is the authority: `harness.py bench help`.",
        add_help=False,
    )
    b.add_argument("rest", nargs=argparse.REMAINDER, metavar="...")
    b.set_defaults(fn=cmd_bench)

    p = sub.add_parser(
        "run",
        help="build the matrix with CMake and run it with ctest",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "This calls `tools/harness.py build cmake`, builds build/native keep-going, and runs\n"
            "ctest - 0.06 s per test, and an incremental build. It is what to reach for.\n"
            "\n"
            "  harness.py run                          the whole matrix\n"
            "  harness.py run native_telnet            one env (and every suite it runs)\n"
            "\n"
            "driving one env's binary by hand:\n"
            "  harness.py run --debug <env>            build -g -O0; the binary stays at\n"
            "                                         .pio/native/<env>.exe\n"
            "  .pio/native/<env>.exe                   run it directly and read its output\n"
            "  gdb -batch -ex run -ex bt .pio/native/<env>.exe\n"
            "                                         backtrace where it died\n"
            "\n"
            "A crash reports as exit=3221225477 (0xC0000005) with no Unity output, because the\n"
            "process dies before the first case prints. The backtrace names the line; the frame\n"
            "above it is usually the caller that handed over the wrong buffer.\n"
        ),
    )
    p.add_argument("envs", nargs="*")
    p.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4)
    p.add_argument("-v", "--verbose", action="store_true")
    p.add_argument("--keep", action="store_true", help="leave the built binaries in .pio/native")
    p.add_argument("--debug", action="store_true", help="build -g -O0 and keep the binary, for gdb")
    p.add_argument(
        "--coverage", action="store_true", help="instrument, gcovr each env, union into the SonarQube coverage report"
    )
    p.add_argument(
        "--coverage-out",
        default="test/coverage.xml",
        help="where --coverage writes the report (default test/coverage.xml)",
    )
    p.add_argument("--report-out", metavar="PATH", help="write the run's TEST_REPORT.md here")
    p.add_argument("--pio", action="store_true", help="run the envs through `pio test` instead")
    p.add_argument(
        "--direct",
        action="store_true",
        help="compile each env here instead of through CMake; --debug, --coverage and --report-out imply it",
    )
    p.set_defaults(fn=cmd_run, group="run", cmd="")

    # runners / keys / report -------------------------------------------
    runners = sub.add_parser(
        "runners", help="the bridge to Unity's generate_test_runner.rb - what makes a suite runnable"
    ).add_subparsers(dest="cmd", required=True)
    p = runners.add_parser(
        "gen",
        help="write a suite's unity_runner.c",
        description="Run Unity's generate_test_runner.rb over a suite's source and write "
        "unity_runner.c beside it. This is step 2 of writing a test (see `harness.py help`): the "
        "generator registers file-scope `void test_<case>(void)` and nothing else, so any function "
        "that only looks like a case is named here rather than silently never running.",
    )
    p.add_argument("suite", nargs="+", help="suite directories, repo-relative")
    p.set_defaults(fn=cmd_runners_gen)

    keys = sub.add_parser("keys", help="test key provisioning").add_subparsers(dest="cmd", required=True)
    p = keys.add_parser("ensure")
    p.add_argument("--force", action="store_true", help="regenerate even when one is present")
    p.set_defaults(fn=cmd_keys_ensure)

    readme = sub.add_parser("readme", help="test/README.md generated sections").add_subparsers(
        dest="cmd", required=True
    )
    p = readme.add_parser("gen")
    p.add_argument("--check", action="store_true", help="exit 1 if the generated sections are stale")
    p.set_defaults(fn=cmd_readme_gen)

    report = sub.add_parser("report", help="TEST_REPORT.md operations").add_subparsers(dest="cmd", required=True)
    p = report.add_parser("merge")
    p.add_argument("committed")
    p.add_argument("partial")
    p.add_argument("out")
    p.set_defaults(fn=cmd_report_merge)
    p = report.add_parser("stable")
    p.add_argument("path")
    p.set_defaults(fn=cmd_report_stable)
    return ap


def main():
    a = build_parser().parse_args()
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
