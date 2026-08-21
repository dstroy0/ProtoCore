# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_digest_vectors: test_digest_vectors.c, plus the generated Unity runner.

protocore_add_suite(unit/src/crypto/hash/test_digest_vectors
  SOURCES
    test_digest_vectors.c
)
