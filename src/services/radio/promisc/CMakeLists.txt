# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/radio/promisc
  GATE PROTOCORE_ENABLE_PROMISC
  SOURCES promisc.c
  PRIVATE_DEPS
    mmgr/plaintext
    mmgr/protomem
    network_drivers/physical/physical
    shared/pcap
)
