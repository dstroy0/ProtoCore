# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/signaling/signaling
  SOURCES signaling.c
  PRIVATE_DEPS
    mmgr/plaintext
    network_drivers/transport/tcp
    network_drivers/transport/tcp/protocol
)
