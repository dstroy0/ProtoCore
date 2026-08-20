# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/fieldbus/df1
  GATE PROTOCORE_ENABLE_DF1
  SOURCES df1.c
  PRIVATE_DEPS
    shared/crc
)
