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

void protocore_pcap_global_header(uint8_t *work)
{
    (void)work;
    uint8_t *out = PcapV.args.out;

    PcapV.n = 0;
    if (!out || PcapV.args.cap < PROTOCORE_PCAP_GLOBAL_HDR_LEN)
    {
        return;
    }
    // magic: usec timestamps, little-endian
    EMBED_CALL(parva_extremitas.wr, EndianCfg, .dst = out + 0, .val = 0xa1b2c3d4, .width = MMGR_ENDIAN_32);
    EMBED_CALL(parva_extremitas.wr, EndianCfg, .dst = out + 4, .val = 2, .width = MMGR_ENDIAN_16);  // version major
    EMBED_CALL(parva_extremitas.wr, EndianCfg, .dst = out + 6, .val = 4, .width = MMGR_ENDIAN_16);  // version minor
    EMBED_CALL(parva_extremitas.wr, EndianCfg, .dst = out + 8, .val = 0, .width = MMGR_ENDIAN_32);  // thiszone (GMT)
    EMBED_CALL(parva_extremitas.wr, EndianCfg, .dst = out + 12, .val = 0, .width = MMGR_ENDIAN_32); // sigfigs
    EMBED_CALL(parva_extremitas.wr, EndianCfg, .dst = out + 16, .val = 65535, .width = MMGR_ENDIAN_32); // snaplen
    // network / DLT
    EMBED_CALL(parva_extremitas.wr, EndianCfg, .dst = out + 20, .val = PcapV.args.linktype, .width = MMGR_ENDIAN_32);
    PcapV.n = PROTOCORE_PCAP_GLOBAL_HDR_LEN;
}

void protocore_pcap_record_header(uint8_t *work)
{
    (void)work;
    uint8_t *out = PcapV.args.out;

    PcapV.n = 0;
    if (!out || PcapV.args.cap < PROTOCORE_PCAP_REC_HDR_LEN)
    {
        return;
    }
    EMBED_CALL(parva_extremitas.wr, EndianCfg, .dst = out + 0, .val = PcapV.rec.ts_sec, .width = MMGR_ENDIAN_32);
    EMBED_CALL(parva_extremitas.wr, EndianCfg, .dst = out + 4, .val = PcapV.rec.ts_usec, .width = MMGR_ENDIAN_32);
    EMBED_CALL(parva_extremitas.wr, EndianCfg, .dst = out + 8, .val = PcapV.rec.caplen, .width = MMGR_ENDIAN_32);
    EMBED_CALL(parva_extremitas.wr, EndianCfg, .dst = out + 12, .val = PcapV.rec.origlen, .width = MMGR_ENDIAN_32);
    PcapV.n = PROTOCORE_PCAP_REC_HDR_LEN;
}

/** @brief The operands and the outcome. */
PcapVars PcapV;
