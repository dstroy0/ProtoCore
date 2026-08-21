# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# test_quic_packet: test_quic_packet.c, plus the generated Unity runner.

protocore_add_suite(unit/src/network_drivers/presentation/http/http3/test_quic_packet
  SOURCES
    test_quic_packet.c
)
