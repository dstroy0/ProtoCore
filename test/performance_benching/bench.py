#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
bench.py - the one entry point for the microbenchmark matrix.

The counterpart to test/harness.py: same discipline, same shape, for
test/performance_benching instead of the native test suite. `bench.py -h` is the whole surface:

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


def host_flags(entry):
    incs = [
        "-Icore_setup/hal/host",
        "-Itest/support",
        "-Isrc",
        "-I.",
        "-I" + os.path.relpath(COMMON, ROOT).replace("\\", "/"),
    ]
    defs = [f for f in entry.get("flags", []) if f.startswith("-D")]
    return incs, defs


def _mm_headers(cc, entry, main_c):
    """The src/ headers main.c pulls in, from the compiler's own dependency scan."""
    incs, defs = host_flags(entry)
    cmd = [cc, "-MM", "-std=c11", "-D_POSIX_C_SOURCE=200809L"] + defs + incs + [main_c]
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    if r.returncode != 0:
        return None, r.stderr.strip()
    out = []
    for tok in r.stdout.replace("\\\n", " ").split(":", 1)[-1].split():
        p = os.path.relpath(os.path.abspath(os.path.join(ROOT, tok)), ROOT).replace("\\", "/")
        if p.startswith("src/") and p.endswith(".h"):
            out.append(p)
    return out, None


def cmd_deps(a):
    """A header that has a .c beside it is a translation unit the bench has to link.

    Iterated to a fixpoint: a .c pulled in for main.c has its own include closure, and the
    definitions it needs (protomem, swar, ...) are only reachable from there.
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
                continue  # a TU that does not preprocess alone contributes nothing
            for h in headers:
                c = h[:-2] + ".c"
                if c not in srcs and os.path.isfile(os.path.join(ROOT, c)):
                    srcs.append(c)
                    pending.append(c)
        if failed is not None:
            print("[%d/%d] %-30s SCAN FAILED" % (i, len(names), name))
            if a.verbose:
                print(failed)
            continue
        srcs.sort()
        resolved[name] = srcs
        print("[%d/%d] %-30s %d sources" % (i, len(names), name, len(srcs)))
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
        main_c = os.path.relpath(os.path.join(bench_dir(e), "main", "main.c"), ROOT).replace("\\", "/")
        exe = os.path.join(outdir, name + ".exe")
        cmd = [cc, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-O2"] + defs + incs + \
              [main_c] + e["src"] + ["-o", exe, "-lm"]
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


def main():
    ap = argparse.ArgumentParser(
        prog="bench.py", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = ap.add_subparsers(dest="cmd", required=True)

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
    p.add_argument("-v", "--verbose", action="store_true")
    p.set_defaults(fn=cmd_run)

    p = sub.add_parser("flash", help="build (and optionally upload) one bench to a device")
    p.add_argument("bench")
    p.add_argument("--upload", action="store_true")
    p.add_argument("--port")
    p.set_defaults(fn=cmd_flash)

    a = ap.parse_args()
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
