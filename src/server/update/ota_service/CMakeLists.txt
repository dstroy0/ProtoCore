# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/update/ota_service
  GATE PROTOCORE_ENABLE_OTA AND PROTOCORE_HAS_VENDOR_OTA
  SOURCES ota_service.c
  PRIVATE_DEPS
    mmgr/protomem
    mmgr/protostr
    network_drivers/presentation/codec/base64
    network_drivers/presentation/http/http_parser
    server/clock
    shared/mime
)
