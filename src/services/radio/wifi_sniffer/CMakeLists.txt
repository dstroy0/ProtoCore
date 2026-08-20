# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/radio/wifi_sniffer
  GATE PROTOCORE_ENABLE_WIFI_SNIFFER
  SOURCES wifi_sniffer.c
  PRIVATE_DEPS
    mmgr/plaintext
    mmgr/protomem
    server/clock
    services/radio/promisc
    shared/pcap
)
