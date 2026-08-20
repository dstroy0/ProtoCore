# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/security/oidc
  GATE PROTOCORE_ENABLE_OIDC
  SOURCES oidc.c
  PRIVATE_DEPS
    crypto/asymmetric/rsa
    mmgr/membuild
    mmgr/plaintext
    mmgr/protomem
    mmgr/protostr
    mmgr/secure
    network_drivers/presentation/codec/base64
)
