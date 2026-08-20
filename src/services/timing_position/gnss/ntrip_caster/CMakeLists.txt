# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/timing_position/gnss/ntrip_caster
  GATE PROTOCORE_ENABLE_NTRIP_CASTER
  SOURCES ntrip_caster.c
  PRIVATE_DEPS
    mmgr/membuild
    mmgr/protomem
)
