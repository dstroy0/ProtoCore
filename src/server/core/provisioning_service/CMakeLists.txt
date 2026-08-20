# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/core/provisioning_service
  GATE PROTOCORE_ENABLE_PROVISIONING
  SOURCES provisioning_service.c
  PRIVATE_DEPS
    mmgr/protomem
    mmgr/protostr
    mmgr/secure
    network_drivers/application/web_assets
    network_drivers/physical/physical
    network_drivers/transport/udp/server
    server/clock
    shared/hex
    shared/mime
)
