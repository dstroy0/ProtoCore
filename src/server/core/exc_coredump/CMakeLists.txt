# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/core/exc_coredump
  SOURCES exc_coredump.c
  PRIVATE_DEPS
    mmgr/protomem
    mmgr/protostr
    server/core/exc_decoder
)
