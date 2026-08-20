# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/net/smtp
  GATE PROTOCORE_ENABLE_SMTP
  SOURCES smtp.c
  PRIVATE_DEPS
    mmgr/membuild
    mmgr/protostr
    mmgr/secure
    network_drivers/presentation/codec/base64
    network_drivers/transport/tcp/client
    server/clock
)
