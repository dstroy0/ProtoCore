# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/fieldbus/cia402
  GATE PROTOCORE_ENABLE_CIA402
  SOURCES cia402.c
  DEPS
    PRIVATE
    mmgr/endian
    mmgr/protomem
    services/fieldbus/canopen
    shared/can
  PRIVATE_DEPS
    mmgr/endian
    mmgr/protomem
    services/fieldbus/canopen
)
