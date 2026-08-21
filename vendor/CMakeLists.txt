# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The vendor axis. Header-only: these declare what a part has and how to reach it, and every
# definition behind them is the vendor SDK's, not this tree's.
#
# src/ reaches this tree through three headers - vendor_detect.h, vendor_hal.h and
# board_profiles/board_profile.h - and the other 24 hang off those. So it is declared as one module
# per directory rather than one per header: a consumer depends on the axis, not on which header of
# it answered the question.

protocore_add_module(vendor
  HEADER_ONLY
)

add_subdirectory(board_profiles)
add_subdirectory(esp)
