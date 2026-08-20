# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/ssh/transport/comp
  GATE PROTOCORE_ENABLE_SSH_ZLIB
  SOURCES comp.c
  PRIVATE_DEPS
    mmgr/plaintext
    mmgr/secure
    network_drivers/presentation/ssh/common
    network_drivers/presentation/ssh/transport/inflate
    network_drivers/presentation/ssh/transport/zlib
)
