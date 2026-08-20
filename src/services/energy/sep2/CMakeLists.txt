# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/energy/sep2
  GATE PROTOCORE_ENABLE_SEP2
  SOURCES sep2.c
  PRIVATE_DEPS
    mmgr/membuild
)
