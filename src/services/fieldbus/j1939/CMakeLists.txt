# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/fieldbus/j1939
  GATE PROTOCORE_ENABLE_J1939
  SOURCES j1939.c
  DEPS
    PRIVATE
    mmgr/plaintext
    mmgr/protomem
    shared/can
  PRIVATE_DEPS
    mmgr/plaintext
    mmgr/protomem
)
