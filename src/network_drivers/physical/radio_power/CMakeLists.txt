# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/physical/radio_power
  GATE PROTOCORE_ENABLE_RADIO_POWER
  SOURCES radio_power.c
  DEPS
    PRIVATE
    mmgr/plaintext
    network_drivers/physical/physical
  PRIVATE_DEPS
    mmgr/plaintext
)
