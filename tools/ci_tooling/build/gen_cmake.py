#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Write test/CMakeLists.txt from test/test_matrix.json.

The matrix is the one statement of what an env compiles: its flags, its `+<glob>` source filter, the
base it extends, and the suite directories it runs. This renders that into a CMake project with one
executable target and one `add_test` per SUITE, so `ctest` runs what `test/harness.py run` runs.

The matrix stays the source of truth. Nothing is hand-written into the generated file, and running
this again after editing the matrix is how the build follows it.

    python tools/harness.py build cmake            # write test/CMakeLists.txt
    python tools/harness.py build cmake --check    # fail if it is stale, write nothing

Then, out of tree:

    cmake -S test -B build/native
    cmake --build build/native -j
    ctest --test-dir build/native
"""

import argparse
import glob as _glob
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
MATRIX = os.path.join(ROOT, "test", "test_matrix.json")
OUT = os.path.join(ROOT, "test", "CMakeLists.txt")
OWED = os.path.join(ROOT, "test", "yanked_includes.json")

# The include dirs every env compiles with, whatever it named. -Iinclude is PlatformIO's implicit
# include_dir, where protocore.h lives; nothing here is pio, so each one is stated.
BASE_INCLUDES = ["test/core_setup/hal/host", "test/support", "src", "include", "."]

GENERATED_RUNNER = "unity_runner.c"


def rel(path):
    return os.path.relpath(path, ROOT).replace("\\", "/")


def inherited(envs, name, key, seen=None):
    """An env's @p key with its base chain in front of it, base first."""
    seen = seen or set()
    if name in seen or name not in envs:
        return []
    seen.add(name)
    e = envs[name]
    out = []
    base = e.get("base", "")
    if base.startswith("env:"):
        out = inherited(envs, base[len("env:") :], key, seen)
    return out + list(e.get(key, []))


def split_flags(flags):
    """An env's flags as (include dirs, defines), dropping ini interpolation."""
    incs, defs = [], []
    it = iter(flags)
    for f in it:
        if f.startswith("${") or f.startswith(";"):
            continue
        if f.startswith("-I"):
            incs.append((f[2:] if len(f) > 2 else next(it, "")).strip())
        elif f.startswith("-D"):
            defs.append(f[2:])
    for b in BASE_INCLUDES:
        if b not in incs:
            incs.append(b)
    return incs, defs


def expand_interpolation(envs, globs, seen=None):
    """Replace `${env:NAME.build_src_filter}` with that env's own filter, chased through its base.

    The matrix spells "everything that env compiles" the way platformio.ini does. A direct build has
    to expand it or the whole stack the entry stood for is silently left out of the link.
    """
    seen = seen or set()
    out = []
    for g in globs:
        m = g.strip()
        if m.startswith("${env:") and m.endswith(".build_src_filter}"):
            other = m[len("${env:") : -len(".build_src_filter}")]
            if other in seen or other not in envs:
                continue
            seen.add(other)
            out += expand_interpolation(envs, inherited(envs, other, "src"), seen)
            continue
        out.append(g)
    return out


def resolve_src(globs):
    """Expand `+<glob>` entries into repo-relative .c paths, under src/ first then the repo root.

    A path is returned once however many entries reach it: an env that extends a base inherits that
    base's filter and then names files of its own, and one .c compiled twice is every symbol in it
    defined twice, which the linker refuses.
    """
    out, seen, missing = [], set(), []
    for g in globs:
        stem = g
        if stem.startswith("+<") and stem.endswith(">"):
            stem = stem[2:-1]
        elif stem.startswith("-<"):
            continue  # an exclusion; the filter here is additive
        stem = stem.replace("../", "")
        hits = []
        for cand in ("src/" + stem, stem):
            full = os.path.join(ROOT, cand)
            if os.path.isfile(full):
                hits = [cand.replace("\\", "/")]
                break
            found = [rel(h) for h in _glob.glob(full, recursive=True) if h.endswith(".c")]
            if found:
                hits = sorted(found)
                break
        if not hits:
            missing.append(g)
            continue
        for h in hits:
            if h not in seen:
                seen.add(h)
                out.append(h)
    return out, missing


def suite_sources(test):
    """Every .c one suite contributes, and whether it brings its own main()."""
    srcs, has_main = [], False
    sd = os.path.join(ROOT, "test", *test.split("/"))
    if not os.path.isdir(sd):
        return srcs, has_main
    for f in sorted(os.listdir(sd)):
        if not f.endswith(".c"):
            continue
        if f == GENERATED_RUNNER:
            srcs.append(rel(os.path.join(sd, f)))
            continue
        p = os.path.join(sd, f)
        srcs.append(rel(p))
        with open(p, encoding="utf-8", errors="replace") as fh:
            if "int main(" in fh.read():
                has_main = True
    return srcs, has_main


def suite_dir(test):
    return rel(os.path.join(ROOT, "test", *test.split("/")))


def reached_headers(test):
    """Header names one suite includes, one level on from the host mocks it names.

    A suite reaches littlefs through test/core_setup/hal/host/lfs_mock.h rather than by naming
    lfs.h, so the includes of the headers it does name are read too. Same rule as
    test/harness.py's _reached_headers, so both build paths pull in the same packages.
    """
    sd = os.path.join(ROOT, "test", *test.split("/"))
    names, text = set(), []
    try:
        for f in os.listdir(sd):
            if f.endswith((".c", ".h")):
                with open(os.path.join(sd, f), encoding="utf-8", errors="replace") as fh:
                    text.append(fh.read())
    except OSError:
        return names
    hal = os.path.join(ROOT, "test", "core_setup", "hal", "host")
    seen, i = set(), 0
    while i < len(text):
        t = text[i]
        i += 1
        for inc in re.findall(r'#\s*include\s*[<"]([^">]+)[">]', t):
            base = os.path.basename(inc)
            names.add(base)
            cand = os.path.join(hal, base)
            if base not in seen and os.path.isfile(cand):
                seen.add(base)
                with open(cand, encoding="utf-8", errors="replace") as fh:
                    text.append(fh.read())
    return names


def lib_packages(envname):
    """A lib_dep's include dir and sources, which pio passes and a plain CMake build does not.

    Unity is handled on its own because every env links it. Anything else under an env's libdeps is
    a package the env asked for - littlefs is the one in the tree, reached through the host mount
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
        incs.append(rel(inc))
        heads = [h for h in os.listdir(inc) if h.endswith(".h")]
        srcs = [rel(os.path.join(src, f)) for f in sorted(os.listdir(src)) if f.endswith(".c")]
        pkgs.append((heads, srcs))
    return incs, pkgs


def target_names(name, tests):
    """One target name per suite: the env alone when it runs one, else the env plus the suite.

    Each suite carries its own Unity main(), so an env that runs several links several mains into
    one image. The direct compile in test/harness.py builds one binary per suite dir; this is that.
    Two suites in one env can share a leaf name (native_ssh runs two `test_server`), so the suffix
    grows leftward through the path until every name in the env is distinct.
    """
    if len(tests) == 1:
        return {tests[0]: name}
    parts = {t: t.split("/") for t in tests}
    depth = 1
    while True:
        out = {t: name + "__" + "_".join(p[-depth:]) for t, p in parts.items()}
        if len(set(out.values())) == len(tests) or depth >= max(len(p) for p in parts.values()):
            return out
        depth += 1


def cmake_escape(s):
    return s.replace("\\", "/").replace('"', '\\"')


HEADER = """# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# GENERATED by tools/ci_tooling/build/gen_cmake.py from test/test_matrix.json. Do not edit.
# Regenerate with:  python tools/harness.py build cmake
#
# The matrix states what each env compiles; this is that, rendered. One executable target and one
# test per suite: a suite carries its own Unity main(), so an env that runs several is several
# targets, named <env>__<suite>.
#
#   cmake -S test -B build/native
#   cmake --build build/native -j
#   ctest --test-dir build/native

cmake_minimum_required(VERSION 3.16)
project(protocore_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(PROTOCORE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

enable_testing()

# Unity is a package the envs link; pio installs it per env, so any copy in the tree will do.
if(NOT PROTOCORE_UNITY_DIR)
  file(GLOB _unity_candidates "${PROTOCORE_ROOT}/.pio/libdeps/*/Unity/src")
  if(_unity_candidates)
    list(GET _unity_candidates 0 PROTOCORE_UNITY_DIR)
  endif()
endif()
if(NOT PROTOCORE_UNITY_DIR)
  message(FATAL_ERROR
    "Unity sources not found. Run `pio pkg install` once, or pass -DPROTOCORE_UNITY_DIR=<path to Unity/src>.")
endif()

# Every env compiles under the same base: C11, POSIX, no exceptions, -O1. Matched to the direct
# compile in test/harness.py so a CMake build and a harness run are the same translation.
add_library(protocore_env_base INTERFACE)
target_compile_definitions(protocore_env_base INTERFACE _POSIX_C_SOURCE=200809L)
target_compile_options(protocore_env_base INTERFACE -fno-exceptions -O1)
target_include_directories(protocore_env_base INTERFACE "${PROTOCORE_UNITY_DIR}")
target_link_libraries(protocore_env_base INTERFACE m)

# One suite: its sources, its defines, its include dirs, and the one suite it runs.
#
# unity.c is a source of the target, not a library beside it: UNITY_INCLUDE_DOUBLE and the rest of
# Unity's configuration are env defines, and a shared library compiles them out of every env that
# asked for them. The direct compile in test/harness.py puts unity.c in the same TU list.
function(protocore_env name)
  cmake_parse_arguments(E "" "" "SOURCES;DEFINES;INCLUDES" ${ARGN})
  add_executable(${name} ${E_SOURCES} "${PROTOCORE_UNITY_DIR}/unity.c")
  target_include_directories(${name} PRIVATE ${E_INCLUDES})
  target_compile_definitions(${name} PRIVATE ${E_DEFINES})
  target_link_libraries(${name} PRIVATE protocore_env_base)
  set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/env")
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES
    FAIL_REGULAR_EXPRESSION "[1-9][0-9]* Failures"
    WORKING_DIRECTORY "${PROTOCORE_ROOT}")
endfunction()

"""


def load_owed():
    """What each source file owes because a header no longer carries it (tools/dev_env/yank_includes.py).

    A yanked include is a dependency that moved, not one that went away. Each entry becomes a forced
    include on exactly that source file, so the build states what the header used to.
    """
    if not os.path.isfile(OWED):
        return {}
    with open(OWED, encoding="utf-8") as f:
        return json.load(f).get("owed", {})


def render_owed(owed):
    if not owed:
        return ""
    out = [
        "\n# Includes a header no longer carries (test/yanked_includes.json). Forced onto the one source\n"
        "# file that owes each, so nothing else pays for it.\n"
    ]
    for src in sorted(owed):
        incs = owed[src]
        if not incs:
            continue
        opts = []
        for w in incs:
            opts.append("-include" if w.startswith("<") else "-include")
            opts.append(w.strip('"<>'))
        out.append(
            'set_source_files_properties("${PROTOCORE_ROOT}/%s" PROPERTIES COMPILE_OPTIONS "%s")\n'
            % (cmake_escape(src), ";".join(opts))
        )
    return "".join(out)


def make_runner(test):
    """Generate the suite's unity_runner.c through the test harness. True when one is on disk after.

    Unity's generator lives under .pio/libdeps, so this fails on a checkout that has never installed
    it - which is exactly the case that must not write a truncated CMakeLists.
    """
    sd = os.path.join(ROOT, "test", *test.split("/"))
    subprocess.run(
        [sys.executable, os.path.join(ROOT, "test", "harness.py"), "runners", "gen", rel(sd)],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    return os.path.isfile(os.path.join(sd, GENERATED_RUNNER))


def render(envs, warn, dropped):
    out = [HEADER, render_owed(load_owed())]
    rendered = 0
    for name in sorted(envs):
        env = envs[name]
        if not name.startswith("native"):
            continue
        tests = env.get("tests", [])
        if not tests:
            continue
        srcs, missing = resolve_src(expand_interpolation(envs, inherited(envs, name, "src")))
        for g in missing:
            warn.append("%s: src filter matches nothing: %s" % (name, g))
        incs, defs = split_flags(inherited(envs, name, "flags"))
        lib_incs, lib_pkgs = lib_packages(name)
        desc = (env.get("desc") or "").strip().split("\n")[0]
        targets = target_names(name, tests)
        for t in tests:
            suite, has_main = suite_sources(t)
            # A lib_dep the suite reaches has to be compiled in: pio links it, a plain CMake build
            # does not, and the suite fails at the mock's `#include <lfs.h>` instead.
            reached = reached_headers(t)
            for heads, lib_srcs in lib_pkgs:
                if reached & set(heads):
                    suite = suite + lib_srcs
            if not suite:
                warn.append("%s: no suite sources on disk (%s)" % (name, t))
                continue
            if not has_main and not any(s.endswith(GENERATED_RUNNER) for s in suite):
                # The runner is generated, not committed, so a fresh checkout has none of them and
                # every generated-runner suite would drop out - 437 targets down to 27, written over
                # the committed file without an error. Make it, and count the miss if it cannot be.
                if make_runner(t):
                    suite, has_main = suite_sources(t)
                    reached = reached_headers(t)
                    for heads, lib_srcs in lib_pkgs:
                        if reached & set(heads):
                            suite = suite + lib_srcs
                else:
                    dropped.append(targets[t])
                    warn.append(
                        "%s: no main() and no %s - run `test/harness.py runners gen`" % (targets[t], GENERATED_RUNNER)
                    )
                    continue

            out.append("# %s\n" % desc if desc else "")
            out.append("protocore_env(%s\n" % targets[t])
            out.append("  SOURCES\n")
            for s in srcs + suite:
                out.append('    "${PROTOCORE_ROOT}/%s"\n' % cmake_escape(s))
            if defs:
                out.append("  DEFINES\n")
                for d in defs:
                    out.append('    "%s"\n' % cmake_escape(d))
            out.append("  INCLUDES\n")
            for i in incs + lib_incs + [suite_dir(t)]:
                out.append('    "${PROTOCORE_ROOT}/%s"\n' % cmake_escape(i))
            out.append(")\n\n")
            rendered += 1
    return "".join(out), rendered


def main():
    ap = argparse.ArgumentParser(prog="gen_cmake", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="fail if the file is stale; write nothing")
    ap.add_argument("--quiet", action="store_true", help="only report problems")
    a = ap.parse_args()

    with open(MATRIX, encoding="utf-8") as f:
        envs = json.load(f)["envs"]

    warn = []
    dropped = []
    text, n = render(envs, warn, dropped)

    # A target dropped for a missing runner is a target the file loses, and this file is committed.
    # Refuse the write rather than hand back a smaller CMakeLists that still looks like a good one.
    if dropped and not a.check:
        print(
            "gen_cmake: %d suite(s) have no main() and no %s, and one could not be generated.\n"
            "  test/CMakeLists.txt was NOT written - it would have lost those targets silently.\n"
            "  Unity's generator lives under .pio/libdeps; install it, then re-run.\n"
            "  first few: %s" % (len(dropped), GENERATED_RUNNER, " ".join(sorted(dropped)[:5])),
            file=sys.stderr,
        )
        return 1

    if a.check:
        cur = open(OUT, encoding="utf-8").read() if os.path.isfile(OUT) else ""
        if cur != text:
            print("test/CMakeLists.txt is stale - run `python tools/harness.py build cmake`", file=sys.stderr)
            return 1
        if not a.quiet:
            print("test/CMakeLists.txt is current (%d targets)" % n)
        return 0

    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    if not a.quiet:
        print("wrote %s: %d targets" % (rel(OUT), n))
    for w in warn:
        print("  " + w, file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
