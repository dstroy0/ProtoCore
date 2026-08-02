// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file goose.h
 * @brief IEC 61850 GOOSE publisher + subscriber codec (PC_ENABLE_GOOSE).
 *
 * GOOSE (Generic Object Oriented Substation Event) is the fast raw-L2 multicast publish IEC 61850 uses
 * for protection trips. A GOOSE frame is an Ethernet frame (ethertype 0x88B8, see services/fieldbus/rawl2) with
 * an 8-octet GOOSE header (APPID, length, two reserved) followed by the BER-encoded `IECGoosePdu`:
 *
 *   61 len  { 80 gocbRef  81 timeAllowedToLive  82 datSet  83 goID  84 t(UtcTime,8)  85 stNum
 *             86 sqNum  87 simulation(BOOL)  88 confRev  89 ndsCom(BOOL)  8A numDatSetEntries  AB allData }
 *
 * This builds the control PDU (all fields above) with `allData` supplied as a caller-encoded BER blob
 * (a SEQUENCE of Data elements), then wraps it in the GOOSE header + Ethernet frame; `pc_goose_parse_frame`
 * decodes a received frame the other way (the subscriber side), walking the BER TLVs into a pc_goose_rx.
 * Pure, zero heap, no stdlib, host-testable; the raw-L2 transmit / receive is the
 * device step.
 */

#ifndef PROTOCORE_GOOSE_H
#define PROTOCORE_GOOSE_H

#include "protocore_config.h"

#if PC_ENABLE_GOOSE

/** @brief The GOOSE control fields (strings are borrowed, not copied). */
typedef struct
{
    const char *gocb_ref;          ///< the GOOSE control-block reference.
    uint32_t time_allowed_to_live; ///< ms the subscriber should keep this valid.
    const char *dat_set;           ///< the dataset reference.
    const char *go_id;             ///< the GOOSE id.
    const uint8_t *t;              ///< 8-octet UtcTime (seconds-since-epoch + fraction + quality).
    uint32_t st_num;               ///< state number (bumped on a data change).
    uint32_t sq_num;               ///< sequence number (bumped on each resend).
    proto_bool simulation;         ///< test/simulation flag.
    uint32_t conf_rev;             ///< configuration revision.
    proto_bool nds_com;            ///< needs-commissioning flag.
    uint32_t num_entries;          ///< number of dataset entries in allData.
    const uint8_t *all_data;       ///< pre-encoded BER SEQUENCE-of-Data body (contents of the AB field).
    size_t all_data_len;
} pc_goose;

/**
 * @brief Build the BER IECGoosePdu (the `61 ...` structure) into @p out. @return length, or 0 on overflow.
 */
size_t pc_goose_pdu(const pc_goose *g, uint8_t *out, size_t cap);

/**
 * @brief Build a full GOOSE Ethernet frame: [dst][src][88B8][APPID len rsvd rsvd][IECGoosePdu].
 * @param appid the GOOSE APPID.
 * @return the frame length, or 0 on overflow. Requires PC_ENABLE_RAWL2 for the Ethernet framing.
 */
size_t pc_goose_frame(const uint8_t *dst, const uint8_t *src, uint16_t appid, const pc_goose *g, uint8_t *out,
                      size_t cap);

/** @brief The decoded fields of a received GOOSE frame. String / blob members point INTO the source buffer
 *  and are NOT NUL-terminated (each has an explicit length), so the caller must keep the buffer alive. */
typedef struct
{
    uint16_t appid;       ///< GOOSE APPID from the header
    const char *gocb_ref; ///< gocbRef, or nullptr if absent
    size_t gocb_ref_len;
    uint32_t time_allowed_to_live; ///< ms the subscriber should keep this valid
    const char *dat_set;           ///< dataset reference, or nullptr
    size_t dat_set_len;
    const char *go_id; ///< GOOSE id, or nullptr
    size_t go_id_len;
    const uint8_t *t;        ///< 8-octet UtcTime, or nullptr
    uint32_t st_num;         ///< state number
    uint32_t sq_num;         ///< sequence number
    proto_bool simulation;   ///< test / simulation flag
    uint32_t conf_rev;       ///< configuration revision
    proto_bool nds_com;      ///< needs-commissioning flag
    uint32_t num_entries;    ///< number of dataset entries
    const uint8_t *all_data; ///< the BER-encoded allData contents (inside the 0xAB tag), or nullptr
    size_t all_data_len;
} pc_goose_rx;

/**
 * @brief Parse (subscribe to) a received GOOSE Ethernet frame into @p out. String / blob members borrow the
 *        input buffer. Unknown / future PDU tags are skipped.
 * @return true iff the ethertype is 0x88B8 and the IECGoosePdu parses without truncation; false otherwise.
 */
proto_bool pc_goose_parse_frame(const uint8_t *buf, size_t len, pc_goose_rx *out);

#endif // PC_ENABLE_GOOSE
#endif // PROTOCORE_GOOSE_H
