// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntp_service.h
 * @brief Optional SNTP wall-clock time sync (PROTOCORE_ENABLE_NTP).
 *
 * Starts a client, reports sync state, and formats the current time.
 *
 * One client, the library's own: it asks a server over the UDP listener, checks the reply echoes the
 * request it answers, and keeps the epoch in its own state; the monotonic clock carries it between
 * syncs and nothing in libc moves. It takes a literal address rather than a name - it has no resolver
 * of its own - and reports UTC, so the POSIX TZ argument is accepted and ignored.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_NTP_SERVICE_H
#define PROTOCORE_NTP_SERVICE_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @brief Format the current time as an RFC 7231 IMF-fixdate (HTTP `Date`), e.g.
 *        "Sun, 06 Nov 1994 08:49:37 GMT". Always GMT.
 *
 * Not an entry, and outside the gate below: ntp_service.c defines it on both arms of its own gate,
 * and server/io/response.c reaches it whenever no PROTOCORE_ENABLE_TIME_SOURCE registry is built,
 * whether or not NTP itself is.
 *
 * @param out      Destination buffer (>= 30 bytes recommended).
 * @param out_cap  Capacity of @p out.
 * @return characters written excluding the null, or 0 when no time is available.
 */
size_t protocore_ntp_http_date(char *out, size_t out_cap);

PROTOCORE_END_DECLS

#if PROTOCORE_ENABLE_NTP

PROTOCORE_BEGIN_DECLS

// PROTOCORE_NTP_SERVICE_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

/** @brief Server this asks when the caller names none. */
#define PROTOCORE_NTP_SERVER1 "pool.ntp.org"

/** @brief Server this falls back to when the caller names none. */
#define PROTOCORE_NTP_SERVER2 "time.nist.gov"

/** @brief What begin takes: tz, server1, server2. */
typedef struct
{
    const char *tz;      ///< POSIX TZ string (e.g. "UTC0", "EST5EDT,M3.2.0,M11.1.0"). NULL selects UTC
    const char *server1; ///< Primary NTP server. NULL selects PROTOCORE_NTP_SERVER1
    const char *server2; ///< Secondary NTP server. NULL selects PROTOCORE_NTP_SERVER2
} NtpServiceBeginArgs;
/** @brief What set_test_epoch takes: epoch. */
typedef struct
{
    time_t epoch;
} NtpServiceSetTestEpochArgs;
/**
 * @brief Optional SNTP wall-clock time sync (PROTOCORE_ENABLE_NTP).
 *
 * A caller sets the members a call takes, invokes it through ::NtpService with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   NtpService.begin_args.tz = ...;
 *   NtpService.begin_args.server1 = ...;
 *   NtpService.begin_args.server2 = ...;
 *   NtpService.begin(work);
 *   // NtpService.ok is what the call reports
 *
 * @var NtpServiceNs::begin_args  what begin takes: tz, server1, server2
 * @var NtpServiceNs::set_test_epoch_args  what set_test_epoch takes: epoch
 * @var NtpServiceNs::ok  true if the client was started; false if disabled at compile time
 * @var NtpServiceNs::value  the value a call reports
 * @var NtpServiceNs::ms  the milliseconds a call reports
 * @var NtpServiceNs::begin  start the SNTP client. Returns immediately; the first sync arrives ...
 * @var NtpServiceNs::synced  true once a plausible wall-clock time has been obtained from SNTP. ...
 * @var NtpServiceNs::epoch  current Unix epoch seconds, or 0 if not yet synced (or disabled)
 * @var NtpServiceNs::time_source  NTP as a time source for the multi-source registry ...
 * @var NtpServiceNs::set_test_epoch  seed the clock without asking a server: the accessors above report ...
 *
 * @c work is PROTOCORE_NTP_SERVICE_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    NtpServiceBeginArgs begin_args;
    NtpServiceSetTestEpochArgs set_test_epoch_args;
    proto_bool ok;
    time_t value;
    uint32_t ms;
} NtpServiceVars;

/** @brief The operands and the outcome. */
extern NtpServiceVars NtpServiceV;

/** @brief The entries. */
typedef struct
{
    void (*const begin)(uint8_t *restrict work);
    void (*const synced)(uint8_t *restrict work);
    void (*const epoch)(uint8_t *restrict work);
    void (*const time_source)(uint8_t *restrict work);
    void (*const set_test_epoch)(uint8_t *restrict work);
} NtpServiceNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in NtpServiceV or a region of the borrow at a fixed offset.
void protocore_ntp_service_begin(uint8_t *restrict work);
void protocore_ntp_service_synced(uint8_t *restrict work);
void protocore_ntp_service_epoch(uint8_t *restrict work);
void protocore_ntp_service_time_source(uint8_t *restrict work);
void protocore_ntp_service_set_test_epoch(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `NtpService.begin(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const NtpServiceNs NtpService __attribute__((unused)) = {
    .begin = protocore_ntp_service_begin,
    .synced = protocore_ntp_service_synced,
    .epoch = protocore_ntp_service_epoch,
    .time_source = protocore_ntp_service_time_source,
    .set_test_epoch = protocore_ntp_service_set_test_epoch,
};

/**
 * @brief The PROTOCORE_NTP_SERVICE_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_ntp_service_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_NTP

#endif // PROTOCORE_NTP_SERVICE_H
