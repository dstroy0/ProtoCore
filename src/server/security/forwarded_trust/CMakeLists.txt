# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/security/forwarded_trust
  GATE PROTOCORE_ENABLE_FORWARDED_TRUST
  SOURCES forwarded_trust.c
  PRIVATE_DEPS
    mmgr/secure
    shared/ip
)
