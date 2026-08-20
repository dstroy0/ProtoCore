# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/fieldbus/profibus
  GATE PROTOCORE_ENABLE_PROFIBUS
  SOURCES profibus.c
  PRIVATE_DEPS
    mmgr/protomem
)
