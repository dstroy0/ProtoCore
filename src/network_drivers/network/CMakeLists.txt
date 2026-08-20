# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/network
  GATE PROTOCORE_ENABLE_DNS
  SOURCES network.c
  DEPS
    network_drivers/network/dns/dns
    network_drivers/network/forward
    shared/ip
)

add_subdirectory(dns)
add_subdirectory(forward)


