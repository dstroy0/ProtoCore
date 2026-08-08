#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# PlatformIO pre-build hook for the envs that provision an SSH host key: put
# test/fixtures/ssh_test_host_key/ssh_test_keys.h in place before anything compiles, so a bare
# `pio test -e native_ssh` builds from a clean checkout.
#
# --if-absent, so a full run's one fresh key (test/run_tests.sh generates it up front) is the key
# every env in that run compiles against.
#
# PlatformIO exec()s this with no __file__, and injects the SCons construction environment, which
# carries the project root.

import subprocess
import sys

Import("env")  # noqa: F821 - injected by SCons

subprocess.run(
    [sys.executable, "-m", "tools.crypto.gen_ssh_test_keys", "--if-absent"],
    cwd=env["PROJECT_DIR"],  # noqa: F821
    check=True,
)
