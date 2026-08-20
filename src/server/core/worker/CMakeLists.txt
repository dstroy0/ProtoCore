# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(server/core/worker
  GATE PROTOCORE_ENABLE_PREEMPT_QUEUE
  SOURCES worker.c
  DEPS
    PRIVATE
    mmgr/arena
    mmgr/plaintext
    mmgr/ring
    server/core/preempt_queue
  PRIVATE_DEPS
    mmgr/arena
    mmgr/plaintext
    mmgr/ring
)
