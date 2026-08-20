# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/ssh/common
  HEADER_ONLY
  DEPS
    crypto/aead/chachapoly
    crypto/asymmetric/bignum
    crypto/cipher/aes256ctr
    crypto/mac/hmac_sha256
    crypto/pqc/sntrup761
    mmgr/bytes
    mmgr/protostr
    network_drivers/presentation/ssh/transport/ssh_kexhash
)
