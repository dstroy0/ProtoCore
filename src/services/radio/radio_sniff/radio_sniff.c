// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file radio_sniff.c
 * @brief Receive-only radio channel sniffer -> pcap capture records (see radio_sniff.h).
 */

#include "services/radio/radio_sniff/radio_sniff.h"
#include "mmgr/endian.h"

#if PROTOCORE_ENABLE_RADIO_SNIFF

uint32_t protocore_radiosniff_i2f32(int32_t dbm)
{
    if (dbm == 0)
    {
        return 0;
    }
    uint32_t sign = 0;
    uint32_t mag;
    if (dbm < 0)
    {
        sign = 0x80000000u;
        mag = (uint32_t)(-(int64_t)dbm);
    }
    else
    {
        mag = (uint32_t)dbm;
    }
    int e = 31;
    while (!((mag >> e) & 1u))
    {
        e--; // highest set bit
    }
    uint32_t exp = (uint32_t)(127 + e);
    uint32_t mant = (e >= 23) ? ((mag >> (e - 23)) & 0x7FFFFFu) : ((mag << (23 - e)) & 0x7FFFFFu);
    return sign | (exp << 23) | mant;
}

size_t protocore_radiosniff_global(uint8_t *out, size_t cap)
{
    Pcap.args.out = out;
    Pcap.args.cap = cap;
    Pcap.args.linktype = PROTOCORE_DLT_IEEE802_15_4_TAP;
    Pcap.global_header(Pcap.internal);
    return Pcap.n;
}

size_t protocore_radiosniff_tap_record(uint8_t *out, size_t cap, const uint8_t *frame, size_t flen, int32_t rssi_dbm,
                                       uint16_t channel, uint32_t ts_sec, uint32_t ts_usec)
{
    if (!out || !frame || flen == 0)
    {
        return 0;
    }
    size_t caplen = RADIO_SNIFF_TAP_LEN + flen;
    size_t total = PROTOCORE_PCAP_REC_HDR_LEN + caplen;
    if (cap < total)
    {
        return 0;
    }

    // pcap record header.
    Pcap.args.out = out;
    Pcap.args.cap = cap;
    Pcap.rec.ts_sec = ts_sec;
    Pcap.rec.ts_usec = ts_usec;
    Pcap.rec.caplen = (uint32_t)caplen;
    Pcap.rec.origlen = (uint32_t)caplen;
    Pcap.record_header(Pcap.internal);
    uint8_t *p = out + PROTOCORE_PCAP_REC_HDR_LEN;

    // 802.15.4 TAP header: version(1)=0, reserved(1)=0, length(2 LE) = whole TAP block.
    p[0] = 0;
    p[1] = 0;
    endian.wr16le(p + 2, RADIO_SNIFF_TAP_LEN);
    // TLV: Received Signal Strength (type 1, len 4), float32 dBm.
    endian.wr16le(p + 4, 1);
    endian.wr16le(p + 6, 4);
    endian.wr32le(p + 8, protocore_radiosniff_i2f32(rssi_dbm));
    // TLV: Channel Assignment (type 3, len 3 -> padded to 4): channel number(2 LE) + page(1).
    endian.wr16le(p + 12, 3);
    endian.wr16le(p + 14, 3);
    endian.wr16le(p + 16, channel);
    p[18] = 0; // channel page 0
    p[19] = 0; // pad

    // The raw MAC frame.
    uint8_t *f = p + RADIO_SNIFF_TAP_LEN;
    for (size_t i = 0; i < flen; i++)
    {
        f[i] = frame[i];
    }
    return total;
}

#endif // PROTOCORE_ENABLE_RADIO_SNIFF
