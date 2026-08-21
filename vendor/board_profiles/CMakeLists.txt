# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Per-variant default sizing: which flash and PSRAM a part has, and what that makes the pool
# defaults. Reached before the widths, so a profile can state PROTOCORE_HW_WORD_BITS.

protocore_add_module(vendor/board_profiles
  HEADER_ONLY
  DEPS
    vendor
)

add_subdirectory(esp)
