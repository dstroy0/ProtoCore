# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/ssh/connection
  GATE PROTOCORE_ENABLE_SSH_SFTP
  SOURCES connection.c
  DEPS
    network_drivers/presentation/ssh/common
  PRIVATE_DEPS
    mmgr/bytes
    mmgr/endian
    mmgr/plaintext
    mmgr/protoframe
    mmgr/protomem
    mmgr/protostr
    mmgr/secure
    network_drivers/presentation/ssh
    network_drivers/presentation/ssh/app/server
    network_drivers/presentation/ssh/network
    network_drivers/presentation/ssh/server
    network_drivers/presentation/ssh/transport/transport
    network_drivers/transport/tcp
    shared/ip
    shared/log
)
