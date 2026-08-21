# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(crypto/ct_eq
  GATE PROTOCORE_ENABLE_CT_EQ
  SOURCES ct_eq.c
  DEPS
    mmgr/protomem
  PRIVATE_DEPS
    crypto/crypto_opt
)
