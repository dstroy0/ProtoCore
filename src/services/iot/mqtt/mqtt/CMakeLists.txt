# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/iot/mqtt/mqtt
  GATE PROTOCORE_ENABLE_MQTT
  SOURCES mqtt.c
  PRIVATE_DEPS
    mmgr/protomem
    mmgr/protostr
    mmgr/secure
    mmgr/span
    network_drivers/transport/tcp/client
    server/clock
    shared/utf8
)
