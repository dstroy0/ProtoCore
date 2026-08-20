# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/signaling/bus_capture
  GATE PROTOCORE_ENABLE_BUS_CAPTURE
  SOURCES bus_capture.c
  DEPS
    PRIVATE
    mmgr/secure
    shared/can
    shared/pcap
  PRIVATE_DEPS
    mmgr/secure
    shared/pcap
)
