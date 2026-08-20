# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(services/net/snmp/snmp_crypto
  GATE PROTOCORE_ENABLE_SNMP_V3
  SOURCES snmp_crypto.c
  PRIVATE_DEPS
    crypto/cipher/aes_sbox
    crypto/hash/sha256
    mmgr/protomem
    mmgr/protostr
    mmgr/secure
)
