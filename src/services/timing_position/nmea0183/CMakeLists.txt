# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/timing_position/nmea0183
  GATE PROTOCORE_ENABLE_NMEA0183
  SOURCES nmea0183.c
  PRIVATE_DEPS
    mmgr/protomem
    mmgr/protostr
)
