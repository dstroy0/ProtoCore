# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/presentation/http/http3/tls13_msg
  GATE PROTOCORE_ENABLE_PQC_KEX
  SOURCES tls13_msg.c
  PRIVATE_DEPS
    crypto/asymmetric/ed25519
    crypto/pqc/mlkem
    mmgr/protomem
    mmgr/protostr
)
