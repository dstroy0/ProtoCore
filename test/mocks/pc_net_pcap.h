// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Render the mock's UDP send log as a libpcap capture.
//
// pc_net_host.h records what the core handed to the stack: destination, ports, TOS, payload. That
// is a datagram without its headers, so this file synthesizes the IPv4 or IPv6 header and the UDP
// header around each one and frames the result with shared_primitives/pcap.h at DLT_RAW, whose
// records start at the IP header. What comes out is a .pcap Wireshark opens and a test parses.
//
// Separate from pc_net_host.h on purpose: that header is parsed from inside protocore_config.h,
// before shared_primitives/types.h supplies PC_INLINE, so it cannot include pcap.h. A test includes
// this one afterwards, when it can.
//
// The source address is 0.0.0.0 / :: because the mock has no interface address to claim.
//
// test/ is exempt from the src/ style rules, so this reads as plain host C.

#ifndef PROTOCORE_PC_NET_PCAP_H
#define PROTOCORE_PC_NET_PCAP_H

#include "protocore_config.h"       // must be complete before pcap.h is reached
#include "shared_primitives/pcap.h" // PC_DLT_RAW, the global and record headers
#include <stdint.h>
#include <string.h>

#include "pc_net_host.h" // the log this renders

#define PC_NET_PCAP_IP4_HDR 20
#define PC_NET_PCAP_IP6_HDR 40
#define PC_NET_PCAP_UDP_HDR 8
#define PC_NET_PCAP_TTL 64
#define PC_NET_PCAP_PROTO_UDP 17

/** @brief The largest packet a logged datagram can render to. */
#define PC_NET_PCAP_MAX_PKT (PC_NET_PCAP_IP6_HDR + PC_NET_PCAP_UDP_HDR + PC_NET_HOST_DGRAM_LEN)

// Sum @p n bytes into a running one's-complement accumulator, big-endian pairs, odd tail padded.
static inline uint32_t pc_net_pcap_sum(uint32_t acc, const uint8_t *p, size_t n)
{
    size_t i = 0;
    while (i + 1 < n)
    {
        acc += ((uint32_t)p[i] << 8) | (uint32_t)p[i + 1];
        i += 2;
    }
    if (i < n)
    {
        acc += (uint32_t)p[i] << 8;
    }
    return acc;
}

// Fold the carries down and complement: the checksum as it goes on the wire.
static inline uint16_t pc_net_pcap_fold(uint32_t acc)
{
    while (acc >> 16)
    {
        acc = (acc & 0xFFFFu) + (acc >> 16);
    }
    return (uint16_t)(~acc & 0xFFFFu);
}

static inline void pc_net_pcap_put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

/**
 * @brief Build the IP + UDP packet for one logged datagram.
 *
 * The UDP checksum covers the pseudo-header, the UDP header and the payload; a computed zero goes
 * on the wire as 0xFFFF, which is what RFC 768 reserves it to mean.
 * @return the packet length, or 0 if @p d is null, its family is unset, or @p cap is too small.
 */
static inline size_t pc_net_pcap_packet(const pc_net_host_dgram *d, uint8_t *out, size_t cap)
{
    if (!d || !out)
    {
        return 0;
    }
    int v6 = (d->type == PC_NET_TYPE_V6);
    if (!v6 && d->type != PC_NET_TYPE_V4)
    {
        return 0;
    }
    size_t ip_len = v6 ? PC_NET_PCAP_IP6_HDR : PC_NET_PCAP_IP4_HDR;
    size_t udp_len = PC_NET_PCAP_UDP_HDR + d->len;
    size_t total = ip_len + udp_len;
    if (cap < total)
    {
        return 0;
    }
    memset(out, 0, total);

    const uint8_t *src = out + (v6 ? 8 : 12); // the zeroed source, for the pseudo-header
    if (v6)
    {
        out[0] = 0x60u | (uint8_t)(d->tos >> 4);
        out[1] = (uint8_t)((d->tos & 0x0Fu) << 4);
        pc_net_pcap_put16(out + 4, (uint16_t)udp_len);
        out[6] = PC_NET_PCAP_PROTO_UDP;
        out[7] = PC_NET_PCAP_TTL;
        memcpy(out + 24, d->addr, 16);
    }
    else
    {
        out[0] = 0x45u; // version 4, 5 words of header
        out[1] = d->tos;
        pc_net_pcap_put16(out + 2, (uint16_t)total);
        out[8] = PC_NET_PCAP_TTL;
        out[9] = PC_NET_PCAP_PROTO_UDP;
        memcpy(out + 16, d->addr, 4);
        pc_net_pcap_put16(out + 10, pc_net_pcap_fold(pc_net_pcap_sum(0, out, PC_NET_PCAP_IP4_HDR)));
    }

    uint8_t *udp = out + ip_len;
    pc_net_pcap_put16(udp + 0, d->src_port);
    pc_net_pcap_put16(udp + 2, d->dst_port);
    pc_net_pcap_put16(udp + 4, (uint16_t)udp_len);
    memcpy(udp + PC_NET_PCAP_UDP_HDR, d->data, d->len);

    size_t addr_len = v6 ? 16u : 4u;
    uint32_t acc = pc_net_pcap_sum(0, src, addr_len);
    acc = pc_net_pcap_sum(acc, v6 ? out + 24 : out + 16, addr_len);
    acc += (uint32_t)udp_len + (uint32_t)PC_NET_PCAP_PROTO_UDP;
    acc = pc_net_pcap_sum(acc, udp, udp_len);
    uint16_t ck = pc_net_pcap_fold(acc);
    if (ck == 0)
    {
        ck = 0xFFFFu;
    }
    pc_net_pcap_put16(udp + 6, ck);
    return total;
}

/**
 * @brief Render the whole log as a .pcap: the global header, then one record per datagram.
 * @return bytes written, or 0 if @p cap cannot hold the capture.
 */
static inline size_t pc_net_pcap_render(uint8_t *out, size_t cap)
{
    if (!out || pc_pcap_global_header(out, cap, PC_DLT_RAW) == 0)
    {
        return 0;
    }
    size_t w = PC_PCAP_GLOBAL_HDR_LEN;
    for (size_t i = 0; i < pc_net_host_udp_count(); i++)
    {
        const pc_net_host_dgram *d = pc_net_host_udp_at(i);
        if (cap < w + PC_PCAP_REC_HDR_LEN)
        {
            return 0;
        }
        size_t n = pc_net_pcap_packet(d, out + w + PC_PCAP_REC_HDR_LEN, cap - w - PC_PCAP_REC_HDR_LEN);
        if (n == 0)
        {
            return 0;
        }
        pc_pcap_record_header(out + w, cap - w, d->ms / 1000u, (d->ms % 1000u) * 1000u, (uint32_t)n, (uint32_t)n);
        w += PC_PCAP_REC_HDR_LEN + n;
    }
    return w;
}

#endif // PROTOCORE_PC_NET_PCAP_H
