# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/opcua/opcua_client
  GATE PROTOCORE_ENABLE_OPCUA_CLIENT
  SOURCES opcua_client.c
  DEPS
    PRIVATE
    mmgr/protomem
    mmgr/protostr
    services/opcua
  PRIVATE_DEPS
    mmgr/protomem
    mmgr/protostr
)
