# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/iot/mqtt/mqtt_sn
  GATE PROTOCORE_ENABLE_MQTT_SN
  SOURCES mqtt_sn.c
  PRIVATE_DEPS
    mmgr/protomem
    mmgr/protostr
)
