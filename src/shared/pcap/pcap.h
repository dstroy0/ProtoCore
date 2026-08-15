// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "mmgr/endian.h"      // endian.wr32le / endian.wr16le - libpcap headers are little-endian
#include "protocore_config.h" // the entry point: protocore_types.h for the widths

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

/** @brief Where a header is written, and the link type the file declares. */
typedef struct
{
    uint8_t *out;      ///< where the header lands
    size_t cap;        ///< how much room it has
    uint32_t linktype; ///< the DLT_* link type of the frames that follow
} PcapArgs;

/** @brief What one captured frame's record header states. */
typedef struct
{
    uint32_t ts_sec;  ///< capture time, whole seconds
    uint32_t ts_usec; ///< and microseconds within that second
    uint32_t caplen;  ///< octets actually stored
    uint32_t origlen; ///< octets the frame had on the wire
} PcapRecArgs;

/** @brief The writers' own calls, described only in pcap.c. */
struct PcapInternal;

/**
 * @brief The two libpcap headers a capture file is built from.
 *
 * @var PcapNs::args           where a header is written, and the link type the file declares
 * @var PcapNs::rec            what one captured frame's record header states
 * @var PcapNs::n              octets written, or 0 when the buffer is too small
 * @var PcapNs::global_header  the 24-byte file header (little-endian, microsecond timestamps)
 * @var PcapNs::record_header  the 16-byte per-frame header
 * @var PcapNs::internal       the calls that write them
 *
 * No storage member: both headers are written into the caller's buffer.
 */
typedef struct
{
    PcapArgs args;
    PcapRecArgs rec;

    size_t n;

    void (*global_header)(struct PcapInternal *ctx);
    void (*record_header)(struct PcapInternal *ctx);

    struct PcapInternal *internal;
} PcapNs;

/** @brief The one symbol this module exports. */
extern PcapNs Pcap;

#endif // PROTOCORE_PCAP_H
