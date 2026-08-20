# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/opcua/models/robotics
  GATE PROTOCORE_ENABLE_ROBOTICS
  SOURCES robotics.c
  PRIVATE_DEPS
    mmgr/plaintext
    mmgr/protostr
    services/opcua
)
