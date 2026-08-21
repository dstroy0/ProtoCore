# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/net/snmp/snmp_v3
  GATE PROTOCORE_ENABLE_SNMP_V3
  SOURCES snmp_v3.c
  DEPS
    services/net/snmp/snmp_notify
  PRIVATE_DEPS
    crypto/mac/hmac_sha256
    mmgr/endian
    mmgr/protomem
    mmgr/protostr
    mmgr/secure
    network_drivers/transport/udp/client
    server/clock
    services/net/snmp/snmp_agent
    services/net/snmp/snmp_ber
    services/net/snmp/snmp_crypto
    shared/ip
)
