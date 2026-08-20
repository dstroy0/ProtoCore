# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/tls/record
  GATE PROTOCORE_ENABLE_TLS
  SOURCES record.c
  DEPS
    PRIVATE
    crypto/aead/aes128gcm
    crypto/aead/aesgcm
    mmgr/protomem
    mmgr/secure
    network_drivers/presentation/http/http3/tls13_msg
    network_drivers/tls/key_schedule
  PRIVATE_DEPS
    mmgr/protomem
    mmgr/secure
    network_drivers/tls/key_schedule
)
