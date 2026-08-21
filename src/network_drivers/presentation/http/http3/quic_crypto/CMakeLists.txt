# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/http/http3/quic_crypto
  GATE PROTOCORE_ENABLE_HTTP3
  SOURCES quic_crypto.c
  DEPS
    crypto/kdf/hkdf
  PRIVATE_DEPS
    crypto/aead/aes128gcm
    mmgr/protomem
    mmgr/secure
    network_drivers/presentation/http/http3/quic_packet
)
