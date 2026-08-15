// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dds.h
 * @brief The DDSI-RTPS Message framing codec (PROTOCORE_ENABLE_DDS).
 *
 * Governing standard: OMG "The Real-time Publish-Subscribe Protocol DDS Interoperability Wire
 * Protocol (DDSI-RTPS) Specification" version 2.5, OMG document formal/2022-04-01. DDS and its
 * wire protocol are OMG specifications, not IETF ones, so no RFC governs them and none is cited.
 *
 * DDSI-RTPS sec 8.3.3: a Message is a fixed-size leading Header followed by a variable number of
 * Submessages, and each Submessage is a SubmessageHeader followed by SubmessageElements.
 *
 *     Header, 20 octets (sec 9.4.4)      'R' 'T' 'P' 'S' | version 2 | vendorId 2 | guidPrefix 12
 *     SubmessageHeader, 4 (sec 9.4.5.1)  submessageId 1 | flags 1 | octetsToNextHeader 2
 *
 * sec 9.4.5.1 maps the EndiannessFlag onto the least-significant bit of flags, E = flags & 0x01,
 * with E=0 big-endian and E=1 little-endian, and octetsToNextHeader is a CDR ushort in that order.
 *
 * sec 9.4.1: a Message carries no length of its own, the transport supplies it. Over UDP that
 * length is the UDP payload length.
 *
 * This module is the Message and SubmessageHeader framing. The Submessage contents (a Data
 * Submessage's serializedPayload, a Heartbeat's SequenceNumber set, the SPDP and SEDP discovery
 * topics) layer on top of it.
 *
 * The module exports one symbol, @ref Rtps. Everything in dds.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DDS_H
#define PROTOCORE_DDS_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_DDS

/** @brief DDSI-RTPS sec 9.4.5.1 SubmessageKind: the submessageId octet of a SubmessageHeader. */
#define RTPS_SM_PAD 0x01
#define RTPS_SM_ACKNACK 0x06
#define RTPS_SM_HEARTBEAT 0x07
#define RTPS_SM_GAP 0x08
#define RTPS_SM_INFO_TS 0x09
#define RTPS_SM_INFO_SRC 0x0c
#define RTPS_SM_INFO_REPLY_IP4 0x0d
#define RTPS_SM_INFO_DST 0x0e
#define RTPS_SM_INFO_REPLY 0x0f
#define RTPS_SM_DATA 0x15
#define RTPS_SM_DATA_FRAG 0x16
#define RTPS_FLAG_ENDIAN 0x01  ///< EndiannessFlag 'E', flags bit 0: E=1 little-endian (sec 9.4.5.1).
#define RTPS_HEADER_LEN 20     ///< Header: magic 4 + version 2 + vendorId 2 + guidPrefix 12 (sec 9.4.4).
#define RTPS_GUIDPREFIX_LEN 12 ///< GuidPrefix_t is 12 octets (sec 9.3.1.1).

/**
 * @brief One Submessage a parse surfaces: its SubmessageHeader fields and its contents.
 *
 * @param submessage_id  the SubmessageKind octet (sec 9.4.5.1)
 * @param flags          the 8 SubmessageFlags, bit 0 the EndiannessFlag
 * @param contents       the Submessage contents, NULL when there are none
 * @param contents_len   how many octets they run
 * @param arg            the caller's pointer, handed back untouched
 */
typedef void (*protocore_rtps_submessage_cb)(uint8_t submessage_id, uint8_t flags, const uint8_t *contents,
                                             size_t contents_len, void *arg);

/** @brief sec 8.3.3.1 Table 8.14: the Header fields a build stamps, less the protocol and version. */
typedef struct
{
    const uint8_t *guid_prefix; ///< guidPrefix, the 12-octet default GUID prefix for the Message
    const uint8_t *vendor_id;   ///< vendorId, 2 octets; VENDORID_UNKNOWN is {0, 0} (sec 9.3.2.1)
} RtpsHeaderArgs;

/** @brief sec 8.3.3.3 Table 8.16: one Submessage, its SubmessageHeader fields and its contents. */
typedef struct
{
    const uint8_t *contents; ///< the Submessage contents, NULL only when contents_len is 0
    uint16_t contents_len;   ///< their length, written as octetsToNextHeader (sec 9.4.5.1)
    uint8_t submessage_id;   ///< the SubmessageKind octet, one of RTPS_SM_*
    uint8_t flags;           ///< the 8 SubmessageFlags; OR RTPS_FLAG_ENDIAN for little-endian
} RtpsSubmessageArgs;

/** @brief Where a build lays its octets down. */
typedef struct
{
    uint8_t *buf; ///< the buffer a build writes into
    size_t cap;   ///< how much room it has
} RtpsOutArgs;

/** @brief The Message a parse walks, its length supplied by the transport (sec 9.4.1). */
typedef struct
{
    const uint8_t *msg; ///< the whole Message, Header first
    size_t len;         ///< its octet count, over UDP the payload length
} RtpsMessageArgs;

/** @brief Where a parse surfaces each Submessage it walks. */
typedef struct
{
    protocore_rtps_submessage_cb on_submessage; ///< called once per Submessage, NULL to only validate
    void *arg;                                  ///< handed back to it untouched
} RtpsSinkArgs;

/** @brief The codec's own state and the calls that reach it, described only in dds.c. */
struct RtpsInternal;

/**
 * @brief The DDSI-RTPS Message framing codec.
 *
 * A caller sets the members a call takes, invokes it through ::Rtps, and reads the outcome off the
 * same handle.
 *
 * No slot member: the codec keeps no rows, so no call names one.
 *
 * @var RtpsNs::hdr         the Header fields a header stamps (sec 8.3.3.1)
 * @var RtpsNs::sub         the SubmessageHeader fields and contents a submessage writes (sec 8.3.3.3)
 * @var RtpsNs::out         the buffer a header or a submessage writes into
 * @var RtpsNs::msg         the Message a parse walks (sec 9.4.1)
 * @var RtpsNs::sink        where a parse surfaces each Submessage
 * @var RtpsNs::ok          a parse's verdict: the Header is valid and every Submessage fits
 * @var RtpsNs::n           the octets a header or a submessage wrote, 0 when it did not fit
 * @var RtpsNs::header      build the 20-octet Header into @c out (sec 9.4.4)
 * @var RtpsNs::submessage  build one SubmessageHeader and its contents into @c out (sec 9.4.5.1)
 * @var RtpsNs::parse       validate the Header and walk the Submessages, surfacing each to @c sink
 * @var RtpsNs::internal    the codec's state and the calls that reach it
 */
typedef struct
{
    RtpsHeaderArgs hdr;     ///< what a Header stamps
    RtpsSubmessageArgs sub; ///< what one Submessage says
    RtpsOutArgs out;        ///< where a build lands
    RtpsMessageArgs msg;    ///< what a parse walks
    RtpsSinkArgs sink;      ///< where a parse reports

    proto_bool ok;
    size_t n;

    void (*header)(struct RtpsInternal *ctx);
    void (*submessage)(struct RtpsInternal *ctx);
    void (*parse)(struct RtpsInternal *ctx);

    struct RtpsInternal *internal;
} RtpsNs;

/** @brief The protocol version a built Header stamps, major then minor (sec 8.3.3.1). */
extern const uint8_t RTPS_VERSION[2];

/** @brief The one symbol this module exports. */
extern RtpsNs Rtps;

#endif // PROTOCORE_ENABLE_DDS

PROTOCORE_END_DECLS

#endif // PROTOCORE_DDS_H
