# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/fieldbus/devicenet
  GATE PROTOCORE_ENABLE_DEVICENET
  SOURCES devicenet.c
  DEPS
    PRIVATE
    mmgr/protomem
    shared/can
  PRIVATE_DEPS
    mmgr/protomem
)
