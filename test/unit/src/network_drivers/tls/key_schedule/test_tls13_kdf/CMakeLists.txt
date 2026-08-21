# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_tls13_kdf: test_tls13_kdf.c, plus the generated Unity runner.

protocore_add_suite(unit/src/network_drivers/tls/key_schedule/test_tls13_kdf
  SOURCES
    test_tls13_kdf.c
)
