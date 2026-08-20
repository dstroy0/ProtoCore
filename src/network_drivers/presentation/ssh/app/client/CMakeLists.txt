# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/ssh/app/client
  GATE PROTOCORE_ENABLE_SSH_CLIENT
  SOURCES client.c
  DEPS
    PRIVATE
    crypto/asymmetric/ed25519
    mmgr/arena
    mmgr/protomem
    mmgr/secure
    network_drivers/presentation/ssh/client
    network_drivers/presentation/ssh/connection
    network_drivers/presentation/ssh/network
    network_drivers/presentation/ssh/transport/transport
    network_drivers/transport/tcp
    server/clock
    shared/log
  PRIVATE_DEPS
    crypto/asymmetric/ed25519
    mmgr/arena
    mmgr/protomem
    mmgr/secure
    network_drivers/presentation/ssh/connection
    network_drivers/presentation/ssh/network
    network_drivers/presentation/ssh/transport/transport
    network_drivers/transport/tcp
    server/clock
    shared/log
)
