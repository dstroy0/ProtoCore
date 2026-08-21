# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_smb_crypto: test_smb_crypto.c, plus the generated Unity runner.

protocore_add_suite(unit/src/crypto/hash/test_smb_crypto
  SOURCES
    test_smb_crypto.c
)
