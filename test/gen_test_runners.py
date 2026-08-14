#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# PlatformIO pre-build hook: generate each suite's Unity runner instead of hand-maintaining it.
#
# A hand-written main() carries a RUN_TEST line per case, and the two lists drift silently - a test
# function that is written but never registered simply does not run, and nothing reports it. Unity
# ships auto/generate_test_runner.rb for exactly this: it scans a suite for `void test_*(void)`,
# emits the extern declarations and a main() that calls every one of them.
#
# It also stamps each run_test() with the line the test is DEFINED on, so a failure points at the
# case rather than at the RUN_TEST line inside main().
#
# The generated file lands in the suite directory as unity_runner.c (gitignored) because PlatformIO
# compiles every source in the test directory it is running.

import glob
import os
import shutil
import subprocess
import sys

Import("env")  # noqa: F821 - injected by SCons

PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
sys.path.insert(0, PROJECT_DIR)

GENERATED = "unity_runner.c"

# Where an installer puts ruby. Globbed rather than version-pinned: chocolatey lands it in
# C:\tools\ruby<major><minor>, RubyInstaller in C:\Ruby<major><minor>-x64, and both bump the
# directory name on every release.
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
    for pattern in RUBY_GLOBS:
        for hit in sorted(glob.glob(pattern), reverse=True):  # newest install first
            if os.path.isfile(hit):
                return hit
    return None


def find_generator():
    """Unity is a lib_dep, so the generator lives under this env's libdeps."""
    libdeps = env.get("PROJECT_LIBDEPS_DIR")  # noqa: F821
    envname = env.get("PIOENV")  # noqa: F821
    candidates = []
    if libdeps and envname:
        candidates.append(os.path.join(libdeps, envname, "Unity", "auto", "generate_test_runner.rb"))
    if libdeps and os.path.isdir(libdeps):
        for d in os.listdir(libdeps):
            candidates.append(os.path.join(libdeps, d, "Unity", "auto", "generate_test_runner.rb"))
    for c in candidates:
        if os.path.isfile(c):
            return c
    return None


def suite_dir():
    """The directory of the suite this env is running, or None outside a test build."""
    name = env.get("PIOTEST_RUNNING_NAME")  # noqa: F821
    if not name:
        return None
    return os.path.join(PROJECT_DIR, env.get("PIOTEST_DIR") or "test", *name.split("/"))  # noqa: F821


def main():
    d = suite_dir()
    if not d or not os.path.isdir(d):
        return  # not a test build (or a suite that is not on disk); nothing to generate

    sources = [
        f
        for f in sorted(os.listdir(d))
        if f.endswith(".c") and f != GENERATED and "void test_" in open(os.path.join(d, f), encoding="utf-8").read()
    ]
    if not sources:
        return

    # The generator takes one input file and emits one main(), so a suite whose cases are spread
    # across several sources cannot be registered from any single one of them. Refused here rather
    # than generating from the first and dropping the rest, which is the silent drift this exists
    # to stop: move the cases into one file, or split the suite into one directory per file.
    if len(sources) > 1:
        raise SystemExit(
            "gen_test_runners: %s holds test cases in %d sources (%s).\n"
            "  Unity's generator registers one source per runner, so the rest would never run.\n"
            "  Put the cases in one file, or give each file its own suite directory."
            % (os.path.relpath(d, PROJECT_DIR), len(sources), ", ".join(sources))
        )

    ruby = find_ruby()
    generator = find_generator()
    if not ruby:
        raise SystemExit(
            "gen_test_runners: ruby not found on PATH or in %s - install it "
            "(choco install ruby, or winget install RubyInstallerTeam.Ruby.3.4)" % ", ".join(RUBY_GLOBS[:3])
        )
    if not generator:
        raise SystemExit("gen_test_runners: Unity's generate_test_runner.rb not found under PROJECT_LIBDEPS_DIR")

    src = os.path.join(d, sources[0])
    out = os.path.join(d, GENERATED)
    subprocess.run([ruby, generator, src, out], check=True)


main()
