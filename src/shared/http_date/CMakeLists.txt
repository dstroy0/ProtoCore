# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later

protocore_add_module(shared/http_date
  SOURCES http_date.c
  DEPS
    PRIVATE
    mmgr/membuild
    shared/time_compat
  PRIVATE_DEPS
    mmgr/membuild
)
