# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/ssh/transport/ssh_kexhash
  SOURCES ssh_kexhash.c
  PRIVATE_DEPS
    crypto/hash/sha256
    crypto/hash/sha512
)
