# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/transport/happy_eyeballs
  GATE PROTOCORE_ENABLE_HAPPY_EYEBALLS
  SOURCES happy_eyeballs.c
  DEPS
    shared/ip
  PRIVATE_DEPS
    mmgr/plaintext
)
