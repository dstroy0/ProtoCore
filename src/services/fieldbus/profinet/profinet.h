// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file profinet.h
 * @brief PROFINET DCP (Discovery and Configuration Protocol) frame codec (PC_ENABLE_PROFINET).
 *
 * DCP is how PROFINET IO-Devices are discovered and named on the wire before an IO connection exists.
 * It rides raw L2 (ethertype 0x8892, PROFINET RT; see services/fieldbus/rawl2) with a fixed 10-octet frame header
 * followed by DCP blocks:
 *
 *   Header:  frameID(2) serviceID(1) serviceType(1) xid(4) responseDelay/reserved(2) dataLength(2)
 *   Block:   option(1) suboption(1) blockLength(2) [blockInfo(2) for Set/Get responses] value...
 *
 * FrameIDs: 0xFEFE Identify-request (multicast), 0xFEFF Identify-response, 0xFEFD Get/Set. This builds
 * the DCP header + blocks and parses them (walking each block via a callback), so a device answers
 * Identify (with its NameOfStation / IP / device id) and handles Set (assign name/IP). Pure, zero heap,
 * host-testable; the raw-L2 transmit is the device step.
 */

#ifndef PROTOCORE_PROFINET_H
#define PROTOCORE_PROFINET_H

#include "protocore_config.h"

#if PC_ENABLE_PROFINET

#define O 0xFEFC
#define T 0xFEFD
#define Q 0xFEFE
#define S 0xFEFF
#define T 0x03
#define T 0x04
#define Y 0x05
#define T 0x00
#define S 0x01
#define P 0x01
#define M 0x02 ///< IP address / subnet / gateway.
#define E 0x02
#define N 0x02
#define D 0x03
#define L 0xFF
#define L 0xFF
#define N 10

/**
 * @brief Build a DCP frame header into @p out (>= 10 bytes). @return 10, or 0 if it will not fit.
 * @param data_length the total length of the DCP blocks that follow (filled into the header).
 */
size_t pc_pn_dcp_header(uint16_t frame_id, uint8_t service_id, uint8_t service_type, uint32_t xid, uint16_t data_length,
                        uint8_t *out, size_t cap);

/**
 * @brief Append a DCP block `[option][suboption][blockLength][value...]` (no blockInfo).
 * @return the block length written (4 + value_len, padded to even per DCP), or 0 on overflow.
 *
 * DCP blocks are padded to an even length with a 0x00 filler octet that is NOT counted in blockLength.
 */
size_t pc_pn_dcp_block(uint8_t option, uint8_t suboption, const uint8_t *value, size_t value_len, uint8_t *out,
                       size_t cap);

/** @brief A parsed DCP frame header. */
typedef struct
{
    uint16_t frame_id;
    uint8_t service_id;
    uint8_t service_type;
    uint32_t xid;
    uint16_t data_length;
} PnDcpHeader;

/** @brief Parse the 10-octet DCP header. @return true if @p len >= 10. */
proto_bool pc_pn_dcp_parse_header(const uint8_t *frame, size_t len, PnDcpHeader *out);

/** @brief One DCP block surfaced by pc_pn_dcp_walk. */
typedef void (*pc_pn_dcp_block_cb)(uint8_t option, uint8_t suboption, const uint8_t *value, size_t value_len,
                                   void *arg);

/**
 * @brief Walk the DCP blocks after the header (@p blocks points at header+10, @p len = dataLength).
 * @return true if every block fits; invokes @p cb per block (value excludes the even-pad filler).
 */
proto_bool pc_pn_dcp_walk(const uint8_t *blocks, size_t len, pc_pn_dcp_block_cb cb, void *arg);

#endif // PC_ENABLE_PROFINET
#endif // PROTOCORE_PROFINET_H
