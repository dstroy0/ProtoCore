# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/io/http_clock
  GATE PROTOCORE_ENABLE_HTTP_CLOCK
  SOURCES http_clock.c
  PRIVATE_DEPS
    mmgr/plaintext
    network_drivers/application/ntp_service
    services/timing_position/time_source
    shared/http_date
)
