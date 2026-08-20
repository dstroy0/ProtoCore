# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/application/ntp_server
  GATE PROTOCORE_ENABLE_NTP_SERVER
  SOURCES ntp_server.c
  DEPS
    PRIVATE
    mmgr/endian
    mmgr/plaintext
    mmgr/protomem
    network_drivers/application/ntp
    network_drivers/transport/udp/server
    server/clock
    services/timing_position/time_source
  PRIVATE_DEPS
    mmgr/endian
    mmgr/plaintext
    mmgr/protomem
    network_drivers/transport/udp/server
    server/clock
    services/timing_position/time_source
)
