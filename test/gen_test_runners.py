#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# PlatformIO pre-build hook: generate the running suite's Unity runner.
#
# PlatformIO exec()s a hook as a file named in extra_scripts, so this stays a file. The generation
# itself is test/harness.py `runners gen`, which is where the logic lives for every other caller.

import os
import sys

Import("env")  # noqa: F821 - injected by SCons

PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
sys.path.insert(0, PROJECT_DIR)
sys.path.insert(0, os.path.join(PROJECT_DIR, "test"))

import harness  # noqa: E402 - the path above is what makes this importable

name = env.get("PIOTEST_RUNNING_NAME")  # noqa: F821
if name:
    suite = os.path.join(PROJECT_DIR, env.get("PIOTEST_DIR") or "test", *name.split("/"))  # noqa: F821
    harness.generate_runner(
        suite,
        libdeps=env.get("PROJECT_LIBDEPS_DIR"),  # noqa: F821
        envname=env.get("PIOENV"),  # noqa: F821
    )
