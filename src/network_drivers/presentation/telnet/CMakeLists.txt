# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/telnet
  GATE PROTOCORE_ENABLE_TELNET
  SOURCES telnet.c
  PRIVATE_DEPS
    mmgr/plaintext
    mmgr/protoframe
    mmgr/protomem
    mmgr/protostr
    network_drivers/transport/tcp/protocol
    server/core/proto_handler
)
