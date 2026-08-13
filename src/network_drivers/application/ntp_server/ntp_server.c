// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntp_server.c
 * @brief NTP server (RFC 5905 server mode) - implementation. See ntp_server.h.
 */

#include "network_drivers/application/ntp_server/ntp_server.h"
#include "mmgr/protomem.h"
#include "protocore_config.h"

#if PROTOCORE_ENABLE_NTP_SERVER

#include "mmgr/endian.h"
// memset, memcpy

#include "network_drivers/transport/udp.h"                    // Udp.listener: the port 123 bind and the reply
#include "server/clock/clock.h"                               // protocore_millis: the sub-second fraction
#include "services/timing_position/time_source/time_source.h" // protocore_time_now: the seconds we serve

size_t protocore_ntp_server_build_response(const uint8_t *req, size_t req_len, uint8_t stratum, uint32_t refid,
                                           uint32_t protocore_ntp_secs, uint32_t protocore_ntp_frac, uint8_t *out,
                                           size_t out_cap)
{
    if (req == NULL || out == NULL || req_len < PROTOCORE_NTP_PACKET_LEN || out_cap < PROTOCORE_NTP_PACKET_LEN)
    {
        return 0;
    }

    // LI (2 bits) | VN (3 bits) | Mode (3 bits). Echo the client's version; reply as server (4).
    uint8_t vn = (uint8_t)((req[0] >> 3) & 0x7);
    mem.set(out, 0, PROTOCORE_NTP_PACKET_LEN);
    out[0] = (uint8_t)((0u << 6) | (vn << 3) | 4u); // LI = 0 (in sync), VN echoed, Mode = 4 (server)
    out[1] = stratum;
    out[2] = req[2] ? req[2] : 6; // poll interval: echo the client's, else 2^6 s
    out[3] = (uint8_t)(-6);       // precision: ~2^-6 s (16 ms), the clock's granularity
    // Root delay (4..7) stays 0; root dispersion (8..11) ~ 1 s to advertise a coarse clock.
    protocore_wr32be(out + 8, 0x00010000u);
    protocore_wr32be(out + 12, refid);
    protocore_wr32be(out + 16, protocore_ntp_secs); // reference timestamp (when our clock was last good = now)
    protocore_wr32be(out + 20, protocore_ntp_frac);
    mem.cpy(out + 24, req + 40, 8);                 // origin timestamp = the client's transmit timestamp
    protocore_wr32be(out + 32, protocore_ntp_secs); // receive timestamp
    protocore_wr32be(out + 36, protocore_ntp_frac);
    protocore_wr32be(out + 40, protocore_ntp_secs); // transmit timestamp
    protocore_wr32be(out + 44, protocore_ntp_frac);
    return PROTOCORE_NTP_PACKET_LEN;
}

// All NTP-server binding state, owned by one instance (internal linkage): the advertised
// stratum and reference id, grouped so it is one named owner, unreachable cross-TU.
typedef struct
{
    uint8_t stratum;
    uint32_t refid;
} NtpServerCtx;
static NtpServerCtx s_ntp = {PROTOCORE_NTP_SERVER_STRATUM, PROTOCORE_NTP_REFID_LOCL};

// UDP handler: answer each request from the current time (silent if we have none).
static void protocore_ntp_server_udp_handler(const uint8_t *data, size_t len, const struct protocore_udp_peer *peer,
                                             void *ctx)
{
    (void)ctx;
    uint32_t unix_secs = protocore_time_now();
    if (unix_secs == 0) // no valid time - do not serve a wrong clock
    {
        return;
    }
    // Sub-second fraction from the monotonic ms clock (best-effort; not phase-locked to the
    // 1 Hz second boundary, so the sub-second component is approximate on this class of clock).
    uint32_t frac = (uint32_t)(((uint64_t)(protocore_millis() % 1000u) << 32) / 1000u);

    uint8_t resp[PROTOCORE_NTP_PACKET_LEN];
    size_t n = protocore_ntp_server_build_response(data, len, s_ntp.stratum, s_ntp.refid,
                                                   unix_secs + PROTOCORE_NTP_UNIX_OFFSET, frac, resp, sizeof(resp));
    if (n)
    {
        Udp.listener->reply(peer, resp, n);
    }
}

proto_bool protocore_ntp_server_begin(uint8_t stratum, uint32_t refid)
{
    s_ntp.stratum = stratum;
    s_ntp.refid = refid;
    return Udp.listener->listen(123, protocore_ntp_server_udp_handler, NULL);
}

#endif // PROTOCORE_ENABLE_NTP_SERVER
