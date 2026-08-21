# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_snmp_ber: test_snmp_ber.c, plus the generated Unity runner.

protocore_add_suite(unit/src/services/net/snmp/test_snmp_ber
  SOURCES
    test_snmp_ber.c
)
