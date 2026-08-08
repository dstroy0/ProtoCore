// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntp_service.c
 * @brief SNTP wall-clock time sync implementation (PC_ENABLE_NTP).
 *
 * Two backends behind one API, picked by PC_HAS_VENDOR_SNTP: the SDK's client where there is one,
 * and otherwise the portable client below, which asks over the UDP listener and keeps the answer.
 */

#include "ntp_service.h"
#include "shared_primitives/http_date.h" // pc_http_date() - the shared IMF-fixdate formatter
#include <time.h>                        // time_t: the epoch this module reports

#if PC_ENABLE_NTP

#if PC_HAS_VENDOR_SNTP
#include <Arduino.h> // configTzTime: the SDK's client, which disciplines the system clock
#else
#include "mmgr/endian.h"                                       // pc_rd32be / pc_wr32be: the timestamp fields
#include "mmgr/secure.h"                                       // pc_secure_persist_span: this module's storage
#include "network_drivers/application/ntp_server/ntp_server.h" // NTP_PACKET_LEN / NTP_UNIX_OFFSET
#include "network_drivers/transport/udp.h"                     // Udp.listener: the client port and the ask
#include "server/clock/clock.h"                                // pc_millis: how the epoch advances between syncs
#include "shared_primitives/ip.h"                              // Ip.parse: a server given as a literal address
#endif

// A successful sync moves the clock well past this sentinel (2021-01-01 UTC);
// a cold-booted RTC sits near the Unix epoch.
static const time_t PC_NTP_PLAUSIBLE_EPOCH = 1609459200;

#if PC_HAS_VENDOR_SNTP

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

#else // the portable client

// Mode 3 is a client asking, mode 4 is a server answering (RFC 5905 sec 7.3). Version 4 in the
// three bits above it: 0x23 is LI 0 | VN 4 | Mode 3.
#define PC_NTP_REQ_BYTE0 0x23u
#define PC_NTP_MODE_MASK 0x07u
#define PC_NTP_MODE_SERVER 4u

// Where the transmit timestamp sits in a packet, and where a reply echoes the one we sent.
#define PC_NTP_OFF_ORIGIN 24u
#define PC_NTP_OFF_XMIT 40u

// All SNTP client state, owned by one instance (internal linkage): the epoch the last reply
// carried, the millisecond it arrived so the clock can advance between syncs, the cookie that
// reply had to echo, and the request buffer. One named owner, unreachable cross-TU.
typedef struct
{
    time_t epoch;     ///< Unix seconds at the moment of the last accepted reply, 0 when never synced
    uint32_t sync_ms; ///< pc_millis() when that reply arrived
    uint32_t cookie;  ///< the transmit stamp the reply must echo as its origin
    pc_span req;      ///< the request in flight, borrowed once and held
} NtpSvcCtx;
static NtpSvcCtx s_ntp_svc = {0, 0, 0, {NULL, 0, 0, PROTO_FALSE}};

// Take the request borrow on first use and hold it for the life of the program. The cookie it
// carries is what authenticates the reply, so the bytes come from the secure pool, whose release
// wipes. False when the pool cannot cover it, and begin() fails closed on that.
static proto_bool ntp_mem_bind(void)
{
    if (pc_span_has_storage(s_ntp_svc.req))
    {
        return PROTO_TRUE;
    }
    s_ntp_svc.req = pc_secure_persist_span(NTP_PACKET_LEN);
    return pc_span_has_storage(s_ntp_svc.req);
}

/**
 * @brief Take one server reply.
 *
 * Refuses anything that is not a mode-4 answer to the question this client asked: the origin field
 * has to carry the cookie the request went out with, which is what stops an off-path packet setting
 * the clock (RFC 4330 sec 5). Stratum 0 is a kiss-o'-death rather than a time.
 */
static void ntp_reply(const uint8_t *data, size_t len, const struct pc_udp_peer *peer, void *ctx)
{
    (void)peer;
    (void)ctx;
    if (len < NTP_PACKET_LEN)
    {
        return;
    }
    if ((data[0] & PC_NTP_MODE_MASK) != PC_NTP_MODE_SERVER)
    {
        return;
    }
    if (data[1] == 0 || data[1] > 15)
    {
        return; // stratum 0 is a kiss-o'-death; past 15 is unsynchronized
    }
    if (pc_rd32be(data + PC_NTP_OFF_ORIGIN) != s_ntp_svc.cookie)
    {
        return; // not an answer to what this client asked
    }
    uint32_t secs = pc_rd32be(data + PC_NTP_OFF_XMIT);
    if (secs <= NTP_UNIX_OFFSET)
    {
        return;
    }
    time_t epoch = (time_t)(secs - NTP_UNIX_OFFSET);
    if (epoch <= PC_NTP_PLAUSIBLE_EPOCH)
    {
        return; // a server that answers with a pre-2021 clock is not one to follow
    }
    s_ntp_svc.epoch = epoch;
    s_ntp_svc.sync_ms = pc_millis();
}

proto_bool pc_ntp_begin(const char *tz, const char *server1, const char *server2)
{
    (void)tz; // the epoch this client reports is UTC; nothing here formats a local time
    (void)server2;
    const char *host = PC_NTP_SERVER1;
    if (server1 != NULL)
    {
        host = server1;
    }
    pc_ip dst = {PC_IP_NONE, {0}};
    if (!ntp_mem_bind() || !Ip.parse(host, &dst))
    {
        return PROTO_FALSE; // no storage, or a name and this client has no resolver of its own
    }
    // Bind every time rather than remembering: the listener rebinds a port it already holds, and a
    // port closed underneath this client is exactly the case a remembered flag would send a datagram
    // from a slot that no longer exists.
    if (!Udp.listener->listen(PC_NTP_CLIENT_PORT, ntp_reply, NULL))
    {
        return PROTO_FALSE;
    }
    // The transmit stamp doubles as the cookie the reply has to echo. Ticks, not a clock: this runs
    // before there is one.
    s_ntp_svc.cookie = pc_millis() | 1u;
    uint8_t *req = s_ntp_svc.req.buf;
    for (size_t i = 0; i < NTP_PACKET_LEN; i++)
    {
        req[i] = 0;
    }
    req[0] = PC_NTP_REQ_BYTE0;
    pc_wr32be(req + PC_NTP_OFF_XMIT, s_ntp_svc.cookie);
    return Udp.listener->sendto(PC_NTP_CLIENT_PORT, &dst, 123, req, NTP_PACKET_LEN);
}

proto_bool pc_ntp_synced(void)
{
    return s_ntp_svc.epoch != 0;
}

time_t pc_ntp_epoch(void)
{
    if (s_ntp_svc.epoch == 0)
    {
        return 0;
    }
    // The reply fixed one instant; the monotonic clock carries it forward from there.
    uint32_t elapsed = pc_millis() - s_ntp_svc.sync_ms;
    return s_ntp_svc.epoch + (time_t)(elapsed / 1000u);
}

void pc_ntp_set_test_epoch(time_t epoch)
{
    s_ntp_svc.epoch = epoch;
    s_ntp_svc.sync_ms = pc_millis();
}

#endif // PC_HAS_VENDOR_SNTP

size_t pc_ntp_http_date(char *out, size_t out_cap)
{
    return pc_http_date(pc_ntp_epoch(), out, out_cap);
}

#else // PC_ENABLE_NTP == 0

size_t pc_ntp_http_date(char *out, size_t out_cap)
{
    return pc_http_date(0, out, out_cap);
}

#endif // PC_ENABLE_NTP

#if PC_ENABLE_NTP
// NTP as a registry time source (pc_ntp_epoch is 0 until a reply lands). Register it with
// pc_time_source_add() so the aggregated pc_time_now() - and the HTTP Date header - can be fed by
// NTP alongside an RTC / GPS.
uint32_t pc_ntp_time_source(void)
{
    return (uint32_t)pc_ntp_epoch();
}
#endif
