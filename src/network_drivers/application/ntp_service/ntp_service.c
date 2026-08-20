// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntp_service.c
 * @brief SNTP wall-clock time sync implementation (PROTOCORE_ENABLE_NTP).
 *
 * One client, ours: it asks a server over the UDP listener, checks the reply answers the request it
 * sent, and keeps the epoch in its own state.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

// Both arms of the gate below format the HTTP Date header, so the formatter and the borrow its
// entry takes sit above the gate rather than inside one arm.
#include "shared/http_date/http_date.h" // HttpDate.format - the shared IMF-fixdate formatter
static uint8_t http_date_work[16];      // the borrow an entry takes; HttpDate never reads it

#if PROTOCORE_ENABLE_NTP

#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "network_drivers/application/ntp_service/ntp_service.h"
#include <time.h> // time_t: the epoch this module reports

#include "mmgr/endian/endian.h"                          // endian.rd32be / endian.wr32be: the timestamp fields
#include "mmgr/secure/secure.h"                          // protocore_secure_persist_span: this module's storage
#include "network_drivers/application/ntp/ntp.h"         // the packet this role asks with
#include "network_drivers/transport/udp/server/server.h" // UdpListener: the client port and the ask
#include "server/clock/clock.h"                          // Clock.millis: how the epoch advances between syncs
#include "shared/ip/ip.h"                                // Ip.parse: a server given as a literal address

PROTOCORE_BEGIN_DECLS

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
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define NTP_SERVICE_OFF_CTX 0u
static_assert(NTP_SERVICE_OFF_CTX + sizeof(NtpSvcCtx) <= PROTOCORE_NTP_SERVICE_BORROW,
              "PROTOCORE_NTP_SERVICE_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(
    NTP_SERVICE_OFF_CTX % _Alignof(NtpSvcCtx) == 0,
    "NTP_SERVICE_OFF_CTX is not a multiple of alignof(NtpSvcCtx) - NTP_SERVICE_CTX() would return a misaligned "
    "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define NTP_SERVICE_CTX(w) ((NtpSvcCtx *)(void *)((w) + NTP_SERVICE_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_NTP_SERVICE_BORROW persistent bytes
} NtpServiceOwnCtx;
static NtpServiceOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_ntp_service_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_NTP_SERVICE_BORROW).buf;
    }
    return s_own.span;
}

// Take the request borrow on first use and hold it for the life of the program. The cookie it
// carries is what authenticates the reply, so the bytes come from the secure pool, whose release
// wipes. False when the pool cannot cover it, and begin() fails closed on that.
static proto_bool ntp_mem_bind(uint8_t *restrict work)
{
    if (span.has_storage(NTP_SERVICE_CTX(work)->req))
    {
        return PROTO_TRUE;
    }
    NTP_SERVICE_CTX(work)->req = protocore_secure_persist_span(PROTOCORE_NTP_PACKET_LEN);
    return span.has_storage(NTP_SERVICE_CTX(work)->req);
}

/**
 * @brief Take one server reply.
 *
 * Refuses anything that is not a mode-4 answer to the question this client asked: the origin field
 * has to carry the cookie the request went out with, which is what stops an off-path packet setting
 * the clock (RFC 4330 sec 5). Stratum 0 is a kiss-o'-death rather than a time.
 */
// The one time source (server/clock/clock.h). Clock.ms is where the last reading landed, so a
// caller that only reads it measures against whichever instant something else stamped. Take the
// reading, then report it.
static uint32_t ntp_now(void)
{
    Clock.millis(Clock.internal);
    return Clock.ms;
}

static void ntp_reply(const uint8_t *data, size_t len, const struct protocore_udp_peer *peer, void *ctx)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_ntp_service_span();

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
    if (endian.rd32be(data + PROTOCORE_NTP_OFF_ORIGIN_SEC) != NTP_SERVICE_CTX(work)->cookie)
    {
        return; // not an answer to what this client asked
    }
    uint32_t secs = endian.rd32be(data + PROTOCORE_NTP_OFF_TX_SEC);
    if (secs <= PROTOCORE_NTP_UNIX_OFFSET)
    {
        return;
    }
    time_t epoch = (time_t)(secs - PROTOCORE_NTP_UNIX_OFFSET);
    if (epoch <= PROTOCORE_NTP_PLAUSIBLE_EPOCH)
    {
        return; // a server that answers with a pre-2021 clock is not one to follow
    }
    NTP_SERVICE_CTX(work)->epoch = epoch;
    NTP_SERVICE_CTX(work)->sync_ms = ntp_now();
}

// The entries this file calls before reaching their definitions.
void protocore_ntp_service_epoch(uint8_t *restrict work);

void protocore_ntp_service_begin(uint8_t *restrict work)
{
    const char *tz = NtpServiceV.begin_args.tz;
    const char *server1 = NtpServiceV.begin_args.server1;
    const char *server2 = NtpServiceV.begin_args.server2;

    (void)tz; // the epoch this client reports is UTC; nothing here formats a local time
    (void)server2;
    const char *host = PROTOCORE_NTP_SERVER1;
    if (server1 != NULL)
    {
        host = server1;
    }
    protocore_ip dst = {PROTOCORE_IP_NONE, {0}};
    if (!ntp_mem_bind(work))
    {
        NtpServiceV.ok = PROTO_FALSE; // no storage
        return;
    }
    IpV.args.text = host;
    IpV.args.out = &dst;
    Ip.parse(work);
    if (!IpV.ok)
    {
        NtpServiceV.ok = PROTO_FALSE; // a name, and this client has no resolver of its own
        return;
    }
    // Bind every time rather than remembering: the listener rebinds a port it already holds, and a
    // port closed underneath this client is exactly the case a remembered flag would send a datagram
    // from a slot that no longer exists.
    UdpListenerV.port = PROTOCORE_NTP_CLIENT_PORT;
    UdpListenerV.bind.handler = ntp_reply;
    UdpListenerV.bind.handler_ctx = NULL;
    UdpListenerV.bind.group_ip = NULL;
    UdpListener.listen(protocore_udp_listener_span());
    if (!UdpListenerV.ok)
    {
        NtpServiceV.ok = PROTO_FALSE;
        return;
    }
    // The transmit stamp doubles as the cookie the reply has to echo. Ticks, not a clock: this runs
    // before there is one.
    NTP_SERVICE_CTX(work)->cookie = ntp_now() | 1u;
    uint8_t *req = NTP_SERVICE_CTX(work)->req.buf;
    for (size_t i = 0; i < PROTOCORE_NTP_PACKET_LEN; i++)
    {
        req[i] = 0;
    }
    req[0] = PROTOCORE_NTP_LI_VN_MODE(PROTOCORE_NTP_LI_NONE, PROTOCORE_NTP_VERSION, PROTOCORE_NTP_MODE_CLIENT);
    endian.wr32be(req + PROTOCORE_NTP_OFF_TX_SEC, NTP_SERVICE_CTX(work)->cookie);
    UdpListenerV.send_args.dst = &dst;
    UdpListenerV.send_args.dst_port = PROTOCORE_NTP_PORT;
    UdpListenerV.send_args.data = req;
    UdpListenerV.send_args.len = PROTOCORE_NTP_PACKET_LEN;
    UdpListener.sendto(protocore_udp_listener_span());
    NtpServiceV.ok = UdpListenerV.ok;
}

void protocore_ntp_service_synced(uint8_t *restrict work)
{
    NtpServiceV.ok = NTP_SERVICE_CTX(work)->epoch != 0;
}

void protocore_ntp_service_epoch(uint8_t *restrict work)
{
    if (NTP_SERVICE_CTX(work)->epoch == 0)
    {
        NtpServiceV.value = 0;
        return;
    }
    // The reply fixed one instant; the monotonic clock carries it forward from there.
    uint32_t elapsed = ntp_now() - NTP_SERVICE_CTX(work)->sync_ms;
    NtpServiceV.value = NTP_SERVICE_CTX(work)->epoch + (time_t)(elapsed / 1000u);
}

void protocore_ntp_service_set_test_epoch(uint8_t *restrict work)
{
    time_t epoch = NtpServiceV.set_test_epoch_args.epoch;

    NTP_SERVICE_CTX(work)->epoch = epoch;
    NTP_SERVICE_CTX(work)->sync_ms = ntp_now();
}

#endif // PROTOCORE_ENABLE_NTP

#if PROTOCORE_ENABLE_NTP
// NTP as a registry time source (protocore_ntp_epoch is 0 until a reply lands). Register it with
// protocore_time_source_add() so the aggregated protocore_time_now() - and the HTTP Date header - can be fed by
// NTP alongside an RTC / GPS.
void protocore_ntp_service_time_source(uint8_t *restrict work)
{
    protocore_ntp_service_epoch(work);
    NtpServiceV.ms = (uint32_t)NtpServiceV.value;
}
/** @brief The operands and the outcome. */
NtpServiceVars NtpServiceV;

PROTOCORE_END_DECLS

#endif
