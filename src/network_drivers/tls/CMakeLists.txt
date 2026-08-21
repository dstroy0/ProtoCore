# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/tls
  GATE PROTOCORE_ENABLE_TLS
  SOURCES tls.c
  DEPS
    network_drivers/presentation/http/http3/tls13_msg
    network_drivers/tls/key_schedule
    network_drivers/tls/record
  PRIVATE_DEPS
    crypto/rng
    mmgr/secure
    network_drivers/transport/tcp/protocol
)

add_subdirectory(handshake)
add_subdirectory(key_schedule)
add_subdirectory(record)


