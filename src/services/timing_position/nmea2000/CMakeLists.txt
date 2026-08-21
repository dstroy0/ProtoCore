# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/timing_position/nmea2000
  GATE PROTOCORE_ENABLE_NMEA2000
  SOURCES nmea2000.c
  DEPS
    services/fieldbus/j1939
    shared/can
  PRIVATE_DEPS
    mmgr/protomem
)
