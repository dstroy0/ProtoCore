// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntp_service.c
 * @brief SNTP wall-clock time sync implementation (PROTOCORE_ENABLE_NTP).
 *
 * One client, ours: it asks a server over the UDP listener, checks the reply answers the request it
 * sent, and keeps the epoch in its own state.
 */

#include "ntp_service.h"
#include "shared_primitives/http_date.h" // protocore_http_date() - the shared IMF-fixdate formatter
#include <time.h>                        // time_t: the epoch this module reports

#if PROTOCORE_ENABLE_NTP

#include "mmgr/endian.h"                         // protocore_rd32be / protocore_wr32be: the timestamp fields
#include "mmgr/secure.h"                         // protocore_secure_persist_span: this module's storage
#include "network_drivers/application/ntp/ntp.h" // the packet this role asks with
#include "network_drivers/transport/udp.h"       // Udp.listener: the client port and the ask
#include "server/clock/clock.h"                  // protocore_millis: how the epoch advances between syncs
#include "shared_primitives/ip.h"                // Ip.parse: a server given as a literal address

// A successful sync moves the clock well past this sentinel (2021-01-01 UTC);
// a cold-booted RTC sits near the Unix epoch.
static const time_t PROTOCORE_NTP_PLAUSIBLE_EPOCH = 1609459200;

// All SNTP client state, owned by one instance (internal linkage): the epoch the last reply
// carried, the millisecond it arrived so the clock can advance between syncs, the cookie that
// reply had to echo, and the request buffer. One named owner, unreachable cross-TU.
typedef struct
{
    time_t epoch;       ///< Unix seconds at the moment of the last accepted reply, 0 when never synced
    uint32_t sync_ms;   ///< protocore_millis() when that reply arrived
    uint32_t cookie;    ///< the transmit stamp the reply must echo as its origin
    protocore_span req; ///< the request in flight, borrowed once and held
} NtpSvcCtx;
static NtpSvcCtx s_ntp_svc = {0, 0, 0, {NULL, 0, 0, PROTO_FALSE}};

// Take the request borrow on first use and hold it for the life of the program. The cookie it
// carries is what authenticates the reply, so the bytes come from the secure pool, whose release
// wipes. False when the pool cannot cover it, and begin() fails closed on that.
static proto_bool ntp_mem_bind(void)
{
    if (protocore_span_has_storage(s_ntp_svc.req))
    {
        return PROTO_TRUE;
    }
    s_ntp_svc.req = protocore_secure_persist_span(PROTOCORE_NTP_PACKET_LEN);
    return protocore_span_has_storage(s_ntp_svc.req);
}

/**
 * @brief Take one server reply.
 *
 * Refuses anything that is not a mode-4 answer to the question this client asked: the origin field
 * has to carry the cookie the request went out with, which is what stops an off-path packet setting
 * the clock (RFC 4330 sec 5). Stratum 0 is a kiss-o'-death rather than a time.
 */
static void ntp_reply(const uint8_t *data, size_t len, const struct protocore_udp_peer *peer, void *ctx)
{
    (void)peer;
    (void)ctx;
    if (len < PROTOCORE_NTP_PACKET_LEN)
    {
        return;
    }
    if (PROTOCORE_NTP_MODE_OF(data[0]) != PROTOCORE_NTP_MODE_SERVER)
    {
        return;
    }
    if (data[1] == PROTOCORE_NTP_STRATUM_KOD || data[1] > PROTOCORE_NTP_STRATUM_MAX)
    {
        return; // stratum 0 is a kiss-o'-death; past 15 is unsynchronized
    }
    if (protocore_rd32be(data + PROTOCORE_NTP_OFF_ORIGIN_SEC) != s_ntp_svc.cookie)
    {
        return; // not an answer to what this client asked
    }
    uint32_t secs = protocore_rd32be(data + PROTOCORE_NTP_OFF_TX_SEC);
    if (secs <= PROTOCORE_NTP_UNIX_OFFSET)
    {
        return;
    }
    time_t epoch = (time_t)(secs - PROTOCORE_NTP_UNIX_OFFSET);
    if (epoch <= PROTOCORE_NTP_PLAUSIBLE_EPOCH)
    {
        return; // a server that answers with a pre-2021 clock is not one to follow
    }
    s_ntp_svc.epoch = epoch;
    s_ntp_svc.sync_ms = protocore_millis();
}

proto_bool protocore_ntp_begin(const char *tz, const char *server1, const char *server2)
{
    (void)tz; // the epoch this client reports is UTC; nothing here formats a local time
    (void)server2;
    const char *host = PROTOCORE_NTP_SERVER1;
    if (server1 != NULL)
    {
        host = server1;
    }
    protocore_ip dst = {PROTOCORE_IP_NONE, {0}};
    if (!ntp_mem_bind() || !Ip.parse(host, &dst))
    {
        return PROTO_FALSE; // no storage, or a name and this client has no resolver of its own
    }
    // Bind every time rather than remembering: the listener rebinds a port it already holds, and a
    // port closed underneath this client is exactly the case a remembered flag would send a datagram
    // from a slot that no longer exists.
    if (!Udp.listener->listen(PROTOCORE_NTP_CLIENT_PORT, ntp_reply, NULL))
    {
        return PROTO_FALSE;
    }
    // The transmit stamp doubles as the cookie the reply has to echo. Ticks, not a clock: this runs
    // before there is one.
    s_ntp_svc.cookie = protocore_millis() | 1u;
    uint8_t *req = s_ntp_svc.req.buf;
    for (size_t i = 0; i < PROTOCORE_NTP_PACKET_LEN; i++)
    {
        req[i] = 0;
    }
    req[0] = PROTOCORE_NTP_LI_VN_MODE(PROTOCORE_NTP_LI_NONE, PROTOCORE_NTP_VERSION, PROTOCORE_NTP_MODE_CLIENT);
    protocore_wr32be(req + PROTOCORE_NTP_OFF_TX_SEC, s_ntp_svc.cookie);
    return Udp.listener->sendto(PROTOCORE_NTP_CLIENT_PORT, &dst, PROTOCORE_NTP_PORT, req, PROTOCORE_NTP_PACKET_LEN);
}

proto_bool protocore_ntp_synced(void)
{
    return s_ntp_svc.epoch != 0;
}

time_t protocore_ntp_epoch(void)
{
    if (s_ntp_svc.epoch == 0)
    {
        return 0;
    }
    // The reply fixed one instant; the monotonic clock carries it forward from there.
    uint32_t elapsed = protocore_millis() - s_ntp_svc.sync_ms;
    return s_ntp_svc.epoch + (time_t)(elapsed / 1000u);
}

void protocore_ntp_set_test_epoch(time_t epoch)
{
    s_ntp_svc.epoch = epoch;
    s_ntp_svc.sync_ms = protocore_millis();
}

size_t protocore_ntp_http_date(char *out, size_t out_cap)
{
    return protocore_http_date(protocore_ntp_epoch(), out, out_cap);
}

#else // PROTOCORE_ENABLE_NTP == 0

size_t protocore_ntp_http_date(char *out, size_t out_cap)
{
    return protocore_http_date(0, out, out_cap);
}

#endif // PROTOCORE_ENABLE_NTP

#if PROTOCORE_ENABLE_NTP
// NTP as a registry time source (protocore_ntp_epoch is 0 until a reply lands). Register it with
// protocore_time_source_add() so the aggregated protocore_time_now() - and the HTTP Date header - can be fed by
// NTP alongside an RTC / GPS.
uint32_t protocore_ntp_time_source(void)
{
    return (uint32_t)protocore_ntp_epoch();
}
#endif
