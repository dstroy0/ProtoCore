# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/ssh/client
  GATE PROTOCORE_ENABLE_SSH_CLIENT
  SOURCES client.c
  DEPS
    network_drivers/presentation/ssh/common
  PRIVATE_DEPS
    crypto/asymmetric/bignum
    crypto/asymmetric/curve25519
    crypto/asymmetric/ecdsa
    crypto/asymmetric/ed25519
    crypto/hash/sha256
    crypto/pqc/mlkem
    crypto/pqc/sntrup761
    crypto/rng
    mmgr/arena
    mmgr/bytes
    mmgr/plaintext
    mmgr/protoframe
    mmgr/protomem
    mmgr/rawmemcpy
    mmgr/secure
    network_drivers/presentation/ssh
    network_drivers/presentation/ssh/auth
    network_drivers/presentation/ssh/connection
    network_drivers/presentation/ssh/network
    network_drivers/presentation/ssh/transport/ssh_kexhash
    network_drivers/presentation/ssh/transport/ssh_rsa
    network_drivers/presentation/ssh/transport/transport
    network_drivers/transport/tcp
    network_drivers/transport/tcp/client
    server/clock
    shared/log
)
