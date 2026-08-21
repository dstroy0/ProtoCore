# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(crypto/pqc/mlkem
  GATE PROTOCORE_ENABLE_MLKEM
  OPT 2
  SOURCES mlkem.c
  PRIVATE_DEPS
    crypto/hash/sha3
    mmgr/protomem
)
