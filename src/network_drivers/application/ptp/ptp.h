// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ptp.h
 * @brief PTP / IEEE 1588-2008 (PTPv2) message codec + slave clock math (PROTOCORE_ENABLE_PTP).
 *
 * The Precision Time Protocol synchronizes clocks across a LAN to sub-microsecond accuracy by
 * exchanging timestamped messages. This codec builds and parses the PTPv2 wire format - the 34-octet
 * common header, the 10-octet (48-bit seconds + 32-bit nanoseconds) timestamp, the E2E Sync /
 * Delay_Req / Follow_Up / Delay_Resp / Announce messages, and the P2P peer-delay messages (Pdelay_Req /
 * Pdelay_Resp / Pdelay_Resp_Follow_Up, IEEE 1588-2008 §11.4) - and computes an ordinary-clock **slave**'s
 * offset-from-master and mean-path-delay from the four transfer timestamps (t1..t4), plus the P2P
 * meanLinkDelay. All multi-octet
 * fields are big-endian (network order), per IEEE 1588-2008 clause 13. Pure and host-tested; the UDP
 * transport (event port 319, general port 320, multicast 224.0.1.129) and the local timestamping are
 * the application's - see the Ptp example for the ordinary-clock slave that drives this codec.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PTP_H
#define PROTOCORE_PTP_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_PTP

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief PTPv2 messageType values (low nibble of octet 0). */
enum protocore_ptp_msg_type
{
    PROTOCORE_PTP_SYNC = 0x0,
    PROTOCORE_PTP_PDELAY_REQ = 0x2,  ///< peer-delay request (P2P mechanism)
    PROTOCORE_PTP_PDELAY_RESP = 0x3, ///< peer-delay response (P2P mechanism)
    PROTOCORE_PTP_DELAY_REQ = 0x1,
    PROTOCORE_PTP_FOLLOW_UP = 0x8,
    PROTOCORE_PTP_DELAY_RESP = 0x9,
    PROTOCORE_PTP_PDELAY_RESP_FOLLOW_UP = 0xA, ///< peer-delay response follow-up (two-step P2P)
    PROTOCORE_PTP_ANNOUNCE = 0xB
};

#define PROTOCORE_PTP_HEADER_LEN 34     ///< common header length
#define PROTOCORE_PTP_TS_LEN 10         ///< on-wire timestamp length (6-octet seconds + 4-octet nanoseconds)
#define PROTOCORE_PTP_PDELAY_REQ_LEN 54 ///< header, originTimestamp, and the reserved field after it
#define PROTOCORE_PTP_EVENT_PORT 319    ///< UDP port for event messages (Sync, Delay_Req)
#define PROTOCORE_PTP_GENERAL_PORT 320  ///< UDP port for general messages (Follow_Up, Delay_Resp, Announce)

/** @brief A PTP timestamp: 48-bit seconds + 32-bit nanoseconds. */
typedef struct
{
    uint64_t seconds;     ///< seconds (only the low 48 bits are on the wire)
    uint32_t nanoseconds; ///< nanoseconds within the second (0 .. 999999999)
} protocore_ptp_timestamp;

/** @brief The 34-octet PTPv2 common header. */
typedef struct
{
    uint8_t message_type;       ///< protocore_ptp_msg_type (octet 0 low nibble)
    uint8_t transport_specific; ///< octet 0 high nibble (majorSdoId)
    uint8_t version;            ///< PTP version (octet 1 low nibble); 2 for PTPv2
    uint16_t message_length;    ///< total message length in octets
    uint8_t domain;             ///< domainNumber
    uint16_t flags;             ///< flagField
    int64_t correction;         ///< correctionField (nanoseconds scaled by 2^16)
    uint8_t clock_identity[8];  ///< sourcePortIdentity clockIdentity
    uint16_t port_number;       ///< sourcePortIdentity portNumber
    uint16_t sequence_id;       ///< sequenceId
    uint8_t control;            ///< controlField
    int8_t log_interval;        ///< logMessageInterval
} protocore_ptp_header;

/** @brief Parsed Delay_Resp body. */
typedef struct
{
    protocore_ptp_timestamp receive; ///< receiveTimestamp (t4, when the master got our Delay_Req)
    uint8_t req_clock_id[8];         ///< requestingPortIdentity clockIdentity (echoes our clock id)
    uint16_t req_port;               ///< requestingPortIdentity portNumber
} protocore_ptp_delay_resp;

/** @brief Parsed Pdelay_Resp / Pdelay_Resp_Follow_Up body (P2P peer-delay mechanism). */
typedef struct
{
    protocore_ptp_timestamp
        timestamp;           ///< Pdelay_Resp: requestReceiptTimestamp (t2); Follow_Up: responseOriginTimestamp (t3)
    uint8_t req_clock_id[8]; ///< requestingPortIdentity clockIdentity (echoes the Pdelay_Req sender)
    uint16_t req_port;       ///< requestingPortIdentity portNumber
} protocore_ptp_pdelay_resp;

/** @brief Parsed Announce body (the master's quality, for best-master selection / display). */
typedef struct
{
    protocore_ptp_timestamp origin; ///< originTimestamp
    int16_t utc_offset;             ///< currentUtcOffset (TAI - UTC, seconds)
    uint8_t gm_priority1;           ///< grandmasterPriority1
    uint8_t gm_clock_class;         ///< grandmasterClockQuality.clockClass
    uint8_t gm_clock_accuracy;      ///< grandmasterClockQuality.clockAccuracy
    uint16_t gm_variance;           ///< grandmasterClockQuality.offsetScaledLogVariance
    uint8_t gm_priority2;           ///< grandmasterPriority2
    uint8_t gm_identity[8];         ///< grandmasterIdentity
    uint16_t steps_removed;         ///< stepsRemoved
    uint8_t time_source;            ///< timeSource
} protocore_ptp_announce;

/** @brief Slave sync result: offset from master and mean path delay, in nanoseconds. */
typedef struct
{
    int64_t offset_ns; ///< offsetFromMaster (local - master); subtract to correct the local clock
    int64_t delay_ns;  ///< meanPathDelay
} protocore_ptp_sync;

/** @brief What ts_write takes: p, ts. */
typedef struct
{
    uint8_t *p;
    const protocore_ptp_timestamp *ts;
} PtpTsWriteArgs;

/** @brief What ts_read takes: p, ts. */
typedef struct
{
    const uint8_t *p;
    protocore_ptp_timestamp *ts;
} PtpTsReadArgs;

/** @brief What ts_to_ns takes: ts. */
typedef struct
{
    const protocore_ptp_timestamp *ts;
} PtpTsToNsArgs;

/** @brief What ts_from_ns takes: ns, ts. */
typedef struct
{
    int64_t ns;
    protocore_ptp_timestamp *ts;
} PtpTsFromNsArgs;

/** @brief What build_header takes: buf, cap, h, body_len. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const protocore_ptp_header *h;
    uint16_t body_len;
} PtpBuildHeaderArgs;

/** @brief What parse_header takes: s, len, h. */
typedef struct
{
    const uint8_t *s;
    size_t len;
    protocore_ptp_header *h;
} PtpParseHeaderArgs;

/** @brief What build_sync takes: buf, cap, h, origin. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const protocore_ptp_header *h;
    const protocore_ptp_timestamp *origin;
} PtpBuildSyncArgs;

/** @brief What build_delay_req takes: buf, cap, h, origin. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const protocore_ptp_header *h;
    const protocore_ptp_timestamp *origin;
} PtpBuildDelayReqArgs;

/** @brief What build_follow_up takes: buf, cap, h, precise. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const protocore_ptp_header *h;
    const protocore_ptp_timestamp *precise;
} PtpBuildFollowUpArgs;

/** @brief What build_delay_resp takes: buf, cap, h, recv, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const protocore_ptp_header *h;
    const protocore_ptp_timestamp *recv;
    const uint8_t *req_clock_id;
    uint16_t req_port;
} PtpBuildDelayRespArgs;

/** @brief What build_announce takes: buf, cap, h, a. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const protocore_ptp_header *h;
    const protocore_ptp_announce *a;
} PtpBuildAnnounceArgs;

/** @brief What build_pdelay_req takes: buf, cap, h, origin. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const protocore_ptp_header *h;
    const protocore_ptp_timestamp *origin;
} PtpBuildPdelayReqArgs;

/** @brief What build_pdelay_resp takes: buf, cap, h, recv, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const protocore_ptp_header *h;
    const protocore_ptp_timestamp *recv;
    const uint8_t *req_clock_id;
    uint16_t req_port;
} PtpBuildPdelayRespArgs;

/** @brief What build_pdelay_resp_follow_up takes: buf, cap, h, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const protocore_ptp_header *h;
    const protocore_ptp_timestamp *origin;
    const uint8_t *req_clock_id;
    uint16_t req_port;
} PtpBuildPdelayRespFollowUpArgs;

/** @brief What parse_timestamp_msg takes: s, len, h, ts. */
typedef struct
{
    const uint8_t *s;
    size_t len;
    protocore_ptp_header *h;
    protocore_ptp_timestamp *ts;
} PtpParseTimestampMsgArgs;

/** @brief What parse_delay_resp takes: s, len, h, out. */
typedef struct
{
    const uint8_t *s;
    size_t len;
    protocore_ptp_header *h;
    protocore_ptp_delay_resp *out;
} PtpParseDelayRespArgs;

/** @brief What parse_announce takes: s, len, h, out. */
typedef struct
{
    const uint8_t *s;
    size_t len;
    protocore_ptp_header *h;
    protocore_ptp_announce *out;
} PtpParseAnnounceArgs;

/** @brief What parse_pdelay_req takes: s, len, h, ts. */
typedef struct
{
    const uint8_t *s;
    size_t len;
    protocore_ptp_header *h;
    protocore_ptp_timestamp *ts;
} PtpParsePdelayReqArgs;

/** @brief What parse_pdelay_resp takes: s, len, h, out. */
typedef struct
{
    const uint8_t *s;
    size_t len;
    protocore_ptp_header *h;
    protocore_ptp_pdelay_resp *out;
} PtpParsePdelayRespArgs;

/** @brief What parse_pdelay_resp_follow_up takes: s, len, h, out. */
typedef struct
{
    const uint8_t *s;
    size_t len;
    protocore_ptp_header *h;
    protocore_ptp_pdelay_resp *out;
} PtpParsePdelayRespFollowUpArgs;

/** @brief What compute takes: t1, t2, t3, t4, out. */
typedef struct
{
    int64_t t1;
    int64_t t2;
    int64_t t3;
    int64_t t4;
    protocore_ptp_sync *out;
} PtpComputeArgs;

/** @brief What compute_link_delay takes: t1, t2, t3, t4. */
typedef struct
{
    int64_t t1;
    int64_t t2;
    int64_t t3;
    int64_t t4;
} PtpComputeLinkDelayArgs;

/**
 * @brief PTP / IEEE 1588-2008 (PTPv2) message codec + slave clock math (PROTOCORE_ENABLE_PTP).
 *
 * A caller sets the members a call takes, invokes it through ::Ptp with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Ptp.ts_write_args.p = ...;
 *   Ptp.ts_write_args.ts = ...;
 *   Ptp.ts_write(work);
 *
 * @var PtpNs::ts_write_args  what ts_write takes: p, ts
 * @var PtpNs::ts_read_args  what ts_read takes: p, ts
 * @var PtpNs::ts_to_ns_args  what ts_to_ns takes: ts
 * @var PtpNs::ts_from_ns_args  what ts_from_ns takes: ns, ts
 * @var PtpNs::build_header_args  what build_header takes: buf, cap, h, body_len
 * @var PtpNs::parse_header_args  what parse_header takes: s, len, h
 * @var PtpNs::build_sync_args  what build_sync takes: buf, cap, h, origin
 * @var PtpNs::build_delay_req_args  what build_delay_req takes: buf, cap, h, origin
 * @var PtpNs::build_follow_up_args  what build_follow_up takes: buf, cap, h, precise
 * @var PtpNs::build_delay_resp_args  what build_delay_resp takes: buf, cap, h, recv,
 * @var PtpNs::build_announce_args  what build_announce takes: buf, cap, h, a
 * @var PtpNs::build_pdelay_req_args  what build_pdelay_req takes: buf, cap, h, origin
 * @var PtpNs::build_pdelay_resp_args  what build_pdelay_resp takes: buf, cap, h, recv,
 * @var PtpNs::build_pdelay_resp_follow_up_args  what build_pdelay_resp_follow_up takes: buf, cap, h,
 * @var PtpNs::parse_timestamp_msg_args  what parse_timestamp_msg takes: s, len, h, ts
 * @var PtpNs::parse_delay_resp_args  what parse_delay_resp takes: s, len, h, out
 * @var PtpNs::parse_announce_args  what parse_announce takes: s, len, h, out
 * @var PtpNs::parse_pdelay_req_args  what parse_pdelay_req takes: s, len, h, ts
 * @var PtpNs::parse_pdelay_resp_args  what parse_pdelay_resp takes: s, len, h, out
 * @var PtpNs::parse_pdelay_resp_follow_up_args  what parse_pdelay_resp_follow_up takes: s, len, h, out
 * @var PtpNs::compute_args  what compute takes: t1, t2, t3, t4, out
 * @var PtpNs::compute_link_delay_args  what compute_link_delay takes: t1, t2, t3, t4
 * @var PtpNs::ok  a call's true/false outcome
 * @var PtpNs::value  the value a call reports
 * @var PtpNs::n  PROTOCORE_PTP_HEADER_LEN or 0 on overflow / bad args
 * @var PtpNs::ts_write  write ts to p as the 10-octet on-wire form (big-endian)
 * @var PtpNs::ts_read  read a 10-octet on-wire timestamp from p into ts
 * @var PtpNs::ts_to_ns  convert ts to signed nanoseconds since its epoch (fits current ...
 * @var PtpNs::ts_from_ns  convert signed-nanoseconds ns to a timestamp
 * @var PtpNs::build_header  build the 34-octet common header into buf, stamping messageLength = ...
 * @var PtpNs::parse_header  parse the common header from s (len octets). Returns false if too ...
 * @var PtpNs::build_sync  build a Sync (origin is the originTimestamp; 0 for a two-step Sync)
 * @var PtpNs::build_delay_req  build a Delay_Req (origin is the originTimestamp; usually 0)
 * @var PtpNs::build_follow_up  build a Follow_Up carrying the precise Sync egress time precise (t1)
 * @var PtpNs::build_delay_resp  build a Delay_Resp carrying t4 recv and the requester's port ...
 * @var PtpNs::build_announce  build an Announce from a - master mode: advertise this clock's ...
 * @var PtpNs::build_pdelay_req  build a Pdelay_Req (origin is the originTimestamp, usually 0; the ...
 * @var PtpNs::build_pdelay_resp  build a Pdelay_Resp carrying t2 recv (requestReceiptTimestamp) + ...
 * @var PtpNs::build_pdelay_resp_follow_up  build a Pdelay_Resp_Follow_Up carrying t3 origin ...
 * @var PtpNs::parse_timestamp_msg  parse a Sync / Delay_Req / Follow_Up message: fills h and its ...
 * @var PtpNs::parse_delay_resp  parse a Delay_Resp into h + out. Returns false on a short / ...
 * @var PtpNs::parse_announce  parse an Announce into h + out. Returns false on a short / ...
 * @var PtpNs::parse_pdelay_req  parse a Pdelay_Req into h + its originTimestamp ts. False on a ...
 * @var PtpNs::parse_pdelay_resp  parse a Pdelay_Resp into h + out (timestamp is t2). False on a ...
 * @var PtpNs::parse_pdelay_resp_follow_up  parse a Pdelay_Resp_Follow_Up into h + out (timestamp is t3). False ...
 * @var PtpNs::compute  compute offsetFromMaster and meanPathDelay from the four PTP ...
 * @var PtpNs::compute_link_delay  compute the meanLinkDelay for the P2P mechanism (IEEE 1588-2008 ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    PtpTsWriteArgs ts_write_args;
    PtpTsReadArgs ts_read_args;
    PtpTsToNsArgs ts_to_ns_args;
    PtpTsFromNsArgs ts_from_ns_args;
    PtpBuildHeaderArgs build_header_args;
    PtpParseHeaderArgs parse_header_args;
    PtpBuildSyncArgs build_sync_args;
    PtpBuildDelayReqArgs build_delay_req_args;
    PtpBuildFollowUpArgs build_follow_up_args;
    PtpBuildDelayRespArgs build_delay_resp_args;
    PtpBuildAnnounceArgs build_announce_args;
    PtpBuildPdelayReqArgs build_pdelay_req_args;
    PtpBuildPdelayRespArgs build_pdelay_resp_args;
    PtpBuildPdelayRespFollowUpArgs build_pdelay_resp_follow_up_args;
    PtpParseTimestampMsgArgs parse_timestamp_msg_args;
    PtpParseDelayRespArgs parse_delay_resp_args;
    PtpParseAnnounceArgs parse_announce_args;
    PtpParsePdelayReqArgs parse_pdelay_req_args;
    PtpParsePdelayRespArgs parse_pdelay_resp_args;
    PtpParsePdelayRespFollowUpArgs parse_pdelay_resp_follow_up_args;
    PtpComputeArgs compute_args;
    PtpComputeLinkDelayArgs compute_link_delay_args;

    proto_bool ok;
    int64_t value;
    size_t n;

    void (*const ts_write)(uint8_t *restrict work);
    void (*const ts_read)(uint8_t *restrict work);
    void (*const ts_to_ns)(uint8_t *restrict work);
    void (*const ts_from_ns)(uint8_t *restrict work);
    void (*const build_header)(uint8_t *restrict work);
    void (*const parse_header)(uint8_t *restrict work);
    void (*const build_sync)(uint8_t *restrict work);
    void (*const build_delay_req)(uint8_t *restrict work);
    void (*const build_follow_up)(uint8_t *restrict work);
    void (*const build_delay_resp)(uint8_t *restrict work);
    void (*const build_announce)(uint8_t *restrict work);
    void (*const build_pdelay_req)(uint8_t *restrict work);
    void (*const build_pdelay_resp)(uint8_t *restrict work);
    void (*const build_pdelay_resp_follow_up)(uint8_t *restrict work);
    void (*const parse_timestamp_msg)(uint8_t *restrict work);
    void (*const parse_delay_resp)(uint8_t *restrict work);
    void (*const parse_announce)(uint8_t *restrict work);
    void (*const parse_pdelay_req)(uint8_t *restrict work);
    void (*const parse_pdelay_resp)(uint8_t *restrict work);
    void (*const parse_pdelay_resp_follow_up)(uint8_t *restrict work);
    void (*const compute)(uint8_t *restrict work);
    void (*const compute_link_delay)(uint8_t *restrict work);
} PtpNs;

/** @brief The one symbol this module exports. */
extern PtpNs Ptp;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_PTP

#endif // PROTOCORE_PTP_H
