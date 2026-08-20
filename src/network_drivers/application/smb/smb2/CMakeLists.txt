# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/application/smb/smb2
  GATE PROTOCORE_ENABLE_SMB
  SOURCES smb2.c
  PRIVATE_DEPS
    crypto/aead/aes128gcm
    crypto/aead/aesccm
    crypto/aead/aesgcm
    crypto/hash/sha512
    crypto/kdf/kdf
    crypto/mac/aes_cmac
    crypto/mac/hmac_sha256
    mmgr/endian
    mmgr/protomem
    mmgr/secure
)
