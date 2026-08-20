# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/security/audit_log
  GATE PROTOCORE_ENABLE_AUDIT_LOG
  SOURCES audit_log.c
  PRIVATE_DEPS
    crypto/hash/sha256
    mmgr/membuild
    mmgr/protomem
    mmgr/protostr
    mmgr/secure
    server/clock
    shared/hex
)
