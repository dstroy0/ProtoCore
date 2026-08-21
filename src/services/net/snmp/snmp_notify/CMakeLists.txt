# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/net/snmp/snmp_notify
  GATE PROTOCORE_ENABLE_SNMP_TRAP
  SOURCES snmp_notify.c
  DEPS
    services/net/snmp/snmp_ber
  PRIVATE_DEPS
    mmgr/protostr
    mmgr/secure
    network_drivers/transport/udp/client
    server/clock
    shared/ip
)
