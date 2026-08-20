# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/signaling/device_id
  GATE PROTOCORE_ENABLE_DEVICE_ID
  SOURCES device_id.c
  PRIVATE_DEPS
    crypto/hash/sha1
    mmgr/secure
    mmgr/span
    shared/hex
)
