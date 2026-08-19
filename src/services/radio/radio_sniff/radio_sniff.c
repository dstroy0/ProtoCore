// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file radio_sniff.c
 * @brief Receive-only radio channel sniffer -> pcap capture records (see radio_sniff.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

static uint8_t pcap_work[16]; // the borrow an entry takes; Pcap never reads it

#if PROTOCORE_ENABLE_RADIO_SNIFF

#include "mmgr/endian/endian.h"
#include "services/radio/radio_sniff/radio_sniff.h"
#include "shared/pcap/pcap.h"

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void radio_sniff_i2f32(uint8_t *restrict work);

static void radio_sniff_i2f32(uint8_t *restrict work)
{
    (void)work;
    int32_t dbm = RadioSniff.i2f32_args.dbm;

    if (dbm == 0)
    {
        RadioSniff.u32 = 0;
        return;
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
    RadioSniff.u32 = sign | (exp << 23) | mant;
}

static void radio_sniff_global_header(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = RadioSniff.global_header_args.out;
    size_t cap = RadioSniff.global_header_args.cap;

    Pcap.args.out = out;
    Pcap.args.cap = cap;
    Pcap.args.linktype = PROTOCORE_DLT_IEEE802_15_4_TAP;
    Pcap.global_header(pcap_work);
    RadioSniff.n = Pcap.n;
}

static void radio_sniff_tap_record(uint8_t *restrict work)
{
    uint8_t *out = RadioSniff.tap_record_args.out;
    size_t cap = RadioSniff.tap_record_args.cap;
    const uint8_t *frame = RadioSniff.tap_record_args.frame;
    size_t flen = RadioSniff.tap_record_args.flen;
    int32_t rssi_dbm = RadioSniff.tap_record_args.rssi_dbm;
    uint16_t channel = RadioSniff.tap_record_args.channel;
    uint32_t ts_sec = RadioSniff.tap_record_args.ts_sec;
    uint32_t ts_usec = RadioSniff.tap_record_args.ts_usec;

    if (!out || !frame || flen == 0)
    {
        RadioSniff.n = 0;
        return;
    }
    size_t caplen = RADIO_SNIFF_TAP_LEN + flen;
    size_t total = PROTOCORE_PCAP_REC_HDR_LEN + caplen;
    if (cap < total)
    {
        RadioSniff.n = 0;
        return;
    }

    // pcap record header.
    Pcap.args.out = out;
    Pcap.args.cap = cap;
    Pcap.rec.ts_sec = ts_sec;
    Pcap.rec.ts_usec = ts_usec;
    Pcap.rec.caplen = (uint32_t)caplen;
    Pcap.rec.origlen = (uint32_t)caplen;
    Pcap.record_header(pcap_work);
    uint8_t *p = out + PROTOCORE_PCAP_REC_HDR_LEN;

    // 802.15.4 TAP header: version(1)=0, reserved(1)=0, length(2 LE) = whole TAP block.
    p[0] = 0;
    p[1] = 0;
    endian.wr16le(p + 2, RADIO_SNIFF_TAP_LEN);
    // TLV: Received Signal Strength (type 1, len 4), float32 dBm.
    endian.wr16le(p + 4, 1);
    endian.wr16le(p + 6, 4);
    RadioSniff.i2f32_args.dbm = rssi_dbm;
    radio_sniff_i2f32(work);
    endian.wr32le(p + 8, RadioSniff.u32);
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
    RadioSniff.n = total;
}

RadioSniffNs RadioSniff = {
    .global_header = radio_sniff_global_header, .i2f32 = radio_sniff_i2f32, .tap_record = radio_sniff_tap_record};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_RADIO_SNIFF
