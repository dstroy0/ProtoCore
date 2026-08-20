# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/application/mdns_adaptive
  GATE PROTOCORE_ENABLE_MDNS_ADAPTIVE
  SOURCES mdns_adaptive.c
  PRIVATE_DEPS
    mmgr/plaintext
    network_drivers/application/mdns_service
    network_drivers/physical/physical
    server/clock
    services/radio/promisc
    shared/pcap
)
