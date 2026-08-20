# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/iot/senml
  GATE PROTOCORE_ENABLE_SENML
  SOURCES senml.c
  DEPS
    PRIVATE
    mmgr/membuild
    mmgr/span
    network_drivers/presentation/codec
    network_drivers/presentation/codec/json
  PRIVATE_DEPS
    mmgr/membuild
    mmgr/span
    network_drivers/presentation/codec/json
)
