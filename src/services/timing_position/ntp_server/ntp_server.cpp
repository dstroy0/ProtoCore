// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_ntp_server.cpp
 * @brief NTP server (RFC 5905 server mode) - implementation. See pc_ntp_server.h.
 */

#include "services/timing_position/ntp_server/ntp_server.h"
#include "protocore_config.h"

#if PC_ENABLE_NTP_SERVER

#include "shared_primitives/endian.h"
#include <string.h> // memset, memcpy

#if PROTOCORE_HOT
#include "network_drivers/transport/udp.h"
#include "services/system/clock.h"
#include "services/timing_position/time_source/time_source.h"
#endif
size_t pc_ntp_server_build_response(const uint8_t *req, size_t req_len, uint8_t stratum, uint32_t refid,
                                    uint32_t pc_ntp_secs, uint32_t pc_ntp_frac, uint8_t *out, size_t out_cap)
{
    if (!req || !out || req_len < NTP_PACKET_LEN || out_cap < NTP_PACKET_LEN)
    {
        return 0;
    }

    // LI (2 bits) | VN (3 bits) | Mode (3 bits). Echo the client's version; reply as server (4).
    uint8_t vn = (uint8_t)((req[0] >> 3) & 0x7);
    memset(out, 0, NTP_PACKET_LEN);
    out[0] = (uint8_t)((0u << 6) | (vn << 3) | 4u); // LI = 0 (in sync), VN echoed, Mode = 4 (server)
    out[1] = stratum;
    out[2] = req[2] ? req[2] : 6; // poll interval: echo the client's, else 2^6 s
    out[3] = (uint8_t)(-6);       // precision: ~2^-6 s (16 ms), the clock's granularity
    // Root delay (4..7) stays 0; root dispersion (8..11) ~ 1 s to advertise a coarse clock.
    pc_wr32be(out + 8, 0x00010000u);
    pc_wr32be(out + 12, refid);
    pc_wr32be(out + 16, pc_ntp_secs); // reference timestamp (when our clock was last good = now)
    pc_wr32be(out + 20, pc_ntp_frac);
    memcpy(out + 24, req + 40, 8);    // origin timestamp = the client's transmit timestamp
    pc_wr32be(out + 32, pc_ntp_secs); // receive timestamp
    pc_wr32be(out + 36, pc_ntp_frac);
    pc_wr32be(out + 40, pc_ntp_secs); // transmit timestamp
    pc_wr32be(out + 44, pc_ntp_frac);
    return NTP_PACKET_LEN;
}

#if PROTOCORE_HOT

// All NTP-server binding state, owned by one instance (internal linkage): the advertised
// stratum and reference id, grouped so it is one named owner, unreachable cross-TU.
typedef struct
{
    uint8_t stratum = PC_NTP_SERVER_STRATUM;
    uint32_t refid = NTP_REFID_LOCL;
} NtpServerCtx;
static NtpServerCtx s_ntp;

// UDP handler: answer each request from the current time (silent if we have none).
static void pc_ntp_server_udp_handler(const uint8_t *data, size_t len, const struct pc_udp_peer *peer, void *ctx)
{
    (void)ctx;
    uint32_t unix_secs = pc_time_now();
    if (unix_secs == 0) // no valid time - do not serve a wrong clock
    {
        return;
    }
    // Sub-second fraction from the monotonic ms clock (best-effort; not phase-locked to the
    // 1 Hz second boundary, so the sub-second component is approximate on this class of clock).
    uint32_t frac = (uint32_t)(((uint64_t)(pc_millis() % 1000u) << 32) / 1000u);

    uint8_t resp[NTP_PACKET_LEN];
    size_t n = pc_ntp_server_build_response(data, len, s_ntp.stratum, s_ntp.refid, unix_secs + NTP_UNIX_OFFSET, frac,
                                            resp, sizeof(resp));
    if (n)
    {
        pc_udp_send(peer, resp, n);
    }
}

proto_bool pc_ntp_server_begin(uint8_t stratum, uint32_t refid)
{
    s_ntp.stratum = stratum;
    s_ntp.refid = refid;
    return pc_udp_listen(123, pc_ntp_server_udp_handler, NULL);
}

#else // host build: no lwIP. The codec above is host-tested; the binding is a stub.

proto_bool pc_ntp_server_begin(uint8_t, uint32_t)
{
    return PROTO_FALSE;
}

#endif // PROTOCORE_HOT

#endif // PC_ENABLE_NTP_SERVER
