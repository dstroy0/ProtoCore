// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntp_service.c
 * @brief SNTP wall-clock time sync implementation (PC_ENABLE_NTP).
 */

#include "ntp_service.h"
#include "shared_primitives/http_date.h" // pc_http_date() - the shared IMF-fixdate formatter
#include <time.h>                        // time_t / time() - used by both the Arduino SNTP and host test-seam paths

#if PC_ENABLE_NTP && PROTOCORE_HOT

// A successful sync moves the clock well past this sentinel (2021-01-01 UTC);
// a cold-booted RTC sits near the Unix epoch.
static const time_t PC_NTP_PLAUSIBLE_EPOCH = 1609459200;

proto_bool pc_ntp_begin(const char *tz, const char *server1, const char *server2)
{
    // configTzTime applies the POSIX TZ and starts the SNTP client (async). NULL means the
    // documented default, so a caller with no opinion does not restate the string.
    configTzTime(tz != NULL ? tz : "UTC0", server1 != NULL ? server1 : PC_NTP_SERVER1,
                 server2 != NULL ? server2 : PC_NTP_SERVER2);
    return PROTO_TRUE;
}

proto_bool pc_ntp_synced(void)
{
    return time(NULL) > PC_NTP_PLAUSIBLE_EPOCH;
}

time_t pc_ntp_epoch(void)
{
    time_t now = time(NULL);
    return (now > PC_NTP_PLAUSIBLE_EPOCH) ? now : 0;
}

size_t pc_ntp_http_date(char *out, size_t out_cap)
{
    return pc_http_date(pc_ntp_epoch(), out, out_cap);
}

#else

// Host build: no SNTP. A test seam lets a unit test inject a wall-clock epoch so
// the Date-header path (and any time-dependent code) is exercisable off-device.
// All host NTP test-seam state, owned by one instance (internal linkage): the injected
// wall-clock epoch, so it is one named owner, unreachable from any other translation unit.
typedef struct
{
    time_t host_test_epoch;
} NtpSvcCtx;
static NtpSvcCtx s_ntp_svc = {0};
void pc_ntp_set_test_epoch(time_t epoch)
{
    s_ntp_svc.host_test_epoch = epoch;
}

proto_bool pc_ntp_begin(const char *tz, const char *server1, const char *server2)
{
    (void)tz;
    (void)server1;
    (void)server2;
    return PROTO_FALSE;
}
proto_bool pc_ntp_synced(void)
{
    return s_ntp_svc.host_test_epoch != 0;
}
time_t pc_ntp_epoch(void)
{
    return s_ntp_svc.host_test_epoch;
}
size_t pc_ntp_http_date(char *out, size_t out_cap)
{
    return pc_http_date(pc_ntp_epoch(), out, out_cap);
}

#endif // PC_ENABLE_NTP && PROTOCORE_HOT

// NTP as a registry time source (defined for both the device and host builds; pc_ntp_epoch is 0
// until synced / when no test epoch is injected). Register it with pc_time_source_add() so the
// aggregated pc_time_now() - and the HTTP Date header - can be fed by NTP alongside an RTC / GPS.
uint32_t pc_ntp_time_source(void)
{
    return (uint32_t)pc_ntp_epoch();
}
