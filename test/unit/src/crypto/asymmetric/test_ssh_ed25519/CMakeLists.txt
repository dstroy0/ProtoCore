# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_ssh_ed25519: test_ssh_ed25519.c, plus the generated Unity runner.

protocore_add_suite(unit/src/crypto/asymmetric/test_ssh_ed25519
  SOURCES
    test_ssh_ed25519.c
)
