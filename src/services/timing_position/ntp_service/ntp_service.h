// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntp_service.h
 * @brief Optional SNTP wall-clock time sync (PC_ENABLE_NTP).
 *
 * Thin wrapper over the ESP-IDF SNTP client (`configTzTime`). Gives a
 * clock-less device wall-clock time for log timestamps, the HTTP `Date` header,
 * and future TLS certificate-validity checks. No-op stub when PC_ENABLE_NTP
 * is 0 or on non-Arduino builds.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_NTP_SERVICE_H
#define PROTOCORE_NTP_SERVICE_H

#include "protocore_config.h"
#include <time.h>

#if PC_ENABLE_NTP

/** @brief Server this asks when the caller names none. */
#define PC_NTP_SERVER1 "pool.ntp.org"

/** @brief Server this falls back to when the caller names none. */
#define PC_NTP_SERVER2 "time.nist.gov"

/**
 * @brief Start the SNTP client.
 *
 * Returns immediately; the first sync arrives asynchronously (poll
 * pc_ntp_synced()). Call once after the WiFi link is up.
 *
 * Every parameter takes NULL to mean the documented default, so a caller that
 * has no opinion passes NULL rather than restating the string.
 *
 * @param tz       POSIX TZ string (e.g. "UTC0", "EST5EDT,M3.2.0,M11.1.0"). NULL selects UTC.
 * @param server1  Primary NTP server. NULL selects PC_NTP_SERVER1.
 * @param server2  Secondary NTP server. NULL selects PC_NTP_SERVER2.
 * @return true if the client was started; false if disabled at compile time.
 */
proto_bool pc_ntp_begin(const char *tz, const char *server1, const char *server2);

/**
 * @brief True once a plausible wall-clock time has been obtained from SNTP.
 *
 * Checks that the system clock has advanced past 2021-01-01, which only happens
 * after a successful sync (the RTC starts at the epoch on a cold boot).
 */
proto_bool pc_ntp_synced(void);

/**
 * @brief Current Unix epoch seconds, or 0 if not yet synced (or disabled).
 */
time_t pc_ntp_epoch(void);

/**
 * @brief Format the current time as an RFC 7231 IMF-fixdate (HTTP `Date`).
 *
 * Writes e.g. "Sun, 06 Nov 1994 08:49:37 GMT" into @p out. Always GMT.
 *
 * @param out      Destination buffer (>= 30 bytes recommended).
 * @param out_cap  Capacity of @p out.
 * @return Number of characters written (excluding the null), or 0 if time is
 *         not yet available / disabled.
 */
size_t pc_ntp_http_date(char *out, size_t out_cap);

/**
 * @brief NTP as a time source for the multi-source registry (services/timing_position/time_source).
 *
 * Register with pc_time_source_add("ntp", priority, pc_ntp_time_source) so the aggregated
 * pc_time_now() (and thus the HTTP `Date` header when PC_ENABLE_TIME_SOURCE is set) can
 * be fed by NTP alongside an RTC / GPS. Returns the current epoch, or 0 when not synced.
 */
uint32_t pc_ntp_time_source(void);

#if PROTOCORE_HOST
/**
 * @brief Host-only test seam: inject a wall-clock epoch so time-dependent paths
 *        (e.g. the optional HTTP Date header) are exercisable off-device. 0 = none.
 */
void pc_ntp_set_test_epoch(time_t epoch);
#endif // PROTOCORE_HOST

#endif // PC_ENABLE_NTP

#endif // PROTOCORE_NTP_SERVICE_H
