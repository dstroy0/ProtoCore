# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/security/totp
  GATE PROTOCORE_ENABLE_TOTP
  SOURCES totp.c
  PRIVATE_DEPS
    crypto/hash/sha1
    mmgr/protomem
    mmgr/secure
    mmgr/span
)
