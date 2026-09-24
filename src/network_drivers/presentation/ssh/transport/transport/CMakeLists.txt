# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/ssh/transport/transport
  GATE PROTOCORE_ENABLE_SSH
  SOURCES transport.c
  DEPS
    crypto/aead/aesgcm
    crypto/aead/chachapoly
    crypto/asymmetric/bignum
    crypto/asymmetric/ecdsa
    crypto/cipher/aes256ctr
    crypto/hash/sha256
    mmgr/secure
    network_drivers/presentation/ssh/common
    network_drivers/presentation/ssh/transport/phase_machine
  PRIVATE_DEPS
    crypto/asymmetric/curve25519
    crypto/asymmetric/ed25519
    crypto/asymmetric/rsa
    crypto/ct_eq
    crypto/mac/hmac_sha256
    crypto/mac/hmac_sha512
    crypto/pqc/mlkem
    crypto/pqc/sntrup761
    crypto/rng
    mmgr/bytes
    mmgr/membuild
    mmgr/plaintext
    mmgr/protomem
    mmgr/protostr
    network_drivers/presentation/ssh
    network_drivers/presentation/ssh/auth
    network_drivers/presentation/ssh/network
    network_drivers/presentation/ssh/transport/comp
    network_drivers/presentation/ssh/transport/extension
    network_drivers/presentation/ssh/transport/ssh_kexhash
    network_drivers/presentation/ssh/transport/ssh_rsa
    network_drivers/presentation/ssh/transport/zlib
    server/clock
)
