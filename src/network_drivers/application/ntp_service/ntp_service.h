// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntp_service.h
 * @brief Optional SNTP wall-clock time sync (PC_ENABLE_NTP).
 *
 * Starts a client, reports sync state, and formats the current time.
 *
 * Two backends, chosen by PC_HAS_VENDOR_SNTP. Where the SDK ships an SNTP client the wrapper starts
 * that one and it disciplines the system clock, so `time()` answers everywhere. Where it does not,
 * the portable client asks a server over the UDP listener, checks the reply echoes the request it
 * answers, and keeps the epoch in its own state; the monotonic clock carries it between syncs and
 * nothing else in libc moves. The portable client takes a literal address rather than a name - it
 * has no resolver of its own - and reports UTC, so the POSIX TZ argument is only meaningful to the
 * vendor backend.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_NTP_SERVICE_H
#define PROTOCORE_NTP_SERVICE_H

#include "protocore_config.h"
#include <time.h>

PROTO_BEGIN_DECLS

/**
 * @brief Format the current time as an RFC 7231 IMF-fixdate (HTTP `Date`).
 *
 * Writes e.g. "Sun, 06 Nov 1994 08:49:37 GMT" into @p out. Always GMT.
 *
 * Declared outside the PC_ENABLE_NTP gate because ntp_service.c defines it on both arms of its own
 * gate, and PC_HTTP_EMIT_DATE reaches it whenever no PC_ENABLE_TIME_SOURCE registry is built.
 *
 * @param out      Destination buffer (>= 30 bytes recommended).
 * @param out_cap  Capacity of @p out.
 * @return Number of characters written (excluding the null), or 0 if time is
 *         not yet available / disabled.
 */
size_t pc_ntp_http_date(char *out, size_t out_cap);

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
 * @param tz     POSIX TZ string (e.g. "UTC0", "EST5EDT,M3.2.0,M11.1.0"). NULL selects UTC.
 * @param server1  Primary NTP server. NULL selects PC_NTP_SERVER1.
 * @param server2  Secondary NTP server. NULL selects PC_NTP_SERVER2.
 * @return true if the client was started; false if disabled at compile time.
 */
proto_bool pc_ntp_begin(const char *tz, const char *server1, const char *server2);

/**
 * @brief True once a plausible wall-clock time has been obtained from SNTP.
 *
 * Checks that the system clock has advanced past 2021-01-01.
 */
proto_bool pc_ntp_synced(void);

/**
 * @brief Current Unix epoch seconds, or 0 if not yet synced (or disabled).
 */
time_t pc_ntp_epoch(void);

/**
 * @brief NTP as a time source for the multi-source registry (services/timing_position/time_source).
 *
 * Register with pc_time_source_add("ntp", priority, pc_ntp_time_source). Returns the
 * current epoch, or 0 when not synced.
 */
uint32_t pc_ntp_time_source(void);

#if !PC_HAS_VENDOR_SNTP
/**
 * @brief Seed the clock without asking a server: the accessors above report @p epoch from now on.
 *
 * The portable client keeps the epoch itself, so setting it is the same operation a reply performs.
 * A caller that already knows the time (an RTC, a provisioning step, a test) uses this instead of a
 * round trip. 0 puts the client back to never-synced. Absent on the vendor backend, where the SDK
 * client owns the system clock.
 */
void pc_ntp_set_test_epoch(time_t epoch);
#endif // !PC_HAS_VENDOR_SNTP

#endif // PC_ENABLE_NTP

PROTO_END_DECLS

#endif // PROTOCORE_NTP_SERVICE_H
