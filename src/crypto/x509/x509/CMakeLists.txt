# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(crypto/x509/x509
  GATE PROTOCORE_ENABLE_X509
  SOURCES x509.c
  DEPS
    crypto/x509/x509_types
  PRIVATE_DEPS
    mmgr/protomem
    shared/der
)
