# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(network_drivers/session/scp/scp
  GATE PROTOCORE_ENABLE_SSH_SCP
  SOURCES scp.c
  PRIVATE_DEPS
    mmgr/membuild
    mmgr/protomem
)
