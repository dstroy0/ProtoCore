# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_pqc_sha3: test_pqc_sha3.c, plus the generated Unity runner.

protocore_add_suite(unit/src/crypto/hash/test_pqc_sha3
  SOURCES
    test_pqc_sha3.c
)
