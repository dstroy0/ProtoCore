# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/timing_position/time_source
  GATE PROTOCORE_ENABLE_TIME_SOURCE
  SOURCES time_source.c
  PRIVATE_DEPS
    shared/http_date
)
