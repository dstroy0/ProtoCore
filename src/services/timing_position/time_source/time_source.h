// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file time_source.h
 * @brief Multi-source time fallback matrix (PC_ENABLE_TIME_SOURCE).
 *
 * A small, zero-heap registry of user-defined time sources (NTP, an RTC, GPS, a
 * manually-set clock, ...). Each source is a callback returning the current Unix
 * epoch seconds, or 0 when that source currently has no valid time. Sources are
 * registered with a priority; pc_time_now() queries them in ascending priority
 * order and returns the first nonzero result, so the device falls back
 * automatically when its preferred clock is unavailable (e.g. GPS loses its fix
 * -> RTC -> NTP). The validity rule lives in each user callback (return 0 when
 * invalid/stale), keeping this layer a pure prioritizer.
 *
 * Everything lives in a fixed BSS table (PC_TIME_SOURCE_MAX entries); no heap.
 * The whole core is host-testable. The API is declared unconditionally and
 * compiles to no-op stubs when PC_ENABLE_TIME_SOURCE is 0.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TIME_SOURCE_H
#define PROTOCORE_TIME_SOURCE_H

#include "protocore_config.h"

/**
 * @brief A time source: returns the current Unix epoch seconds for this source,
 *        or 0 if it currently has no valid time.
 */
typedef uint32_t (*TimeSourceFn)(void);

/**
 * @brief Register a time source.
 *
 * @param name      stable label (referenced by pointer; must outlive the source -
 *                  point it at a string literal / static, like the rest of the lib).
 * @param priority  lower value = higher priority (queried first).
 * @param fn        the source callback.
 * @return true if registered; false if @p fn is null or the table is full.
 */
proto_bool pc_time_source_add(const char *name, uint8_t priority, TimeSourceFn fn);

/**
 * @brief Current best time.
 *
 * Queries registered sources in ascending priority and returns the first nonzero
 * epoch (stopping at the first valid source). Returns 0 if none have valid time.
 */
uint32_t pc_time_now(void);

/** @brief Name of the source that satisfied the last pc_time_now(), or nullptr. */
const char *pc_time_source_active(void);

/** @brief Clear all registered sources. */
void pc_time_source_reset(void);

/**
 * @brief The current best time (pc_time_now, any registered NTP / GPS / RTC / ... source)
 *        formatted as an RFC 7231 IMF-fixdate into @p out.
 * @return bytes written, or 0 with an empty @p out when no source currently has a valid time.
 *
 * This is what lets the HTTP `Date:` header be fed by whatever time source is enabled, not just
 * NTP: register RTC / GPS / NTP via pc_time_source_add() and the header follows the priority.
 */
size_t pc_time_http_date(char *out, size_t out_cap);

#endif // PROTOCORE_TIME_SOURCE_H
