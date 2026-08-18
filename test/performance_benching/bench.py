#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
bench.py - the one entry point for the microbenchmark matrix.

The counterpart to test/harness.py: same discipline, same shape, for
test/performance_benching instead of the native test suite. `bench.py help` is the whole surface:

  help    every command's help in one call, or one command's
  add     splice a new bench into bench_matrix.json
  update  change an existing bench's flags / src / desc
  gen     regenerate every bench project's platformio.ini from the matrix
  list    print the benches the matrix defines
  deps    fill each bench's src list from the include closure of its main.c
  run     build and run benches on the host, through core_setup/hal/host
  flash   build and upload one bench to a device, through pio

bench_matrix.json is the single source of truth. It is never hand-edited: every mutation takes the
table's lock, splices the change as text so the file is not reformatted, and re-parses to prove no
other bench moved. Each project's platformio.ini is generated - editing one is discarded by `gen`.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, "test"))

from harness import lock_acquire, lock_release, read_table, splice_after, splice_replace, write_verified  # noqa: E402

TABLE = os.path.join(HERE, "bench_matrix.json")
COMMON = os.path.join(HERE, "common")

# The per-project ini is one template: only the relative depth and the -D flags vary.
INI_HEAD = "; On-device CCOUNT microbenchmark. See performance_benching/README.md."


def render_ini(entry):
    """The project's platformio.ini, at the depth its path sits."""
    depth = len(entry["path"].split("/"))
    up = "../" * depth
    lines = [
        INI_HEAD,
        "[platformio]",
        "src_dir = main",
        "extra_configs = %scommon.ini" % up,
        "",
        "[env:esp32s3]",
        "extends = common_s3",
        "lib_deps = symlink://%s../" % up,
        "build_flags =",
        "    ${common_s3.build_flags}",
        "    -I%scommon" % up,
    ]
    lines += ["    " + f for f in entry.get("flags", [])]
    return "\n".join(lines) + "\n"


def load():
    with open(TABLE, encoding="utf-8") as fh:
        return json.load(fh)["envs"]


def bench_dir(entry):
    return os.path.join(HERE, *entry["path"].split("/"))


# ---------------------------------------------------------------------------
# add / update
# ---------------------------------------------------------------------------


def cmd_add(a):
    lock = lock_acquire(TABLE)
    if not lock:
        print("could not take the bench table lock")
        return 1
    try:
        text, before = read_table(TABLE)
        envs = before["envs"]
        if a.name in envs:
            print("bench already present:", a.name)
            return 1
        if a.after not in envs:
            print("anchor bench not found:", a.after)
            return 1
        d = os.path.join(HERE, *a.path.split("/"))
        if not os.path.isfile(os.path.join(d, "main", "main.c")):
            print("no main/main.c under test/performance_benching/%s" % a.path)
            return 1
        entry = {"desc": a.desc, "path": a.path, "flags": list(a.flags), "src": list(a.src)}
        text = splice_after(text, a.after, a.name, entry)
        rc = write_verified(TABLE, text, before, {a.name}, {a.name: entry})
        if rc == 0:
            print("added %s; run: bench.py gen" % a.name)
        return rc
    finally:
        lock_release(lock)


def cmd_rename(a):
    """Change a bench's key. A module that moves takes its bench's name with it, and the name is
    stored rather than derived, so without this the table keeps naming the tree the module left."""
    lock = lock_acquire(TABLE)
    if not lock:
        print("could not take the bench table lock")
        return 1
    try:
        text, before = read_table(TABLE)
        envs = before["envs"]
        if a.name not in envs:
            print("bench not found:", a.name)
            return 1
        if a.to in envs:
            print("bench already present:", a.to)
            return 1
        entry = json.loads(json.dumps(envs[a.name]))
        # The key is spliced as text so nothing else in the table is reformatted.
        old_key = '    "%s": {' % a.name
        if text.count(old_key) != 1:
            print("the key is not unique in the table:", a.name)
            return 1
        text = text.replace(old_key, '    "%s": {' % a.to, 1)
        after = json.loads(text)
        if after["envs"].get(a.to) != entry or a.name in after["envs"]:
            print("the rename did not round-trip:", a.name)
            return 1
        for k in envs:
            if k == a.name:
                continue
            if envs[k] != after["envs"].get(k):
                print("collateral change in", k)
                return 1
        with open(TABLE, "w", encoding="utf-8", newline="") as fh:
            fh.write(text)
        print("renamed %s -> %s; run: bench.py gen" % (a.name, a.to))
        return 0
    finally:
        lock_release(lock)


def cmd_update(a):
    lock = lock_acquire(TABLE)
    if not lock:
        print("could not take the bench table lock")
        return 1
    try:
        text, before = read_table(TABLE)
        envs = before["envs"]
        if a.name not in envs:
            print("bench not found:", a.name)
            return 1
        entry = json.loads(json.dumps(envs[a.name]))  # a copy the splice is verified against
        for key, add, drop in (("flags", a.flags, a.drop_flags), ("src", a.src, a.drop_src)):
            cur = list(entry.get(key, []))
            for v in drop:
                if v in cur:
                    cur.remove(v)
                else:
                    print("not present in %s.%s: %s" % (a.name, key, v))
            for v in add:
                if v not in cur:
                    cur.append(v)
            entry[key] = cur
        if a.desc is not None:
            entry["desc"] = a.desc
        if entry == envs[a.name]:
            print("no change:", a.name)
            return 0
        text = splice_replace(text, a.name, entry)
        rc = write_verified(TABLE, text, before, {a.name}, {a.name: entry})
        if rc == 0:
            print("updated %s; run: bench.py gen" % a.name)
        return rc
    finally:
        lock_release(lock)


# ---------------------------------------------------------------------------
# gen / list
# ---------------------------------------------------------------------------


def cmd_gen(a):
    envs = load()
    stale = 0
    wrote = 0
    for name, e in envs.items():
        d = bench_dir(e)
        if not os.path.isdir(d):
            print("missing bench dir:", e["path"])
            return 1
        p = os.path.join(d, "platformio.ini")
        want = render_ini(e)
        cur = open(p, encoding="utf-8").read() if os.path.isfile(p) else None
        if cur == want:
            continue
        if a.check:
            print("out of date:", e["path"])
            stale += 1
            continue
        open(p, "w", encoding="utf-8", newline="\n").write(want)
        wrote += 1
    if a.check:
        print("%d of %d bench ini out of date" % (stale, len(envs)))
        return 1 if stale else 0
    print("wrote %d of %d bench platformio.ini" % (wrote, len(envs)))
    return 0


def cmd_list(a):
    envs = load()
    for name, e in envs.items():
        if a.verbose:
            print("%-34s %-42s %s" % (name, e["path"], " ".join(e.get("flags", []))))
        else:
            print(name)
    return 0


# ---------------------------------------------------------------------------
# deps: what each bench has to compile
# ---------------------------------------------------------------------------


# The host branch of the platform seam. These answer the calls src/ makes through
# core_setup/board_profiles/protocore_platform.h and the crypto, NVS and PHY seams: the arm is
# chosen by vendor, so it sits nowhere near the header and no include closure reaches it. The unit
# envs name the same files in their build_src_filter; a bench's src list is derived, so they are
# named here.
#
# They go in an archive rather than on the link line, so the linker takes only the members that
# resolve something. A bench that never reaches the crypto seam then does not drag the AES arm in
# and does not have to link what the AES arm itself needs. Built per bench: a body behind a
# PROTOCORE_ENABLE_* flag is present or empty depending on what that bench defines.
HOST_ARMS = [
    "core_setup/hal/portable/portable_platform.c",
    "core_setup/hal/portable/portable_bignum.c",
    "core_setup/hal/host/host_platform.c",
    "core_setup/hal/host/host_nvs.c",
    "core_setup/hal/host/physical/physical_mock.c",
]


def host_flags(entry):
    # The host has no bounded DRAM, so the arenas are sized far past any bench's worst case, the
    # same way the test envs do it. A failed span is then a defect, not a budget.
    # include/ is where protocore.h lives. Every TU that names it compiles; what the umbrella
    # reaches is kept out of the source list by _mm_headers pruning its subtree, not by leaving the
    # path off.
    incs = [
        "-Icore_setup/hal/host",
        "-Itest/support",
        "-Isrc",
        "-Iinclude",
        "-I.",
        "-I" + os.path.relpath(COMMON, ROOT).replace("\\", "/"),
    ]
    defs = ["-DPROTOCORE_SECURE_ARENA_SIZE=262144"]
    defs += [f for f in entry.get("flags", []) if f.startswith("-D")]
    return incs, defs


# The umbrella header. It names every module in the library, so what it reaches says nothing about
# what a bench measures.
UMBRELLA = "include/protocore.h"


def _mm_headers(cc, entry, main_c):
    """The src/ headers a TU pulls in on its own account, from the compiler's include tree.

    Read as a tree (-H) rather than a flat list (-MM) so the subtree under the umbrella can be
    pruned. A bench measures one module; reaching protocore.h through any header would otherwise
    make the whole server its source list - server_gpio_map went 10 -> 288 translation units that
    way, for the same measured numbers. Pruning the subtree keeps the path available so every TU
    still compiles, and only the membership question ignores it.
    """
    incs, defs = host_flags(entry)
    cmd = [cc, "-H", "-fsyntax-only", "-std=c11", "-D_POSIX_C_SOURCE=200809L"] + defs + incs + [main_c]
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    if r.returncode != 0:
        return None, r.stderr.strip()
    out = []
    prune_depth = None
    for line in r.stderr.split("\n"):
        m = re.match(r"^(\.+)\s+(\S.*?)\s*$", line)
        if not m:
            continue
        depth = len(m.group(1))
        if prune_depth is not None:
            if depth > prune_depth:
                continue  # included by the umbrella, not by this TU
            prune_depth = None
        p = os.path.relpath(os.path.abspath(os.path.join(ROOT, m.group(2))), ROOT).replace("\\", "/")
        if p == UMBRELLA:
            prune_depth = depth
            continue
        if p.startswith("src/") and p.endswith(".h"):
            out.append(p)
    return out, None


def _siblings_of(header):
    """The other .c files in a header's own directory that include it.

    One module is not always one .c: exc_decoder.h is served by exc_decoder.c and exc_coredump.c,
    and the namespace one defines points at calls the other holds. Naming the header is what makes
    such a file part of the module, so that is the test - a .c in the same directory that does not
    include the header is a different module and is not taken.
    """
    d = os.path.dirname(header)
    base = os.path.basename(header)
    # An include names the header from an include-path root, not from the repo root: src/ is one of
    # those roots, so both spellings are accepted.
    wants = ['#include "' + header + '"', '#include "' + header[4:] + '"', '#include "' + base + '"']
    out = []
    for f in sorted(os.listdir(os.path.join(ROOT, d))):
        if not f.endswith(".c") or f == base[:-2] + ".c":
            continue
        p = (d + "/" + f).replace("\\", "/")
        with open(os.path.join(ROOT, p), "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
        if any(w in text for w in wants):
            out.append(p)
    return out


# A file-scope definition: a return type at column 0, then a name, then the parameter list. `static`
# is kept rather than skipped - a definition behind a feature gate is still the file that would
# provide the symbol, and reading it wrong costs nothing here (see symbol_index).
DEFN = re.compile(r"^[A-Za-z_][\w\s\*]*?\b(\w+)\s*\([^;]*$", re.M)

# A file-scope object: a type at column 0, a name, then an initializer. Every module exports its
# namespace as one of these (`HttpNs Http = {...}`), and a link that is missing `Http` is missing an
# object, not a function - DEFN alone never sees it and the closure stalls with the name unresolved.
OBJDEFN = re.compile(r"^[A-Za-z_][\w\s\*]*?\b(\w+)\s*(?:\[[^\]]*\])?\s*=[^=]", re.M)


def _defines(path):
    """The names a .c appears to define at file scope, read from its text.

    Compiling each source and reading nm is exact but costs minutes over the whole tree, and the
    exactness buys nothing: this table is only ever consulted for a symbol a real link has already
    reported missing, so a name it lists that the file does not actually define is never looked up.
    Reading the text is the same answer for this purpose, at a thousandth of the cost.
    """
    with open(os.path.join(ROOT, path), "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    return set(DEFN.findall(text)) | set(OBJDEFN.findall(text))


# symbol -> the src/ .c that defines it. Built once per run; the answer does not depend on which
# bench is asking. Feature gates are ignored on purpose: a symbol only ever reaches this table
# because a link reported it missing, so listing more than a given build defines cannot pull
# anything in that the linker did not ask for.
_SYM_INDEX = None


def symbol_index():
    global _SYM_INDEX
    if _SYM_INDEX is not None:
        return _SYM_INDEX
    _SYM_INDEX = {}
    for d, _dirs, files in os.walk(os.path.join(ROOT, "src")):
        for f in sorted(files):
            if not f.endswith(".c"):
                continue
            p = os.path.relpath(os.path.join(d, f), ROOT).replace("\\", "/")
            for s in _defines(p):
                _SYM_INDEX.setdefault(s, p)
    return _SYM_INDEX


def symbol_closure(cc, entry, incs, defs, srcs, out_dir, name, index, rounds=12):
    """Add the .c files that define what the link is still missing, until it is missing nothing.

    An include closure finds a module's own translation unit, and the sibling rule finds the other
    .c files of that module. Neither finds a definition that lives in a different module with no
    header of its own on the path - response.c's send_text, regex.c's regex_match, the pcap and
    uuid helpers. The linker is the only thing that knows what is actually missing, so it is asked:
    link, read the undefined names, and add whatever src/ file defines each one.
    """
    exe = os.path.join(out_dir, "_deps_" + name + ".exe")
    main_c = os.path.relpath(os.path.join(bench_dir(entry), "main", "main.c"), ROOT).replace("\\", "/")
    # Built once, not once per round: the arms depend on this bench's flags, and those do not
    # change while its source list is being closed over.
    lib, _err = host_arm_archive(cc, entry, incs, defs, out_dir, name)
    for _ in range(rounds):
        cmd = [cc, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-O0"] + defs + incs + [main_c] + srcs
        if lib:
            cmd.append(lib)
        r = subprocess.run(cmd + ["-o", exe, "-lm"], capture_output=True, text=True, cwd=ROOT)
        if r.returncode == 0:
            return srcs, []
        missing = sorted(set(re.findall(r"undefined reference to [`']([^'`]+)'", r.stderr)))
        if not missing:
            return srcs, []
        added = [index[m] for m in missing if m in index and index[m] not in srcs]
        if not added:
            return srcs, missing
        for p in added:
            if p not in srcs:
                srcs.append(p)
    return srcs, []


def cmd_deps(a):
    """A header that has a .c beside it is a translation unit the bench has to link.

    Iterated to a fixpoint: a .c pulled in for main.c has its own include closure, and the
    definitions it needs (protomem, swar, ...) are only reachable from there. Whatever the link is
    still missing after that is resolved by symbol, which is the only way to reach a definition
    that no header on the path declares.
    """
    cc = shutil.which("gcc") or shutil.which("cc")
    if not cc:
        print("no gcc on PATH")
        return 1
    envs = load()
    names = a.benches or list(envs)
    resolved = {}
    for i, name in enumerate(names, 1):
        e = envs.get(name)
        if not e:
            print("unknown bench:", name)
            return 1
        main_c = os.path.relpath(os.path.join(bench_dir(e), "main", "main.c"), ROOT).replace("\\", "/")
        srcs = []
        pending = [main_c]
        seen_tu = set()
        dropped = set()
        failed = None
        while pending:
            tu = pending.pop()
            if tu in seen_tu:
                continue
            seen_tu.add(tu)
            headers, err = _mm_headers(cc, e, tu)
            if headers is None:
                if tu == main_c:
                    failed = err
                    break
                # A TU that does not preprocess under this bench's flags contributes nothing, and
                # leaving it in the list makes the build fail on its includes. The sibling rule
                # takes every .c that names the header; symbol_closure puts back any of them the
                # link actually needs. Recorded, because the same header is reached from more than
                # one TU and the sibling rule would otherwise add it back on the next visit.
                dropped.add(tu)
                if tu in srcs:
                    srcs.remove(tu)
                continue
            for h in headers:
                c = h[:-2] + ".c"
                if not os.path.isfile(os.path.join(ROOT, c)):
                    continue
                for tu2 in [c] + _siblings_of(h):
                    if tu2 not in srcs and tu2 not in dropped:
                        srcs.append(tu2)
                        pending.append(tu2)
        if failed is not None:
            print("[%d/%d] %-30s SCAN FAILED" % (i, len(names), name))
            if a.verbose:
                print(failed)
            continue
        incs, defs = host_flags(e)
        out_dir = os.path.join(ROOT, ".pio", "bench")
        os.makedirs(out_dir, exist_ok=True)
        index = symbol_index()
        n_headers = len(srcs)
        srcs, missing = symbol_closure(cc, e, incs, defs, srcs, out_dir, name, index)
        srcs.sort()
        resolved[name] = srcs
        extra = (" (+%d by symbol)" % (len(srcs) - n_headers)) if len(srcs) > n_headers else ""
        print("[%d/%d] %-30s %d sources%s" % (i, len(names), name, len(srcs), extra))
        if missing:
            print("   %d symbol(s) nothing under src/ defines: %s" % (len(missing), ", ".join(missing[:8])))
    if a.dry_run:
        return 0
    lock = lock_acquire(TABLE)
    if not lock:
        print("could not take the bench table lock")
        return 1
    try:
        text, before = read_table(TABLE)
        expect = {}
        changed = set()
        for name, srcs in resolved.items():
            entry = json.loads(json.dumps(before["envs"][name]))
            if entry.get("src") == srcs:
                continue
            entry["src"] = srcs
            text = splice_replace(text, name, entry)
            expect[name] = entry
            changed.add(name)
        if not changed:
            print("no src lists changed")
            return 0
        rc = write_verified(TABLE, text, before, changed, expect)
        if rc == 0:
            print("filled src for %d bench(es)" % len(changed))
        return rc
    finally:
        lock_release(lock)


# ---------------------------------------------------------------------------
# run / flash
# ---------------------------------------------------------------------------


def host_arm_archive(cc, entry, incs, defs, outdir, name):
    """Compile ::HOST_ARMS under this bench's flags into one archive; its path, or (None, error)."""
    ar = shutil.which("ar")
    if not ar:
        return None, "no ar on PATH"
    objdir = os.path.join(outdir, "arms", name)
    os.makedirs(objdir, exist_ok=True)
    objs = []
    for src in HOST_ARMS:
        obj = os.path.join(objdir, os.path.basename(src)[:-2] + ".o")
        cmd = [cc, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-O2", "-c"] + defs + incs + [src, "-o", obj]
        r = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
        if r.returncode != 0:
            return None, r.stderr.strip()
        objs.append(obj)
    lib = os.path.join(objdir, "libhostarms.a")
    if os.path.isfile(lib):
        os.remove(lib)
    r = subprocess.run([ar, "rcs", lib] + objs, capture_output=True, text=True, cwd=ROOT)
    if r.returncode != 0:
        return None, r.stderr.strip()
    return lib, None


def cmd_run(a):
    """Build each bench against the host branch of the platform and run one pass."""
    cc = shutil.which("gcc") or shutil.which("cc")
    if not cc:
        print("no gcc on PATH")
        return 1
    envs = load()
    names = a.benches or list(envs)
    outdir = os.path.join(ROOT, ".pio", "bench")
    os.makedirs(outdir, exist_ok=True)
    failed = []
    for i, name in enumerate(names, 1):
        e = envs.get(name)
        if not e:
            print("unknown bench:", name)
            failed.append(name)
            continue
        if not e.get("src"):
            print("[%d/%d] %-30s NO SRC (run: bench.py deps %s)" % (i, len(names), name, name))
            failed.append(name)
            continue
        incs, defs = host_flags(e)
        # 'host' leaves both rates unstated, so the counter is the host CPU's own and the bench
        # measures it once and reports against it - the host runs all the way. A number pins the
        # reported rate instead, for comparing a host figure against a part's.
        if str(getattr(a, "mhz", "host")).lower() != "host":
            defs += ["-DPROTOCORE_HOST_CYCLE_MHZ=%du" % int(a.mhz), "-DDBENCH_CPU_MHZ=%du" % int(a.mhz)]
        main_c = os.path.relpath(os.path.join(bench_dir(e), "main", "main.c"), ROOT).replace("\\", "/")
        exe = os.path.join(outdir, name + ".exe")
        lib, err = host_arm_archive(cc, e, incs, defs, outdir, name)
        if lib is None:
            print("[%d/%d] %-30s HOST ARMS FAILED" % (i, len(names), name))
            if a.verbose:
                print(err)
            failed.append(name)
            continue
        cmd = (
            [cc, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-O2"]
            + defs
            + incs
            + [main_c]
            + e["src"]
            + [lib, "-o", exe, "-lm"]
        )
        b = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
        if b.returncode != 0:
            print("[%d/%d] %-30s BUILD FAILED" % (i, len(names), name))
            if a.verbose:
                print(b.stderr.strip())
            failed.append(name)
            continue
        try:
            r = subprocess.run([exe], capture_output=True, text=True, cwd=ROOT, timeout=a.timeout)
        except subprocess.TimeoutExpired:
            print("[%d/%d] %-30s TIMEOUT" % (i, len(names), name))
            failed.append(name)
            continue
        print("[%d/%d] %s" % (i, len(names), name))
        for ln in r.stdout.splitlines():
            if ln.startswith("DB "):
                print("   " + ln[3:])
        if r.returncode != 0:
            failed.append(name)
    print("\n%d/%d benches ran" % (len(names) - len(failed), len(names)))
    if failed:
        print("failed: " + " ".join(failed))
    return 1 if failed else 0


def cmd_flash(a):
    """The device path: pio builds and uploads one bench project."""
    envs = load()
    e = envs.get(a.bench)
    if not e:
        print("unknown bench:", a.bench)
        return 1
    cmd = ["pio", "run", "-d", bench_dir(e)]
    if a.upload:
        cmd += ["-t", "upload"]
    if a.port:
        cmd += ["--upload-port", a.port]
    return subprocess.run(cmd, cwd=ROOT).returncode


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def _subcommands(parser):
    """{name: subparser} for a parser's one subparser group."""
    for action in parser._actions:
        if isinstance(action, argparse._SubParsersAction):
            return action.choices
    return {}


def cmd_help(a):
    """Every command's own help in one call, or one command's.

    `-h` prints usage one level at a time, so reading the whole surface means invoking it once per
    subcommand. This prints all of it.
    """
    subs = _subcommands(build_parser())
    if a.command:
        if a.command not in subs:
            print("no such command: %s (try `bench.py help`)" % a.command, file=sys.stderr)
            return 2
        subs[a.command].print_help()
        return 0
    print(__doc__.strip())
    for name in sorted(subs):
        print("\n" + "-" * 78)
        print("### bench.py %s" % name)
        print(subs[name].format_help().strip())
    return 0


def build_parser():
    ap = argparse.ArgumentParser(
        prog="bench.py", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("help", help="every command's help in one call, or one command's")
    p.add_argument("command", nargs="?")
    p.set_defaults(fn=cmd_help)

    p = sub.add_parser("add", help="splice a new bench into the matrix")
    p.add_argument("name")
    p.add_argument("--after", required=True, help="bench to insert after")
    p.add_argument("--path", required=True, help="dir under test/performance_benching, e.g. services/southbound")
    # Attached form, one per flag: --flags=-DPROTOCORE_ENABLE_X=1 (a bare -D reads as an option).
    p.add_argument("--flags", action="append", default=[], metavar="=-DNAME=VALUE")
    p.add_argument("--src", nargs="*", default=[], help="repo-relative .c paths; bench.py deps fills these")
    p.add_argument("--desc", default="")
    p.set_defaults(fn=cmd_add)

    p = sub.add_parser("update", help="change an existing bench")
    p.add_argument("name")
    p.add_argument("--flags", action="append", default=[], metavar="=-DNAME=VALUE")
    p.add_argument("--drop-flags", action="append", default=[], dest="drop_flags", metavar="=-DNAME=VALUE")
    p.add_argument("--src", nargs="*", default=[])
    p.add_argument("--drop-src", nargs="*", default=[], dest="drop_src")
    p.add_argument("--desc", default=None)
    p.set_defaults(fn=cmd_update)

    p = sub.add_parser("rename", help="change a bench's name (a module that moved took it along)")
    p.add_argument("name")
    p.add_argument("to")
    p.set_defaults(fn=cmd_rename)

    p = sub.add_parser("gen", help="regenerate every bench project's platformio.ini")
    p.add_argument("--check", action="store_true", help="exit 1 if any is out of date (no write)")
    p.set_defaults(fn=cmd_gen)

    p = sub.add_parser("list", help="print the benches the matrix defines")
    p.add_argument("-v", "--verbose", action="store_true")
    p.set_defaults(fn=cmd_list)

    p = sub.add_parser("deps", help="fill each bench's src list from the include closure of its main.c")
    p.add_argument("benches", nargs="*")
    p.add_argument("--dry-run", action="store_true", dest="dry_run")
    p.add_argument("-v", "--verbose", action="store_true")
    p.set_defaults(fn=cmd_deps)

    p = sub.add_parser("run", help="build and run benches on the host")
    p.add_argument("benches", nargs="*")
    p.add_argument("--timeout", type=int, default=120)
    p.add_argument(
        "--mhz",
        default="host",
        help="the clock the counts are reported against: 'host' (default) measures the host's own "
        "and runs all the way; a number pins that rate instead",
    )
    p.add_argument("-v", "--verbose", action="store_true")
    p.set_defaults(fn=cmd_run)

    p = sub.add_parser("flash", help="build (and optionally upload) one bench to a device")
    p.add_argument("bench")
    p.add_argument("--upload", action="store_true")
    p.add_argument("--port")
    p.set_defaults(fn=cmd_flash)
    return ap


def main(argv=None):
    a = build_parser().parse_args(argv)
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
