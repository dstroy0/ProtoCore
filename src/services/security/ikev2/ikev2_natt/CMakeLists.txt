# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/security/ikev2/ikev2_natt
  GATE PROTOCORE_ENABLE_IKEV2
  SOURCES ikev2_natt.c
  DEPS
    services/security/ikev2/ikev2
  PRIVATE_DEPS
    crypto/hash/sha1
    mmgr/protomem
    mmgr/secure
    mmgr/span
)
