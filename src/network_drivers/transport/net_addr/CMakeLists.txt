# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/transport/net_addr
  SOURCES net_addr.c
  DEPS
    PRIVATE
    mmgr/rawmemcpy
    shared/ip
  PRIVATE_DEPS
    mmgr/rawmemcpy
)
