// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the PTPv2 message codec (network_drivers/application/ptp/ptp.h).
//
// IEEE 1588-2008 is a paid document and is not in this tree. The wire format tested here is taken
// from IEEE Std 802.1AS-2020, a published profile of IEEE Std 1588 that reproduces the PTP data
// types and the common message header verbatim, downloaded and read in this session
// (standards.ieee.org GET program). Its clause numbers are cited on every case that uses it.
//
// The two load-bearing cases rest on values that document prints in full:
//   test_correction_field_published_example - 802.1AS-2020 6.4.3.3: "2.5 ns is expressed as
//     0x0000 0000 0002 8000", repeated in 11.4.2.6.
//   test_timestamp_published_example - 802.1AS-2020 6.4.3.4: "+2.000000001 seconds is represented
//     by seconds = 0x0000 0000 0002 and nanoseconds = 0x0000 0001".
// The header and body offsets come from Table 10-7 (PTP message header), Table 10-11 (Announce),
// Table 11-8 (Sync), Table 11-10 (Follow_Up), Table 11-12 (Pdelay_Req), Table 11-13 (Pdelay_Resp)
// and Table 11-14 (Pdelay_Resp_Follow_Up). Octet order is 6.4.4.3 / 6.4.4.4 (most significant octet
// nearest the beginning of the PDU). The 48-bit-seconds + 32-bit-nanoseconds timestamp is also
// published in RFC 8877 sec 4.3 ("PTP uses an 80-bit timestamp format ... Nanoseconds ... in the
// range 0 to (10^9)-1"). The E2E offset and delay equations are derived from RFC 5905 sec 8
// (docs/learn/rfc/text/rfc5905.txt), the derivation is written out on the case. The P2P link delay
// is 802.1AS-2020 Equation (11-1). Ports 319 and 320 are the IANA Service Name and Transport
// Protocol Port Number Registry assignments ptp-event and ptp-general.
//
// What could not be sourced. 802.1AS uses only the peer-delay mechanism, so it publishes no
// messageType number for Delay_Req or Delay_Resp and no body layout for them; IEEE 1588-2008
// Table 19 and Tables 27 to 31 would, and could not be obtained. Those two enumerators are
// therefore asserted only through the published event/general rule of 11.4.2.2, and the Delay_Resp
// body is asserted as the PortIdentity structure of 6.4.3.7 plus a round trip, never as a number
// read off ptp.h. Nothing about a negative nanosecond input to a host helper is published anywhere,
// so test_a_negative_instant_still_yields_a_well_formed_timestamp asserts only that the output is a
// legal Timestamp.
//
// Scope note: the module implements plain IEEE 1588-2008, not the gPTP profile, so the profile-only
// additions of 802.1AS (minorVersionPTP = 1, the 32-octet Follow_Up information TLV, the path trace
// TLV, majorSdoId = 0x1) are not asserted here.
//
// test_pdelay_req_frame_is_fifty_four_octets FAILS. See its comment.

#include "network_drivers/application/ptp/ptp.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static const uint8_t CLOCK_ID[8] = {0x00, 0x1B, 0x19, 0xFF, 0xFE, 0x00, 0x00, 0x01};
static const uint8_t REQ_ID[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22};

static void base_header(protocore_ptp_header *h)
{
    memset(h, 0, sizeof(*h));
    h->version = 2;
    h->domain = 0;
    memcpy(h->clock_identity, CLOCK_ID, 8);
    h->port_number = 1;
    h->sequence_id = 0x1234;
    h->log_interval = -3;
}

// 6.4.4.4: a multi-octet numeric field puts its most significant octet nearest the start of the PDU.
static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

// 802.1AS-2020 6.4.3.4 prints one example of the Timestamp type:
//   "+2.000000001 seconds is represented by seconds = 0x0000 0000 0002 and nanoseconds = 0x0000 0001"
// 2.000000001 s in signed nanoseconds is 2 * 10^9 + 1 = 2000000001.
void test_timestamp_published_example(void)
{
    protocore_ptp_timestamp ts;
    protocore_ptp_ts_from_ns(2000000001LL, &ts);
    TEST_ASSERT_EQUAL_UINT64(0x000000000002ULL, ts.seconds);
    TEST_ASSERT_EQUAL_UINT32(0x00000001U, ts.nanoseconds);
    TEST_ASSERT_EQUAL_INT64(2000000001LL, protocore_ptp_ts_to_ns(&ts));

    // 6.4.3.4 struct Timestamp {UInteger48 seconds; UInteger32 nanoseconds;}, written per 6.4.4.5
    // (first member nearest the start, no padding) and 6.4.4.4, so 6 octets then 4 octets.
    uint8_t w[PROTOCORE_PTP_TS_LEN];
    protocore_ptp_ts_write(w, &ts);
    static const uint8_t WANT[10] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, w, sizeof(WANT));
}

// RFC 8877 sec 4.3: "PTP uses an 80-bit timestamp format", 80 bits = 10 octets, of which the low 32
// are Nanoseconds, leaving 48 for Seconds. 802.1AS-2020 6.4.3.4 gives the same split by type.
void test_timestamp_octet_layout(void)
{
    TEST_ASSERT_EQUAL_size_t(10, (size_t)PROTOCORE_PTP_TS_LEN);

    protocore_ptp_timestamp ts;
    ts.seconds = 0x010203040506ULL;
    ts.nanoseconds = 0x0708090AU;
    uint8_t w[PROTOCORE_PTP_TS_LEN];
    protocore_ptp_ts_write(w, &ts);
    static const uint8_t WANT[10] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, w, sizeof(WANT));

    protocore_ptp_timestamp back;
    protocore_ptp_ts_read(w, &back);
    TEST_ASSERT_EQUAL_UINT64(0x010203040506ULL, back.seconds);
    TEST_ASSERT_EQUAL_UINT32(0x0708090AU, back.nanoseconds);

    // The widest value each member holds: 2^48-1 seconds and 10^9-1 nanoseconds.
    ts.seconds = 0xFFFFFFFFFFFFULL;
    ts.nanoseconds = 999999999U;
    protocore_ptp_ts_write(w, &ts);
    protocore_ptp_ts_read(w, &back);
    TEST_ASSERT_EQUAL_UINT64(0xFFFFFFFFFFFFULL, back.seconds);
    TEST_ASSERT_EQUAL_UINT32(999999999U, back.nanoseconds);
}

// 802.1AS-2020 6.4.3.4: "The nanoseconds member is always less than 10^9", so the split of an
// instant into (seconds, nanoseconds) is unique and the conversion is invertible.
void test_nanosecond_conversion_is_exact_and_carries_at_one_second(void)
{
    static const int64_t NS[] = {0, 1, 999999999LL, 1000000000LL, 1000000001LL, 1234567890123456789LL};
    for (size_t i = 0; i < sizeof(NS) / sizeof(NS[0]); i++)
    {
        protocore_ptp_timestamp ts;
        protocore_ptp_ts_from_ns(NS[i], &ts);
        TEST_ASSERT_TRUE(ts.nanoseconds < 1000000000U);
        TEST_ASSERT_EQUAL_INT64(NS[i], protocore_ptp_ts_to_ns(&ts));
    }

    // 1500000000 ns = 1 * 10^9 + 500000000.
    protocore_ptp_timestamp ts;
    protocore_ptp_ts_from_ns(1500000000LL, &ts);
    TEST_ASSERT_EQUAL_UINT64(1, ts.seconds);
    TEST_ASSERT_EQUAL_UINT32(500000000U, ts.nanoseconds);
}

// 6.4.3.4 makes Timestamp "a positive time with respect to the epoch" over unsigned members, so a
// negative instant has no representation and no document states what a converter owes for one.
// Only the well-formedness of the output is asserted: whatever it does, it must not hand back a
// nanoseconds member of one second or more.
void test_a_negative_instant_still_yields_a_well_formed_timestamp(void)
{
    protocore_ptp_timestamp ts;

    protocore_ptp_ts_from_ns(-1, &ts);
    TEST_ASSERT_TRUE(ts.nanoseconds < 1000000000U);

    protocore_ptp_ts_from_ns(-1000000001LL, &ts);
    TEST_ASSERT_TRUE(ts.nanoseconds < 1000000000U);

    protocore_ptp_ts_from_ns(-1234567890123456789LL, &ts);
    TEST_ASSERT_TRUE(ts.nanoseconds < 1000000000U);
}

// 802.1AS-2020 6.4.3.3: "The TimeInterval type represents time intervals, in units of 2^-16 ns. ...
// For example: 2.5 ns is expressed as: 0x0000 0000 0002 8000", and 11.4.2.6 repeats it of the
// correctionField itself. 2.5 * 65536 = 163840 = 0x28000, which is that value.
// The negative of the same interval, as an Integer64 in twos complement (6.4.2):
//   2^64 - 0x28000 = 0xFFFFFFFFFFFD8000.
void test_correction_field_published_example(void)
{
    protocore_ptp_header h;
    base_header(&h);
    h.message_type = PROTOCORE_PTP_SYNC;
    h.correction = 0x0000000000028000LL;
    TEST_ASSERT_EQUAL_INT64(163840LL, h.correction);

    uint8_t buf[PROTOCORE_PTP_HEADER_LEN];
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_PTP_HEADER_LEN, protocore_ptp_build_header(buf, sizeof(buf), &h, 0));

    // Table 10-7 puts correctionField at offset 8, 8 octets, most significant octet first (6.4.4.4).
    static const uint8_t WANT[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x80, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf + 8, 8);

    protocore_ptp_header back;
    TEST_ASSERT_TRUE(protocore_ptp_parse_header(buf, sizeof(buf), &back));
    TEST_ASSERT_EQUAL_INT64(0x0000000000028000LL, back.correction);

    h.correction = -163840LL;
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_PTP_HEADER_LEN, protocore_ptp_build_header(buf, sizeof(buf), &h, 0));
    static const uint8_t WANT_NEG[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFD, 0x80, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT_NEG, buf + 8, 8);
    TEST_ASSERT_TRUE(protocore_ptp_parse_header(buf, sizeof(buf), &back));
    TEST_ASSERT_EQUAL_INT64(-163840LL, back.correction);
}

// 802.1AS-2020 Table 10-7 (PTP message header), octets and offset:
//   majorSdoId (bits 7..4) + messageType (bits 3..0)   1   0
//   minorVersionPTP (bits 7..4) + versionPTP (3..0)    1   1
//   messageLength                                      2   2
//   domainNumber                                       1   4
//   minorSdoId                                         1   5
//   flags                                              2   6
//   correctionField                                    8   8
//   messageTypeSpecific                                4   16
//   sourcePortIdentity                                 10  20
//   sequenceId                                         2   30
//   controlField                                       1   32
//   logMessageInterval                                 1   33
// 33 + 1 = 34 octets. sourcePortIdentity is a PortIdentity (6.4.3.7): ClockIdentity, which is an
// Octet8 (6.4.3.6), then UInteger16 portNumber, laid out first member first with no padding
// (6.4.4.5), so the clock identity is 20..27 and the port number is 28..29.
void test_common_header_field_offsets(void)
{
    protocore_ptp_header h;
    base_header(&h);
    h.message_type = PROTOCORE_PTP_ANNOUNCE; // 0xB, Table 10-8
    h.transport_specific = 0x1;              // majorSdoId
    h.domain = 0x2A;
    h.flags = 0x0208;
    h.correction = 0;
    h.port_number = 0x0003;
    h.sequence_id = 0x1234;
    h.control = 0x05;
    h.log_interval = -3;

    uint8_t buf[PROTOCORE_PTP_HEADER_LEN];
    TEST_ASSERT_EQUAL_size_t(34, (size_t)PROTOCORE_PTP_HEADER_LEN);
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_PTP_HEADER_LEN, protocore_ptp_build_header(buf, sizeof(buf), &h, 30));

    // 6.4.4.4: "one octet contains multiple fields ... the bit positions within the octet ... shall
    // be preserved", so octet 0 is (majorSdoId << 4) | messageType = (0x1 << 4) | 0xB = 0x1B.
    TEST_ASSERT_EQUAL_HEX8(0x1B, buf[0]);
    // 10.6.2.2.4: versionPTP "shall be 2". The module is IEEE 1588-2008, where the nibble above it
    // is reserved and transmitted 0, so octet 1 is 0x02.
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[1]);
    // 10.6.2.2.5: messageLength is "the total number of octets that form the PTP message",
    // 34 + 30 = 64.
    TEST_ASSERT_EQUAL_UINT16(64, be16(buf + 2));
    TEST_ASSERT_EQUAL_HEX8(0x2A, buf[4]);
    // 8.1: "The value of minorSdoId for a gPTP domain shall be 0x00"; NOTE 2 there records that the
    // same octet was a reserved field "transmitted as 0" before minorSdoId existed.
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[5]);
    TEST_ASSERT_EQUAL_UINT16(0x0208, be16(buf + 6));
    // 10.6.2.2.10: messageTypeSpecific "is transmitted with all bits of the field 0". Octets 16..19
    // being zero is also what fixes correctionField ending at 15 and sourcePortIdentity at 20.
    for (int i = 16; i < 20; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x00, buf[i]);
    }
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CLOCK_ID, buf + 20, 8);
    TEST_ASSERT_EQUAL_UINT16(0x0003, be16(buf + 28));
    TEST_ASSERT_EQUAL_UINT16(0x1234, be16(buf + 30));
    TEST_ASSERT_EQUAL_HEX8(0x05, buf[32]);
    // logMessageInterval is an Integer8 (10.6.2.2.14) in twos complement (6.4.2): -3 is 256-3 = 0xFD.
    TEST_ASSERT_EQUAL_HEX8(0xFD, buf[33]);
}

// Every header field survives a build/parse pair unchanged, including the extremes of each width.
void test_header_round_trip(void)
{
    protocore_ptp_header h;
    base_header(&h);
    h.message_type = PROTOCORE_PTP_DELAY_RESP;
    h.transport_specific = 0xF;
    h.domain = 17;
    h.flags = 0xBEEF;
    h.correction = -1;
    h.port_number = 0xFFFF;
    h.sequence_id = 0xFFFF;
    h.control = 0x03;
    h.log_interval = -128;

    uint8_t buf[PROTOCORE_PTP_HEADER_LEN];
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_PTP_HEADER_LEN, protocore_ptp_build_header(buf, sizeof(buf), &h, 20));

    protocore_ptp_header g;
    TEST_ASSERT_TRUE(protocore_ptp_parse_header(buf, sizeof(buf), &g));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PTP_DELAY_RESP, g.message_type);
    TEST_ASSERT_EQUAL_UINT8(0xF, g.transport_specific);
    TEST_ASSERT_EQUAL_UINT8(2, g.version);
    TEST_ASSERT_EQUAL_UINT16(54, g.message_length); // 34 + 20
    TEST_ASSERT_EQUAL_UINT8(17, g.domain);
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, g.flags);
    TEST_ASSERT_EQUAL_INT64(-1, g.correction);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CLOCK_ID, g.clock_identity, 8);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, g.port_number);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, g.sequence_id);
    TEST_ASSERT_EQUAL_HEX8(0x03, g.control);
    TEST_ASSERT_EQUAL_INT8(-128, g.log_interval);
}

// 802.1AS-2020 Table 11-5 (Values for messageType field):
//   Sync 0x0, Pdelay_Req 0x2, Pdelay_Resp 0x3, Follow_Up 0x8, Pdelay_Resp_Follow_Up 0xA.
// Table 10-8 adds Announce 0xB.
void test_message_type_values(void)
{
    TEST_ASSERT_EQUAL_UINT8(0x0, PROTOCORE_PTP_SYNC);
    TEST_ASSERT_EQUAL_UINT8(0x2, PROTOCORE_PTP_PDELAY_REQ);
    TEST_ASSERT_EQUAL_UINT8(0x3, PROTOCORE_PTP_PDELAY_RESP);
    TEST_ASSERT_EQUAL_UINT8(0x8, PROTOCORE_PTP_FOLLOW_UP);
    TEST_ASSERT_EQUAL_UINT8(0xA, PROTOCORE_PTP_PDELAY_RESP_FOLLOW_UP);
    TEST_ASSERT_EQUAL_UINT8(0xB, PROTOCORE_PTP_ANNOUNCE);
}

// 11.4.2.2: "The most significant bit of the message ID field divides this field in half between
// event and general messages, i.e., it is 0 for event messages and 1 for general messages."
// Table 10-10 classes Delay_Req as an Event message; Delay_Resp answers it and so is General, as
// every reply in the request-response pair is. 802.1AS publishes no number for either, so this
// class bit is all that is asserted of them.
void test_event_and_general_message_classes(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, PROTOCORE_PTP_SYNC & 0x8);
    TEST_ASSERT_EQUAL_UINT8(0, PROTOCORE_PTP_DELAY_REQ & 0x8);
    TEST_ASSERT_EQUAL_UINT8(0, PROTOCORE_PTP_PDELAY_REQ & 0x8);
    TEST_ASSERT_EQUAL_UINT8(0, PROTOCORE_PTP_PDELAY_RESP & 0x8);
    TEST_ASSERT_EQUAL_UINT8(8, PROTOCORE_PTP_FOLLOW_UP & 0x8);
    TEST_ASSERT_EQUAL_UINT8(8, PROTOCORE_PTP_DELAY_RESP & 0x8);
    TEST_ASSERT_EQUAL_UINT8(8, PROTOCORE_PTP_PDELAY_RESP_FOLLOW_UP & 0x8);
    TEST_ASSERT_EQUAL_UINT8(8, PROTOCORE_PTP_ANNOUNCE & 0x8);

    // No two enumerators share a number, so a parsed low nibble names one message.
    static const uint8_t ALL[8] = {PROTOCORE_PTP_SYNC,
                                   PROTOCORE_PTP_DELAY_REQ,
                                   PROTOCORE_PTP_PDELAY_REQ,
                                   PROTOCORE_PTP_PDELAY_RESP,
                                   PROTOCORE_PTP_FOLLOW_UP,
                                   PROTOCORE_PTP_DELAY_RESP,
                                   PROTOCORE_PTP_PDELAY_RESP_FOLLOW_UP,
                                   PROTOCORE_PTP_ANNOUNCE};
    for (size_t i = 0; i < 8; i++)
    {
        TEST_ASSERT_TRUE(ALL[i] < 0x10);
        for (size_t j = i + 1; j < 8; j++)
        {
            TEST_ASSERT_NOT_EQUAL_UINT8(ALL[i], ALL[j]);
        }
    }
}

// Message sizes summed from the octet columns of the 802.1AS-2020 tables, and 10.6.2.2.5, which
// makes messageLength the total octet count of the message:
//   Table 11-8  Sync (twoStep)          34 header + 10 = 44
//   Table 11-10 Follow_Up               34 header + 10 preciseOriginTimestamp = 44 fixed fields
//   Table 11-12 Pdelay_Req              34 + 10 + 10 = 54
//   Table 11-13 Pdelay_Resp             34 + 10 requestReceiptTimestamp + 10 PortIdentity = 54
//   Table 11-14 Pdelay_Resp_Follow_Up   34 + 10 responseOriginTimestamp + 10 PortIdentity = 54
//   Table 10-11 Announce                34 + 10 + 2 + 1 + 1 + 4 + 1 + 8 + 2 + 1 = 64
// Delay_Req and Delay_Resp are not in 802.1AS. Their lengths are asserted only as equal to the
// same-shaped messages above: Delay_Req carries one Timestamp like Sync, Delay_Resp carries a
// Timestamp plus a PortIdentity like Pdelay_Resp.
void test_message_lengths(void)
{
    protocore_ptp_header h;
    base_header(&h);
    protocore_ptp_timestamp ts = {12, 500};
    protocore_ptp_announce a;
    memset(&a, 0, sizeof(a));
    a.origin = ts;
    uint8_t buf[128];

    TEST_ASSERT_EQUAL_size_t(44, protocore_ptp_build_sync(buf, sizeof(buf), &h, &ts));
    TEST_ASSERT_EQUAL_UINT16(44, be16(buf + 2));

    TEST_ASSERT_EQUAL_size_t(44, protocore_ptp_build_follow_up(buf, sizeof(buf), &h, &ts));
    TEST_ASSERT_EQUAL_UINT16(44, be16(buf + 2));

    TEST_ASSERT_EQUAL_size_t(54, protocore_ptp_build_pdelay_req(buf, sizeof(buf), &h, &ts));
    TEST_ASSERT_EQUAL_UINT16(54, be16(buf + 2));

    TEST_ASSERT_EQUAL_size_t(54, protocore_ptp_build_pdelay_resp(buf, sizeof(buf), &h, &ts, REQ_ID, 7));
    TEST_ASSERT_EQUAL_UINT16(54, be16(buf + 2));

    TEST_ASSERT_EQUAL_size_t(54, protocore_ptp_build_pdelay_resp_follow_up(buf, sizeof(buf), &h, &ts, REQ_ID, 7));
    TEST_ASSERT_EQUAL_UINT16(54, be16(buf + 2));

    TEST_ASSERT_EQUAL_size_t(64, protocore_ptp_build_announce(buf, sizeof(buf), &h, &a));
    TEST_ASSERT_EQUAL_UINT16(64, be16(buf + 2));

    const size_t sync_len = protocore_ptp_build_sync(buf, sizeof(buf), &h, &ts);
    TEST_ASSERT_EQUAL_size_t(sync_len, protocore_ptp_build_delay_req(buf, sizeof(buf), &h, &ts));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)sync_len, be16(buf + 2));

    const size_t presp_len = protocore_ptp_build_pdelay_resp(buf, sizeof(buf), &h, &ts, REQ_ID, 7);
    TEST_ASSERT_EQUAL_size_t(presp_len, protocore_ptp_build_delay_resp(buf, sizeof(buf), &h, &ts, REQ_ID, 7));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)presp_len, be16(buf + 2));
}

// Table 11-8 puts the message's one Timestamp at offset 34, straight after the header, and
// Table 11-10 does the same for Follow_Up. The value written there is the value read back.
void test_timestamp_message_build_and_parse(void)
{
    protocore_ptp_header h;
    base_header(&h);
    protocore_ptp_timestamp ts = {0x0000000012345678ULL, 987654321U};
    uint8_t buf[64];

    struct
    {
        size_t (*build)(uint8_t *, size_t, const protocore_ptp_header *, const protocore_ptp_timestamp *);
        uint8_t type;
    } static const CASES[] = {
        {protocore_ptp_build_sync, PROTOCORE_PTP_SYNC},
        {protocore_ptp_build_delay_req, PROTOCORE_PTP_DELAY_REQ},
        {protocore_ptp_build_follow_up, PROTOCORE_PTP_FOLLOW_UP},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        size_t n = CASES[i].build(buf, sizeof(buf), &h, &ts);
        TEST_ASSERT_EQUAL_size_t(44, n);
        TEST_ASSERT_EQUAL_UINT8(CASES[i].type, buf[0] & 0x0F);

        protocore_ptp_timestamp on_wire;
        protocore_ptp_ts_read(buf + PROTOCORE_PTP_HEADER_LEN, &on_wire);
        TEST_ASSERT_EQUAL_UINT64(ts.seconds, on_wire.seconds);
        TEST_ASSERT_EQUAL_UINT32(ts.nanoseconds, on_wire.nanoseconds);

        protocore_ptp_header g;
        protocore_ptp_timestamp got;
        TEST_ASSERT_TRUE(protocore_ptp_parse_timestamp_msg(buf, n, &g, &got));
        TEST_ASSERT_EQUAL_UINT8(CASES[i].type, g.message_type);
        TEST_ASSERT_EQUAL_UINT64(ts.seconds, got.seconds);
        TEST_ASSERT_EQUAL_UINT32(ts.nanoseconds, got.nanoseconds);
    }

    // An Announce is not one of the three, so the single-timestamp parse must refuse it.
    protocore_ptp_announce a;
    memset(&a, 0, sizeof(a));
    size_t n = protocore_ptp_build_announce(buf, sizeof(buf), &h, &a);
    protocore_ptp_header g;
    protocore_ptp_timestamp got;
    TEST_ASSERT_FALSE(protocore_ptp_parse_timestamp_msg(buf, n, &g, &got));
}

// IEEE 1588-2008 Table 30, the Delay_Resp body, could not be obtained. What is asserted is the
// PortIdentity structure of 802.1AS-2020 6.4.3.7 (ClockIdentity Octet8, then UInteger16 portNumber,
// first member first and no padding per 6.4.4.5) sitting after a 10-octet Timestamp, which is
// exactly the pair Table 11-13 publishes at offsets 34 and 44 for Pdelay_Resp, plus a round trip.
void test_delay_resp_body(void)
{
    protocore_ptp_header h;
    base_header(&h);
    protocore_ptp_timestamp t4 = {1000, 250000000U};
    uint8_t buf[64];
    size_t n = protocore_ptp_build_delay_resp(buf, sizeof(buf), &h, &t4, REQ_ID, 0x0007);
    TEST_ASSERT_EQUAL_size_t(54, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(REQ_ID, buf + 44, 8);
    TEST_ASSERT_EQUAL_UINT16(0x0007, be16(buf + 52));

    protocore_ptp_header g;
    protocore_ptp_delay_resp out;
    TEST_ASSERT_TRUE(protocore_ptp_parse_delay_resp(buf, n, &g, &out));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PTP_DELAY_RESP, g.message_type);
    TEST_ASSERT_EQUAL_UINT64(1000, out.receive.seconds);
    TEST_ASSERT_EQUAL_UINT32(250000000U, out.receive.nanoseconds);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(REQ_ID, out.req_clock_id, 8);
    TEST_ASSERT_EQUAL_UINT16(0x0007, out.req_port);

    protocore_ptp_timestamp ts = {0, 0};
    size_t sn = protocore_ptp_build_sync(buf, sizeof(buf), &h, &ts);
    TEST_ASSERT_FALSE(protocore_ptp_parse_delay_resp(buf, sn, &g, &out));
}

// Table 11-12: a Pdelay_Req is header(34) + reserved(10) at offset 34 + reserved(10) at offset 44.
// 11.4.1: "Reserved fields shall be transmitted with all bits of the field 0".
// Table 11-13 and Table 11-14 put requestReceiptTimestamp / responseOriginTimestamp at offset 34
// and requestingPortIdentity at offset 44, and 11.4.6.2.2 / 11.4.7.2.2 make that identity the
// sourcePortIdentity of the Pdelay_Req being answered.
void test_peer_delay_messages(void)
{
    protocore_ptp_header h;
    base_header(&h);
    uint8_t buf[64];

    protocore_ptp_timestamp origin = {0, 0};
    size_t n = protocore_ptp_build_pdelay_req(buf, sizeof(buf), &h, &origin);
    TEST_ASSERT_EQUAL_size_t(54, n);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PTP_PDELAY_REQ, buf[0] & 0x0F);
    for (size_t i = 44; i < 54; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x00, buf[i]);
    }
    protocore_ptp_header g;
    protocore_ptp_timestamp got;
    TEST_ASSERT_TRUE(protocore_ptp_parse_pdelay_req(buf, n, &g, &got));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PTP_PDELAY_REQ, g.message_type);

    protocore_ptp_timestamp t2 = {5, 6};
    n = protocore_ptp_build_pdelay_resp(buf, sizeof(buf), &h, &t2, REQ_ID, 0x0042);
    TEST_ASSERT_EQUAL_size_t(54, n);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PTP_PDELAY_RESP, buf[0] & 0x0F);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(REQ_ID, buf + 44, 8);
    TEST_ASSERT_EQUAL_UINT16(0x0042, be16(buf + 52));
    protocore_ptp_pdelay_resp pr;
    TEST_ASSERT_TRUE(protocore_ptp_parse_pdelay_resp(buf, n, &g, &pr));
    TEST_ASSERT_EQUAL_UINT64(5, pr.timestamp.seconds);
    TEST_ASSERT_EQUAL_UINT32(6, pr.timestamp.nanoseconds);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(REQ_ID, pr.req_clock_id, 8);
    TEST_ASSERT_EQUAL_UINT16(0x0042, pr.req_port);

    // 0x3 and 0xA are different messages, so neither parse accepts the other's frame.
    TEST_ASSERT_FALSE(protocore_ptp_parse_pdelay_resp_follow_up(buf, n, &g, &pr));

    protocore_ptp_timestamp t3 = {7, 8};
    n = protocore_ptp_build_pdelay_resp_follow_up(buf, sizeof(buf), &h, &t3, REQ_ID, 0x0042);
    TEST_ASSERT_EQUAL_size_t(54, n);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PTP_PDELAY_RESP_FOLLOW_UP, buf[0] & 0x0F);
    TEST_ASSERT_TRUE(protocore_ptp_parse_pdelay_resp_follow_up(buf, n, &g, &pr));
    TEST_ASSERT_EQUAL_UINT64(7, pr.timestamp.seconds);
    TEST_ASSERT_EQUAL_UINT32(8, pr.timestamp.nanoseconds);
    TEST_ASSERT_FALSE(protocore_ptp_parse_pdelay_resp(buf, n, &g, &pr));
}

// FAILING, and the failure is the finding.
// 802.1AS-2020 Table 11-12 fixes the Pdelay_Req message at header(34) + reserved(10) at 34 +
// reserved(10) at 44, so the message is 54 octets and a 53-octet frame is truncated. ptp.h says
// protocore_ptp_parse_pdelay_req returns "False on a short / wrong-type frame", and
// protocore_ptp_build_pdelay_req refuses a capacity below 54, but ptp.c line 358 tests only
// len < PROTOCORE_PTP_HEADER_LEN + PROTOCORE_PTP_TS_LEN, i.e. 44, so it accepts a frame missing up
// to the whole second reserved field. Its two siblings, parse_pdelay_resp and
// parse_pdelay_resp_follow_up, require the full 54 for the identically sized message.
void test_pdelay_req_frame_is_fifty_four_octets(void)
{
    protocore_ptp_header h;
    base_header(&h);
    protocore_ptp_timestamp origin = {0, 0};
    uint8_t buf[64];
    size_t n = protocore_ptp_build_pdelay_req(buf, sizeof(buf), &h, &origin);
    TEST_ASSERT_EQUAL_size_t(54, n);

    protocore_ptp_header g;
    protocore_ptp_timestamp got;
    TEST_ASSERT_TRUE(protocore_ptp_parse_pdelay_req(buf, 54, &g, &got));
    for (size_t len = 0; len < 54; len++)
    {
        TEST_ASSERT_FALSE(protocore_ptp_parse_pdelay_req(buf, len, &g, &got));
    }
}

// 802.1AS-2020 Table 10-11 (Announce message fields), octets and offset:
//   header                  34  0
//   reserved                10  34
//   currentUtcOffset        2   44
//   reserved                1   46
//   grandmasterPriority1    1   47
//   grandmasterClockQuality 4   48
//   grandmasterPriority2    1   52
//   grandmasterIdentity     8   53
//   stepsRemoved            2   61
//   timeSource              1   63
// grandmasterClockQuality is a ClockQuality (6.4.3.8): UInteger8 clockClass, Enumeration8
// clockAccuracy, UInteger16 offsetScaledLogVariance, so 48, 49, and 50..51.
// The gPTP profile reserves octets 34..43; IEEE 1588-2008 places originTimestamp there and that is
// what the module writes, so those ten octets are only round-tripped here, not pinned to a value.
void test_announce_body_offsets(void)
{
    protocore_ptp_header h;
    base_header(&h);
    protocore_ptp_announce a;
    memset(&a, 0, sizeof(a));
    a.origin.seconds = 0x0000AABBCCDDULL;
    a.origin.nanoseconds = 0x11223344U;
    a.utc_offset = 37;
    a.gm_priority1 = 128;
    a.gm_clock_class = 6;
    a.gm_clock_accuracy = 0x21;
    a.gm_variance = 0x436A;
    a.gm_priority2 = 128;
    memcpy(a.gm_identity, REQ_ID, 8);
    a.steps_removed = 3;
    a.time_source = 0x20;

    uint8_t buf[80];
    size_t n = protocore_ptp_build_announce(buf, sizeof(buf), &h, &a);
    TEST_ASSERT_EQUAL_size_t(64, n);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PTP_ANNOUNCE, buf[0] & 0x0F);
    TEST_ASSERT_EQUAL_UINT16(37, be16(buf + 44));
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[46]);
    TEST_ASSERT_EQUAL_HEX8(128, buf[47]);
    TEST_ASSERT_EQUAL_HEX8(6, buf[48]);
    TEST_ASSERT_EQUAL_HEX8(0x21, buf[49]);
    TEST_ASSERT_EQUAL_UINT16(0x436A, be16(buf + 50));
    TEST_ASSERT_EQUAL_HEX8(128, buf[52]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(REQ_ID, buf + 53, 8);
    TEST_ASSERT_EQUAL_UINT16(3, be16(buf + 61));
    TEST_ASSERT_EQUAL_HEX8(0x20, buf[63]);

    protocore_ptp_header g;
    protocore_ptp_announce got;
    TEST_ASSERT_TRUE(protocore_ptp_parse_announce(buf, n, &g, &got));
    TEST_ASSERT_EQUAL_UINT64(0x0000AABBCCDDULL, got.origin.seconds);
    TEST_ASSERT_EQUAL_UINT32(0x11223344U, got.origin.nanoseconds);
    TEST_ASSERT_EQUAL_INT16(37, got.utc_offset);
    TEST_ASSERT_EQUAL_UINT8(128, got.gm_priority1);
    TEST_ASSERT_EQUAL_UINT8(6, got.gm_clock_class);
    TEST_ASSERT_EQUAL_UINT8(0x21, got.gm_clock_accuracy);
    TEST_ASSERT_EQUAL_UINT16(0x436A, got.gm_variance);
    TEST_ASSERT_EQUAL_UINT8(128, got.gm_priority2);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(REQ_ID, got.gm_identity, 8);
    TEST_ASSERT_EQUAL_UINT16(3, got.steps_removed);
    TEST_ASSERT_EQUAL_UINT8(0x20, got.time_source);
}

// 10.6.3.2.1 types currentUtcOffset as Integer16, and 6.4.2 puts signed integers in twos complement,
// so -1 is 2^16 - 1 = 0xFFFF over the two octets at offset 44.
void test_announce_utc_offset_is_signed(void)
{
    protocore_ptp_header h;
    base_header(&h);
    protocore_ptp_announce a;
    memset(&a, 0, sizeof(a));
    a.utc_offset = -1;
    uint8_t buf[80];
    size_t n = protocore_ptp_build_announce(buf, sizeof(buf), &h, &a);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[44]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[45]);

    protocore_ptp_header g;
    protocore_ptp_announce got;
    TEST_ASSERT_TRUE(protocore_ptp_parse_announce(buf, n, &g, &got));
    TEST_ASSERT_EQUAL_INT16(-1, got.utc_offset);
}

// 10.6.2.2.4: "For transmitted messages, the value shall be 2". Octet 1 low nibble, Table 10-7.
void test_builders_stamp_version_two(void)
{
    protocore_ptp_header h;
    memset(&h, 0, sizeof(h));
    protocore_ptp_timestamp ts = {1, 2};
    protocore_ptp_announce a;
    memset(&a, 0, sizeof(a));
    uint8_t buf[80];

    TEST_ASSERT_EQUAL_size_t(44, protocore_ptp_build_sync(buf, sizeof(buf), &h, &ts));
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[1] & 0x0F);
    TEST_ASSERT_EQUAL_size_t(44, protocore_ptp_build_delay_req(buf, sizeof(buf), &h, &ts));
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[1] & 0x0F);
    TEST_ASSERT_EQUAL_size_t(44, protocore_ptp_build_follow_up(buf, sizeof(buf), &h, &ts));
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[1] & 0x0F);
    TEST_ASSERT_EQUAL_size_t(54, protocore_ptp_build_delay_resp(buf, sizeof(buf), &h, &ts, REQ_ID, 1));
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[1] & 0x0F);
    TEST_ASSERT_EQUAL_size_t(54, protocore_ptp_build_pdelay_req(buf, sizeof(buf), &h, &ts));
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[1] & 0x0F);
    TEST_ASSERT_EQUAL_size_t(54, protocore_ptp_build_pdelay_resp(buf, sizeof(buf), &h, &ts, REQ_ID, 1));
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[1] & 0x0F);
    TEST_ASSERT_EQUAL_size_t(54, protocore_ptp_build_pdelay_resp_follow_up(buf, sizeof(buf), &h, &ts, REQ_ID, 1));
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[1] & 0x0F);
    TEST_ASSERT_EQUAL_size_t(64, protocore_ptp_build_announce(buf, sizeof(buf), &h, &a));
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[1] & 0x0F);
}

// RFC 5905 sec 8 (docs/learn/rfc/text/rfc5905.txt lines 1597-1604) publishes the two-way exchange
// equations, for A the requester and B the responder, T1 = A transmit, T2 = B receive, T3 = B
// transmit, T4 = A receive:
//     theta = T(B) - T(A) = 1/2 * [(T2-T1) + (T3-T4)]
//     delta = T(ABA)      = (T4-T1) - (T3-T2)
// PTP names the same four instants t1 = Sync egress at the master, t2 = Sync ingress at the slave,
// t3 = Delay_Req egress at the slave, t4 = Delay_Req ingress at the master. Taking the slave as A
// and the master as B, the Delay_Req is the A-to-B leg and the Sync is the B-to-A leg:
//     T1 = t3, T2 = t4, T3 = t1, T4 = t2
// Substituting,
//     theta = 1/2 * [(t4-t3) + (t1-t2)] = -1/2 * [(t2-t1) - (t4-t3)]
//     delta =        (t2-t3) - (t1-t4)  =        (t2-t1) + (t4-t3)
// theta is master minus slave, so offsetFromMaster, which is slave minus master, is -theta:
//     offsetFromMaster = ((t2-t1) - (t4-t3)) / 2
// and meanPathDelay is the one-way half of the round trip:
//     meanPathDelay    = ((t2-t1) + (t4-t3)) / 2
// Each case below builds t2 and t4 from a chosen delay d and offset o, so the two results are known
// before the call: t2 = t1 + d + o and t4 = t3 + d - o give (t2-t1) = d+o and (t4-t3) = d-o, hence
// offset = ((d+o) - (d-o))/2 = o and delay = ((d+o) + (d-o))/2 = d.
void test_offset_and_delay_from_the_four_timestamps(void)
{
    struct
    {
        int64_t t1, d, o;
    } static const CASES[] = {
        {1000000000LL, 5000LL, 1234000LL},
        {1000000000LL, 5000LL, -1234000LL},
        {0LL, 5000LL, 0LL},
        {0LL, 0LL, 7LL},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        const int64_t t1 = CASES[i].t1;
        const int64_t t2 = t1 + CASES[i].d + CASES[i].o;
        const int64_t t3 = t1 + 2000000LL;
        const int64_t t4 = t3 + CASES[i].d - CASES[i].o;
        protocore_ptp_sync s;
        protocore_ptp_compute(t1, t2, t3, t4, &s);
        TEST_ASSERT_EQUAL_INT64(CASES[i].o, s.offset_ns);
        TEST_ASSERT_EQUAL_INT64(CASES[i].d, s.delay_ns);
    }
}

// The same two equations on four literal instants, so the arithmetic is visible without the loop:
//   t1 = 1000000000, t2 = 1001239000, t3 = 1002000000, t4 = 1000771000
//   (t2-t1) =  1239000
//   (t4-t3) = -1229000
//   offset  = ( 1239000 - (-1229000)) / 2 = 2468000 / 2 = 1234000
//   delay   = ( 1239000 + (-1229000)) / 2 =   10000 / 2 =    5000
void test_offset_and_delay_worked_example(void)
{
    protocore_ptp_sync s;
    protocore_ptp_compute(1000000000LL, 1001239000LL, 1002000000LL, 1000771000LL, &s);
    TEST_ASSERT_EQUAL_INT64(1234000LL, s.offset_ns);
    TEST_ASSERT_EQUAL_INT64(5000LL, s.delay_ns);
}

// 802.1AS-2020 11.1.2, Equation (11-1), for t1 = Pdelay_Req egress, t2 = its ingress at the peer,
// t3 = Pdelay_Resp egress at the peer, t4 = its ingress:
//     D = ((t4 - t1) - (t3 - t2)) / 2
// With t2 = t1 + d + o and t4 = t3 + d - o for any peer offset o:
//     (t4 - t1) = t3 + d - o - t1
//     (t3 - t2) = t3 - t1 - d - o
//     difference = 2d, so D = d whatever o is.
void test_peer_link_delay_is_independent_of_the_peer_offset(void)
{
    const int64_t d = 7500LL;
    const int64_t t1 = 0LL;
    const int64_t t3 = 3500000LL;

    const int64_t o1 = 3000000LL;
    TEST_ASSERT_EQUAL_INT64(d, protocore_ptp_compute_link_delay(t1, t1 + d + o1, t3, t3 + d - o1));

    const int64_t o2 = -12345678LL;
    TEST_ASSERT_EQUAL_INT64(d, protocore_ptp_compute_link_delay(t1, t1 + d + o2, t3, t3 + d - o2));

    TEST_ASSERT_EQUAL_INT64(d, protocore_ptp_compute_link_delay(t1, t1 + d, t3, t3 + d));
}

// A buffer one octet short of the message the table fixes cannot hold it, in either direction, and
// a null pointer is not a buffer.
void test_short_buffers_are_refused(void)
{
    protocore_ptp_header h;
    base_header(&h);
    protocore_ptp_timestamp ts = {1, 2};
    protocore_ptp_announce a;
    memset(&a, 0, sizeof(a));
    uint8_t buf[80];

    TEST_ASSERT_EQUAL_size_t(0, protocore_ptp_build_header(buf, 33, &h, 0));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ptp_build_header(NULL, sizeof(buf), &h, 0));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ptp_build_header(buf, sizeof(buf), NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ptp_build_sync(buf, 43, &h, &ts));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ptp_build_delay_req(buf, 43, &h, &ts));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ptp_build_follow_up(buf, 43, &h, &ts));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ptp_build_delay_resp(buf, 53, &h, &ts, REQ_ID, 1));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ptp_build_delay_resp(buf, sizeof(buf), &h, &ts, NULL, 1));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ptp_build_pdelay_req(buf, 53, &h, &ts));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ptp_build_pdelay_resp(buf, 53, &h, &ts, REQ_ID, 1));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ptp_build_pdelay_resp_follow_up(buf, 53, &h, &ts, REQ_ID, 1));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ptp_build_announce(buf, 63, &h, &a));

    protocore_ptp_header g;
    protocore_ptp_timestamp got;
    protocore_ptp_delay_resp dr;
    protocore_ptp_announce an;
    protocore_ptp_pdelay_resp pr;

    TEST_ASSERT_FALSE(protocore_ptp_parse_header(buf, 33, &g));
    TEST_ASSERT_FALSE(protocore_ptp_parse_header(NULL, 34, &g));

    size_t n = protocore_ptp_build_sync(buf, sizeof(buf), &h, &ts);
    TEST_ASSERT_FALSE(protocore_ptp_parse_timestamp_msg(buf, n - 1, &g, &got));

    n = protocore_ptp_build_delay_resp(buf, sizeof(buf), &h, &ts, REQ_ID, 1);
    TEST_ASSERT_FALSE(protocore_ptp_parse_delay_resp(buf, n - 1, &g, &dr));

    n = protocore_ptp_build_announce(buf, sizeof(buf), &h, &a);
    TEST_ASSERT_FALSE(protocore_ptp_parse_announce(buf, n - 1, &g, &an));

    n = protocore_ptp_build_pdelay_resp(buf, sizeof(buf), &h, &ts, REQ_ID, 1);
    TEST_ASSERT_FALSE(protocore_ptp_parse_pdelay_resp(buf, n - 1, &g, &pr));

    n = protocore_ptp_build_pdelay_resp_follow_up(buf, sizeof(buf), &h, &ts, REQ_ID, 1);
    TEST_ASSERT_FALSE(protocore_ptp_parse_pdelay_resp_follow_up(buf, n - 1, &g, &pr));
}

// IANA Service Name and Transport Protocol Port Number Registry, entries registered 2010-07-27:
//   ptp-event   319 udp  PTP Event
//   ptp-general 320 udp  PTP General
void test_transport_ports(void)
{
    TEST_ASSERT_EQUAL_UINT16(319, PROTOCORE_PTP_EVENT_PORT);
    TEST_ASSERT_EQUAL_UINT16(320, PROTOCORE_PTP_GENERAL_PORT);
}
