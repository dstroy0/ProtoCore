# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/ssh/transport/ssh_rsa
  GATE PROTOCORE_ENABLE_SSH_RSA
  SOURCES ssh_rsa.c
  DEPS
    PRIVATE
    crypto/asymmetric/rsa
    mmgr/protomem
    mmgr/secure
  PRIVATE_DEPS
    mmgr/protomem
    mmgr/secure
)
