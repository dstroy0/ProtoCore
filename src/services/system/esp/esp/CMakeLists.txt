# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/system/esp/esp
  GATE PROTOCORE_ENABLE_IKEV2
  SOURCES esp.c
  PRIVATE_DEPS
    crypto/aead/aesgcm
    mmgr/protomem
    mmgr/secure
)
