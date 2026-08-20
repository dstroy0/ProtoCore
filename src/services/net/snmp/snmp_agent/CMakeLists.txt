# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/net/snmp/snmp_agent
  GATE PROTOCORE_ENABLE_SNMP
  SOURCES snmp_agent.c
  DEPS
    PRIVATE
    mmgr/protomem
    mmgr/protostr
    mmgr/secure
    network_drivers/transport/udp/server
    server/clock
    services/net/snmp/snmp_ber
    services/net/snmp/snmp_v3
  PRIVATE_DEPS
    mmgr/protomem
    mmgr/protostr
    mmgr/secure
    network_drivers/transport/udp/server
    server/clock
    services/net/snmp/snmp_v3
)
