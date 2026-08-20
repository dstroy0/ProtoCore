# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/update/ota_rollback
  GATE PROTOCORE_ENABLE_OTA_ROLLBACK
  SOURCES ota_rollback.c
  PRIVATE_DEPS
    server/clock
)
