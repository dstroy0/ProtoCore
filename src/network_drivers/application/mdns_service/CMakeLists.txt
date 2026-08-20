# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/application/mdns_service
  GATE PROTOCORE_ENABLE_MDNS
  SOURCES mdns_service.c
  PRIVATE_DEPS
    mmgr/plaintext
    mmgr/protostr
    mmgr/rawmemcpy
    mmgr/secure
    network_drivers/network/dns/dns_wire
    network_drivers/physical/physical
    network_drivers/transport/udp/server
)
