// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntp_server.c
 * @brief NTP server (RFC 5905 server mode) - implementation. See ntp_server.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_NTP_SERVER

#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "mmgr/protomem/protomem.h"
#include "network_drivers/application/ntp_server/ntp_server.h"

#include "mmgr/endian/endian.h"
PROTOCORE_BEGIN_DECLS

// memset, memcpy

#include "network_drivers/transport/udp/server/server.h"      // UdpListener: the port 123 bind and the reply
#include "server/clock/clock.h"                               // Clock.millis: the sub-second fraction
#include "services/timing_position/time_source/time_source.h" // protocore_time_now: the seconds we serve


static void ntp_server_build_response(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *req = NtpServer.build_response_args.req;
    size_t req_len = NtpServer.build_response_args.req_len;
    uint8_t stratum = NtpServer.build_response_args.stratum;
    uint32_t refid = NtpServer.build_response_args.refid;
    uint32_t protocore_ntp_secs = NtpServer.build_response_args.protocore_ntp_secs;
    uint32_t protocore_ntp_frac = NtpServer.build_response_args.protocore_ntp_frac;
    uint8_t *out = NtpServer.build_response_args.out;
    size_t out_cap = NtpServer.build_response_args.out_cap;

    if (req == NULL || out == NULL || req_len < PROTOCORE_NTP_PACKET_LEN || out_cap < PROTOCORE_NTP_PACKET_LEN)
    {
        NtpServer.n = 0;
        return;
    }

    // LI (2 bits) | VN (3 bits) | Mode (3 bits). Client (3) is answered server (4) and symmetric
    // active (1) symmetric passive (2); every other mode returns no reply.
    uint8_t vn = (uint8_t)((req[0] >> 3) & 0x7);
    uint8_t mode = (uint8_t)(req[0] & 0x7);
    uint8_t reply_mode;
    if (mode == 3u)
    {
        reply_mode = 4u;
    }
    else if (mode == 1u)
    {
        reply_mode = 2u;
    }
    else
    {
        NtpServer.n = 0;
        return;
    }
    mem.set(out, 0, PROTOCORE_NTP_PACKET_LEN);
    out[0] = (uint8_t)((0u << 6) | (vn << 3) | reply_mode); // LI = 0 (in sync), VN echoed
    out[1] = stratum;
    out[2] = req[2];        // poll interval: the request's, intact
    out[3] = (uint8_t)(-6); // precision: ~2^-6 s (16 ms), the clock's granularity
    // Root delay (4..7) stays 0. Root dispersion (8..11) is 0 for a primary server; a higher
    // stratum advertises ~1 s.
    if (stratum != 1u)
    {
        endian.wr32be(out + 8, 0x00010000u);
    }
    endian.wr32be(out + 12, refid);
    endian.wr32be(out + 16, protocore_ntp_secs); // reference timestamp (when our clock was last good = now)
    endian.wr32be(out + 20, protocore_ntp_frac);
    mem.cpy(out + 24, req + 40, 8);              // origin timestamp = the client's transmit timestamp
    endian.wr32be(out + 32, protocore_ntp_secs); // receive timestamp
    endian.wr32be(out + 36, protocore_ntp_frac);
    endian.wr32be(out + 40, protocore_ntp_secs); // transmit timestamp
    endian.wr32be(out + 44, protocore_ntp_frac);
    NtpServer.n = PROTOCORE_NTP_PACKET_LEN;
}

// All NTP-server binding state, owned by one instance (internal linkage): the advertised
// stratum and reference id, grouped so it is one named owner, unreachable cross-TU.
typedef struct
{
    uint8_t stratum;
    uint32_t refid;
} NtpServerCtx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define NTP_SERVER_OFF_CTX 0u
static_assert(NTP_SERVER_OFF_CTX + sizeof(NtpServerCtx) <= PROTOCORE_NTP_SERVER_BORROW,
              "PROTOCORE_NTP_SERVER_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define NTP_SERVER_CTX(w) ((NtpServerCtx *)(void *)((w) + NTP_SERVER_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_NTP_SERVER_BORROW persistent bytes
} NtpServerOwnCtx;
static NtpServerOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_ntp_server_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_NTP_SERVER_BORROW).buf;
        // A borrow arrives zeroed, and these do not start at zero.
        NTP_SERVER_CTX(s_own.span)->refid = PROTOCORE_NTP_REFID_LOCL;
        NTP_SERVER_CTX(s_own.span)->stratum = PROTOCORE_NTP_SERVER_STRATUM;
    }
    return s_own.span;
}

// UDP handler: answer each request from the current time (silent if we have none).
static void protocore_ntp_server_udp_handler(const uint8_t *data, size_t len, const struct protocore_udp_peer *peer,
                                             void *ctx)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_ntp_server_span();

    (void)ctx;
    uint32_t unix_secs = protocore_time_now();
    if (unix_secs == 0) // no valid time - do not serve a wrong clock
    {
        return;
    }
    // Sub-second fraction from the monotonic ms clock (best-effort; not phase-locked to the
    // 1 Hz second boundary, so the sub-second component is approximate on this class of clock).
    uint32_t frac = (uint32_t)(((uint64_t)(Clock.ms % 1000u) << 32) / 1000u);

    uint8_t resp[PROTOCORE_NTP_PACKET_LEN];
    NtpServer.build_response_args.req = data;
    NtpServer.build_response_args.req_len = len;
    NtpServer.build_response_args.stratum = NTP_SERVER_CTX(work)->stratum;
    NtpServer.build_response_args.refid = NTP_SERVER_CTX(work)->refid;
    NtpServer.build_response_args.protocore_ntp_secs = unix_secs + PROTOCORE_NTP_UNIX_OFFSET;
    NtpServer.build_response_args.protocore_ntp_frac = frac;
    NtpServer.build_response_args.out = resp;
    NtpServer.build_response_args.out_cap = sizeof(resp);
    NtpServer.build_response(work);
    const size_t n = NtpServer.n;
    if (n)
    {
        UdpListener.peer_args.peer = peer;
        UdpListener.send_args.data = resp;
        UdpListener.send_args.len = n;
        UdpListener.reply(protocore_udp_listener_span());
    }
}

static void ntp_server_begin(uint8_t *restrict work)
{
    uint8_t stratum = NtpServer.begin_args.stratum;
    uint32_t refid = NtpServer.begin_args.refid;

    NTP_SERVER_CTX(work)->stratum = stratum;
    NTP_SERVER_CTX(work)->refid = refid;
    UdpListener.port = PROTOCORE_NTP_PORT;
    UdpListener.bind.handler = protocore_ntp_server_udp_handler;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.bind.group_ip = NULL;
    UdpListener.listen(protocore_udp_listener_span());
    NtpServer.ok = UdpListener.ok;
}

NtpServerNs NtpServer = {
    .build_response = ntp_server_build_response,
    .begin = ntp_server_begin,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_NTP_SERVER
