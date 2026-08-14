#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# PlatformIO pre-build hook for the envs that provision an SSH host key: put
# test/fixtures/ssh_test_host_key/ssh_test_keys.h in place before anything compiles, so a bare
# `pio test -e native_ssh` builds from a clean checkout.
#
# --if-absent, so a full run's one fresh key (the runner generates it up front) is the key every
# env in that run compiles against.
#
# PlatformIO exec()s a hook as a file named in extra_scripts, so this stays a file. The work itself
# is test/harness.py `keys ensure`.

import os
import sys

Import("env")  # noqa: F821 - injected by SCons

PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
sys.path.insert(0, PROJECT_DIR)
sys.path.insert(0, os.path.join(PROJECT_DIR, "test"))

import harness  # noqa: E402 - the path above is what makes this importable

harness.ensure_keys(if_absent=True)
