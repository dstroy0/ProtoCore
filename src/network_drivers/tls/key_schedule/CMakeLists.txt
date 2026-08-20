# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/tls/key_schedule
  SOURCES key_schedule.c
  PRIVATE_DEPS
    crypto/hash/sha256
    crypto/hash/sha384
    crypto/kdf/hkdf
    crypto/kdf/hkdf_sha384
    crypto/mac/hmac_sha256
    crypto/mac/hmac_sha384
)
