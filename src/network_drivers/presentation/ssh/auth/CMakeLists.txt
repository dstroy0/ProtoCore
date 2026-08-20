# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/ssh/auth
  GATE PROTOCORE_ENABLE_SSH_ZLIB
  SOURCES auth.c
  DEPS
    PRIVATE
    crypto/asymmetric/ecdsa
    crypto/asymmetric/ed25519
    mmgr/bytes
    mmgr/endian
    mmgr/plaintext
    mmgr/protomem
    mmgr/protostr
    mmgr/secure
    network_drivers/presentation/ssh/common
    network_drivers/presentation/ssh/connection
    network_drivers/presentation/ssh/network
    network_drivers/presentation/ssh/transport/comp
    network_drivers/presentation/ssh/transport/ssh_rsa
    network_drivers/presentation/ssh/transport/transport
    server/clock
  PRIVATE_DEPS
    crypto/asymmetric/ecdsa
    crypto/asymmetric/ed25519
    mmgr/bytes
    mmgr/endian
    mmgr/plaintext
    mmgr/protomem
    mmgr/protostr
    mmgr/secure
    network_drivers/presentation/ssh/connection
    network_drivers/presentation/ssh/network
    network_drivers/presentation/ssh/transport/comp
    network_drivers/presentation/ssh/transport/ssh_rsa
    network_drivers/presentation/ssh/transport/transport
    server/clock
)
