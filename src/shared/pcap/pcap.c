// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pcap.c
 * @brief The two libpcap headers a capture file is built from. See pcap.h.
 *
 * Pure: both headers are written into the caller's buffer and nothing is held between calls, so
 * there is no storage member.
 */

#include "shared/pcap/pcap.h"

/**
 * @brief The writers' calls - what PcapNs points at.
 *
 * @var PcapInternal::ns  the handle a caller sets a call's members on
 */
struct PcapInternal
{
    PcapNs *ns;
};

static struct PcapInternal s_pcap = {.ns = &Pcap};

static void pcap_global(struct PcapInternal *restrict ctx)
{
    uint8_t *out = ctx->ns->args.out;

    ctx->ns->n = 0;
    if (!out || ctx->ns->args.cap < PROTOCORE_PCAP_GLOBAL_HDR_LEN)
    {
        return;
    }
    endian.wr32le(out + 0, 0xa1b2c3d4);              // magic: usec timestamps, little-endian
    endian.wr16le(out + 4, 2);                       // version major
    endian.wr16le(out + 6, 4);                       // version minor
    endian.wr32le(out + 8, 0);                       // thiszone (GMT)
    endian.wr32le(out + 12, 0);                      // sigfigs
    endian.wr32le(out + 16, 65535);                  // snaplen
    endian.wr32le(out + 20, ctx->ns->args.linktype); // network / DLT
    ctx->ns->n = PROTOCORE_PCAP_GLOBAL_HDR_LEN;
}

static void pcap_record(struct PcapInternal *restrict ctx)
{
    uint8_t *out = ctx->ns->args.out;

    ctx->ns->n = 0;
    if (!out || ctx->ns->args.cap < PROTOCORE_PCAP_REC_HDR_LEN)
    {
        return;
    }
    endian.wr32le(out + 0, ctx->ns->rec.ts_sec);
    endian.wr32le(out + 4, ctx->ns->rec.ts_usec);
    endian.wr32le(out + 8, ctx->ns->rec.caplen);
    endian.wr32le(out + 12, ctx->ns->rec.origlen);
    ctx->ns->n = PROTOCORE_PCAP_REC_HDR_LEN;
}

PcapNs Pcap = {.global_header = pcap_global, .record_header = pcap_record, .internal = &s_pcap};
