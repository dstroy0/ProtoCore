# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/http/auth
  GATE PROTOCORE_ENABLE_AUTH
  SOURCES auth.c
  PRIVATE_DEPS
    crypto/ct_eq
    crypto/hash/sha256
    mmgr/membuild
    mmgr/protomem
    mmgr/protostr
    mmgr/secure
    network_drivers/presentation/codec/base64
    network_drivers/presentation/http
    network_drivers/transport/tcp/protocol
    server/clock
    shared/hex
)
