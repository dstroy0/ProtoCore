# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/application/sftp/ssh_sftp
  GATE PROTOCORE_ENABLE_SSH_SFTP
  SOURCES ssh_sftp.c
  PRIVATE_DEPS
    mmgr/endian
    mmgr/plaintext
    mmgr/protomem
    mmgr/protostr
    network_drivers/application/sftp/sftp
    network_drivers/presentation/ssh/connection
    network_drivers/presentation/ssh/network
    network_drivers/session
    server/storage/filesystem
)
