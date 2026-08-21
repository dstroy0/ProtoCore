# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The ESP dies and their flash / PSRAM sizes. One header per die and per part size; the profile
# above picks among them from what the SDK reports.

protocore_add_module(vendor/board_profiles/esp
  HEADER_ONLY
  DEPS
    vendor/board_profiles
)
