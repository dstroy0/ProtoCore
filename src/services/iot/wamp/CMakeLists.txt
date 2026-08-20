# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/iot/wamp
  GATE PROTOCORE_ENABLE_WAMP
  SOURCES wamp.c
  PRIVATE_DEPS
    network_drivers/presentation/codec/json
)
