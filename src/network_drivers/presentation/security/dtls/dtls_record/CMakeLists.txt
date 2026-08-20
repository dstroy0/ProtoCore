# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/security/dtls/dtls_record
  GATE PROTOCORE_ENABLE_DTLS
  SOURCES dtls_record.c
  PRIVATE_DEPS
    crypto/aead/aes128gcm
    mmgr/protomem
    mmgr/secure
    network_drivers/tls/key_schedule
)
