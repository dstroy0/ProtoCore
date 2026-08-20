# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/system/esp/ipsec_db
  GATE PROTOCORE_ENABLE_IKEV2
  SOURCES ipsec_db.c
  DEPS
    PRIVATE
    mmgr/protomem
    services/security/ikev2/ikev2
    services/system/esp/esp
  PRIVATE_DEPS
    mmgr/protomem
)
