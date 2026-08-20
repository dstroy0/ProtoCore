# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/iot/cloudevents
  GATE PROTOCORE_ENABLE_CLOUDEVENTS
  SOURCES cloudevents.c
  DEPS
    PRIVATE
    network_drivers/presentation/codec/json
    network_drivers/presentation/http/http_parser
  PRIVATE_DEPS
    network_drivers/presentation/codec/json
)
