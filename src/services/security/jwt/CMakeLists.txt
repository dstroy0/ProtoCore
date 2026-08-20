# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/security/jwt
  GATE PROTOCORE_ENABLE_JWT
  SOURCES jwt.c
  PRIVATE_DEPS
    crypto/ct_eq
    crypto/mac/hmac_sha256
    mmgr/membuild
    mmgr/protomem
    mmgr/protostr
    mmgr/secure
    network_drivers/presentation/codec/base64
)
