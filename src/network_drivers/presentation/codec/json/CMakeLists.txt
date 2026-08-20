# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/codec/json
  GATE PROTOCORE_ENABLE_JSON
  SOURCES json.c
  PRIVATE_DEPS
    mmgr/membuild
    mmgr/protostr
    shared/hex
)
