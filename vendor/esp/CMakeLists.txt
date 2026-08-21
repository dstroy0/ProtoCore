# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# What an ESP part answers to the capability questions: the one vendor header the platform layer
# reads its seams out of.

protocore_add_module(vendor/esp
  HEADER_ONLY
  DEPS
    vendor
)
