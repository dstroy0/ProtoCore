# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/storage/partition_monitor
  GATE PROTOCORE_ENABLE_PARTITION_MONITOR
  SOURCES partition_monitor.c
  PRIVATE_DEPS
    mmgr/protoframe
    mmgr/protostr
)

add_subdirectory(partition_monitor_routes)


