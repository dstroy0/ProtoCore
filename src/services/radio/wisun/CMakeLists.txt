# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/radio/wisun
  GATE PROTOCORE_ENABLE_WISUN
  SOURCES wisun.c
  DEPS
    PRIVATE
    mmgr/membuild
    mmgr/protomem
    shared/ip
  PRIVATE_DEPS
    mmgr/membuild
    mmgr/protomem
)
