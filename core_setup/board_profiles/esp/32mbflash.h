// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file 32mbflash.h
 * @brief 32 MB flash profile - flash-backed default sizing.
 *
 * The flash-size axis, independent of the chip and PSRAM. Define here any default that should
 * scale with available flash (on-flash asset/response cache, OTA staging, flash-backed store).
 * No library default keys on flash size yet, so this is a structural placeholder; keep any
 * default added here `#ifndef`-guarded so -D overrides win.
 */

#ifndef PROTOCORE_32MBFLASH_H
#define PROTOCORE_32MBFLASH_H

// (no flash-scaled defaults yet - see file comment)

#endif // PROTOCORE_32MBFLASH_H
