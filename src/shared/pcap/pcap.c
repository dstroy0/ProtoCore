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

static void pcap_global(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = Pcap.args.out;

    Pcap.n = 0;
    if (!out || Pcap.args.cap < PROTOCORE_PCAP_GLOBAL_HDR_LEN)
    {
        return;
    }
    endian.wr32le(out + 0, 0xa1b2c3d4);          // magic: usec timestamps, little-endian
    endian.wr16le(out + 4, 2);                   // version major
    endian.wr16le(out + 6, 4);                   // version minor
    endian.wr32le(out + 8, 0);                   // thiszone (GMT)
    endian.wr32le(out + 12, 0);                  // sigfigs
    endian.wr32le(out + 16, 65535);              // snaplen
    endian.wr32le(out + 20, Pcap.args.linktype); // network / DLT
    Pcap.n = PROTOCORE_PCAP_GLOBAL_HDR_LEN;
}

static void pcap_record(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = Pcap.args.out;

    Pcap.n = 0;
    if (!out || Pcap.args.cap < PROTOCORE_PCAP_REC_HDR_LEN)
    {
        return;
    }
    endian.wr32le(out + 0, Pcap.rec.ts_sec);
    endian.wr32le(out + 4, Pcap.rec.ts_usec);
    endian.wr32le(out + 8, Pcap.rec.caplen);
    endian.wr32le(out + 12, Pcap.rec.origlen);
    Pcap.n = PROTOCORE_PCAP_REC_HDR_LEN;
}

PcapNs Pcap = {.global_header = pcap_global, .record_header = pcap_record};
