# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_wifi_sniffer: test_wifi_sniffer.c, plus the generated Unity runner.

protocore_add_suite(unit/src/services/radio/wifi_sniffer/test_wifi_sniffer
  SOURCES
    test_wifi_sniffer.c
)
