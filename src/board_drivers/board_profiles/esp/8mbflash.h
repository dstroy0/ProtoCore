// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file 8mbflash.h
 * @brief 8 MB flash profile - flash-backed default sizing.
 *
 * The flash-size axis, independent of the chip and PSRAM (a given chip ships in several flash
 * densities). Define here any default that should scale with available flash - e.g. an
 * on-flash asset / response cache, OTA staging, or a flash-backed store. No default in the
 * library keys on flash size yet, so this profile is a structural placeholder for now;
 * anything added here must stay `#ifndef`-guarded so -D overrides win.
 */

#ifndef PROTOCORE_8MBFLASH_H
#define PROTOCORE_8MBFLASH_H

// (no flash-scaled defaults yet - see file comment)

#endif // PROTOCORE_8MBFLASH_H
