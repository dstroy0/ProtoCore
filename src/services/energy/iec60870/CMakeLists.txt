# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/energy/iec60870
  GATE PROTOCORE_ENABLE_IEC60870
  SOURCES iec60870.c
  PRIVATE_DEPS
    mmgr/protomem
)
