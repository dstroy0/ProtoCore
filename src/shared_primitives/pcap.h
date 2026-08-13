// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pcap.h
 * @brief libpcap file framing - the classic global + per-record headers, link-type agnostic.
 *
 * One owner for the PCAP framing shared by every capture feature (Wi-Fi promiscuous capture,
 * CAN / bus listen-only capture, ...): each writes its frames with the matching DLT link type so
 * the forwarded stream is a valid `.pcap` a wired Wireshark / tcpdump opens directly. Header-only
 * and pure (little-endian byte writes, no heap, no stdlib), host-identical.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PCAP_H
#define PROTOCORE_PCAP_H

#include "mmgr/endian.h"      // protocore_wr32le / protocore_wr16le - libpcap headers are little-endian
#include "protocore_config.h" // the entry point: protocore_types.h for the widths and PROTOCORE_INLINE

/** @brief libpcap header sizes. */
#define PROTOCORE_PCAP_GLOBAL_HDR_LEN 24
#define PROTOCORE_PCAP_REC_HDR_LEN 16

/** @brief Common libpcap DLT link-layer types. */
#define PROTOCORE_DLT_IEEE802_11 105         ///< raw 802.11 (Wi-Fi promiscuous capture)
#define PROTOCORE_DLT_CAN_SOCKETCAN 227      ///< Linux SocketCAN classic/FD frames
#define PROTOCORE_DLT_ETHERNET 1             ///< IEEE 802.3 Ethernet
#define PROTOCORE_DLT_IEEE802_15_4_NOFCS 230 ///< raw 802.15.4 MAC frame, no FCS
#define PROTOCORE_DLT_IEEE802_15_4_TAP 283   ///< 802.15.4 with a TAP pseudo-header (RSSI / channel TLVs)
#define PROTOCORE_DLT_RAW 101                ///< the record starts at the IP header, with no link layer

/**
 * @brief Write the 24-byte libpcap global header (little-endian, microsecond timestamps).
 * @param linktype the DLT_* link type of the frames that follow (e.g. ::PROTOCORE_DLT_IEEE802_11).
 * @return ::PROTOCORE_PCAP_GLOBAL_HDR_LEN, or 0 if @p cap is too small.
 */
PROTOCORE_INLINE size_t protocore_pcap_global_header(uint8_t *out, size_t cap, uint32_t linktype)
{
    if (!out || cap < PROTOCORE_PCAP_GLOBAL_HDR_LEN)
    {
        return 0;
    }
    protocore_wr32le(out + 0, 0xa1b2c3d4); // magic: usec timestamps, little-endian
    protocore_wr16le(out + 4, 2);          // version major
    protocore_wr16le(out + 6, 4);          // version minor
    protocore_wr32le(out + 8, 0);          // thiszone (GMT)
    protocore_wr32le(out + 12, 0);         // sigfigs
    protocore_wr32le(out + 16, 65535);     // snaplen
    protocore_wr32le(out + 20, linktype);  // network / DLT
    return PROTOCORE_PCAP_GLOBAL_HDR_LEN;
}

/**
 * @brief Write a 16-byte libpcap record header for one captured frame.
 * @return ::PROTOCORE_PCAP_REC_HDR_LEN, or 0 if @p cap is too small.
 */
PROTOCORE_INLINE size_t protocore_pcap_record_header(uint8_t *out, size_t cap, uint32_t ts_sec, uint32_t ts_usec,
                                                     uint32_t caplen, uint32_t origlen)
{
    if (!out || cap < PROTOCORE_PCAP_REC_HDR_LEN)
    {
        return 0;
    }
    protocore_wr32le(out + 0, ts_sec);
    protocore_wr32le(out + 4, ts_usec);
    protocore_wr32le(out + 8, caplen);
    protocore_wr32le(out + 12, origlen);
    return PROTOCORE_PCAP_REC_HDR_LEN;
}

#endif // PROTOCORE_PCAP_H
