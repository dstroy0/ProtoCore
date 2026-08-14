// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the DDSI-RTPS Message framing codec (services/iot/dds/dds.h).
//
// Governing document: OMG "The Real-time Publish-Subscribe Protocol DDS Interoperability Wire
// Protocol (DDSI-RTPS) Specification" version 2.5, OMG document formal/2022-04-01, read from
// https://www.omg.org/spec/DDSI-RTPS/2.5/PDF. Sections 8.3.4.1 (receiver rules), 8.3.6.3 (Header
// validity), 9.3.1.1 / 9.3.2.1 (type mappings), 9.4.4 (Header wire form) and 9.4.5.1
// (SubmessageHeader wire form, the SubmessageKind enum, the EndiannessFlag, octetsToNextHeader).
//
// The load-bearing cases are test_header_matches_the_published_layout, whose 20 octets come from
// 9.4.4's field diagram plus the four typedefs that give each field its width, and
// test_submessage_kinds_match_the_published_enum, which reproduces 9.4.5.1's SubmessageKind
// @value list verbatim.
//
// Two cases assert the document against the codec and are expected to FAIL.
// test_any_minor_protocol_version_is_valid rests on 8.3.6.3, which makes a Header invalid on
// exactly three conditions - too few octets, a protocol other than PROTOCOL_RTPS, and a MAJOR
// version larger than the implementation's - and says nothing at all about the minor version.
// test_a_partial_submessage_header_invalidates_the_rest rests on 8.3.4.1 rule 1, "If the full
// Submessage header cannot be read, the rest of the Message is considered invalid", which is the
// same verdict rule 2 spells with the same words and which this suite asserts for rule 2 in
// test_contents_past_the_end_are_refused.
//
// 8.3.6.3 is silent about a major version SMALLER than the implementation's, so no case asserts
// one. No case asserts a value for RTPS_VERSION[1] either - octet 5 is only checked to carry it,
// which fixes the field's offset and not its content - because the published document contradicts
// itself about the minor an RTPS 2.5 implementation stamps: sec 8.3.3.1.2 reads "Implementations
// following this version of the document implement protocol version 2.5 (major = 2, minor = 5)"
// while sec 8.2.1.2 Table 8.2 still reserves only up to PROTOCOLVERSION_2_4 and calls it "the most
// recent version".

#include "services/iot/dds/dds.h"

#include <string.h>
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static const uint8_t GUID[RTPS_GUIDPREFIX_LEN] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                                                  0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c};

static const uint8_t VENDOR[2] = {0x01, 0x0f};

typedef struct
{
    uint8_t id;
    uint8_t flags;
    const uint8_t *contents;
    size_t contents_len;
} Seen;

static Seen g_seen[8];
static size_t g_seen_n;
static void *g_seen_arg;

static void on_submessage(uint8_t id, uint8_t flags, const uint8_t *contents, size_t len, void *arg)
{
    g_seen_arg = arg;
    if (g_seen_n < sizeof(g_seen) / sizeof(g_seen[0]))
    {
        g_seen[g_seen_n].id = id;
        g_seen[g_seen_n].flags = flags;
        g_seen[g_seen_n].contents = contents;
        g_seen[g_seen_n].contents_len = len;
        g_seen_n++;
    }
}

static size_t build_header(uint8_t *buf, size_t cap)
{
    Rtps.hdr.guid_prefix = GUID;
    Rtps.hdr.vendor_id = VENDOR;
    Rtps.out.buf = buf;
    Rtps.out.cap = cap;
    Rtps.header(Rtps.internal);
    return Rtps.n;
}

static size_t build_submessage(uint8_t *buf, size_t cap, uint8_t id, uint8_t flags, const uint8_t *contents,
                               uint16_t len)
{
    Rtps.sub.submessage_id = id;
    Rtps.sub.flags = flags;
    Rtps.sub.contents = contents;
    Rtps.sub.contents_len = len;
    Rtps.out.buf = buf;
    Rtps.out.cap = cap;
    Rtps.submessage(Rtps.internal);
    return Rtps.n;
}

static proto_bool walk(const uint8_t *msg, size_t len)
{
    g_seen_n = 0;
    g_seen_arg = NULL;
    Rtps.msg.msg = msg;
    Rtps.msg.len = len;
    Rtps.sink.on_submessage = on_submessage;
    Rtps.sink.arg = g_seen;
    Rtps.parse(Rtps.internal);
    return Rtps.ok;
}

// sec 9.4.4 lays the Header out as
//     | 'R' | 'T' | 'P' | 'S' |
//     | ProtocolVersion version | VendorId vendorId |
//     | GuidPrefix guidPrefix (three rows of 4) |
// and the four typedefs fix the widths: sec 9.3.2.1 "typedef octet ProtocolId_t[4]" with
// ProtocolId_t[0..3] = 'R','T','P','S', "struct ProtocolVersion_t { octet major; octet minor; }",
// "typedef octet VendorId_t[2]", sec 9.3.1.1 "typedef octet GuidPrefix_t[12]".
//   4 + 2 + 2 + 12 = 20 octets.
// sec 9.4.4: "The structure of the Header cannot change in this major version (2) of the
// protocol", and sec 8.3.3.1.2 gives that major as 2.
void test_header_matches_the_published_layout(void)
{
    uint8_t buf[32];
    memset(buf, 0xAA, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT(20u, build_header(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT(20u, (unsigned)RTPS_HEADER_LEN);
    TEST_ASSERT_EQUAL_UINT(12u, (unsigned)RTPS_GUIDPREFIX_LEN);

    TEST_ASSERT_EQUAL_HEX8('R', buf[0]);
    TEST_ASSERT_EQUAL_HEX8('T', buf[1]);
    TEST_ASSERT_EQUAL_HEX8('P', buf[2]);
    TEST_ASSERT_EQUAL_HEX8('S', buf[3]);

    TEST_ASSERT_EQUAL_UINT8(2, RTPS_VERSION[0]);
    TEST_ASSERT_EQUAL_HEX8(RTPS_VERSION[0], buf[4]);
    TEST_ASSERT_EQUAL_HEX8(RTPS_VERSION[1], buf[5]);

    TEST_ASSERT_EQUAL_HEX8(VENDOR[0], buf[6]);
    TEST_ASSERT_EQUAL_HEX8(VENDOR[1], buf[7]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(GUID, buf + 8, RTPS_GUIDPREFIX_LEN);

    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[20]);
}

// A build handed less room than the 20 octets sec 9.4.4 fixes writes nothing: a Header short of
// its published width is a Message sec 8.3.6.3 makes invalid at the far end.
void test_header_refuses_a_short_buffer(void)
{
    uint8_t buf[RTPS_HEADER_LEN];
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT(0u, build_header(buf, RTPS_HEADER_LEN - 1));
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[0]);
    TEST_ASSERT_EQUAL_UINT(20u, build_header(buf, RTPS_HEADER_LEN));

    TEST_ASSERT_EQUAL_UINT(0u, build_header(NULL, RTPS_HEADER_LEN));
}

// sec 9.4.5.1 maps the SubmessageHeader to
//     struct SubmessageHeader { octet submessageId; octet flags; unsigned short submessageLength; }
//   1 + 1 + 2 = 4 octets, then "following are the contents of Submessage".
// sec 9.4.5.1 gives DATA the value 0x15 and E = flags & 0x01 with E=1 little-endian, so a 6-octet
// body writes octetsToNextHeader as 0x06 0x00.
void test_submessage_header_is_four_octets_then_contents(void)
{
    static const uint8_t BODY[6] = {0xde, 0xad, 0xbe, 0xef, 0x00, 0x11};
    uint8_t buf[32];
    memset(buf, 0xAA, sizeof(buf));

    TEST_ASSERT_EQUAL_UINT(4u + 6u, build_submessage(buf, sizeof(buf), RTPS_SM_DATA, RTPS_FLAG_ENDIAN, BODY, 6));
    TEST_ASSERT_EQUAL_HEX8(0x15, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x06, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[3]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(BODY, buf + 4, 6);
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[10]);
}

// sec 9.4.5.1: "The PSM maps the EndiannessFlag flag into the least-significant bit (LSB) of the
// flags ... E=0 means big-endian, E=1 means little-endian", "E = SubmessageHeader.flags & 0x01",
// and "The representation of this field is a CDR unsigned short (ushort)".
//   0x0102 big-endian    -> 0x01 0x02
//   0x0102 little-endian -> 0x02 0x01
// Either spelling names the same 258 octets of contents to the walk.
void test_octets_to_next_header_follows_the_endianness_flag(void)
{
    static const uint8_t BODY[0x0102] = {0};
    uint8_t big[4 + 0x0102];
    uint8_t little[4 + 0x0102];
    TEST_ASSERT_EQUAL_HEX8(0x01, RTPS_FLAG_ENDIAN);

    TEST_ASSERT_EQUAL_UINT(4u + 0x0102u, build_submessage(big, sizeof(big), RTPS_SM_HEARTBEAT, 0x00, BODY, 0x0102));
    TEST_ASSERT_EQUAL_HEX8(0x01, big[2]);
    TEST_ASSERT_EQUAL_HEX8(0x02, big[3]);

    TEST_ASSERT_EQUAL_UINT(4u + 0x0102u,
                           build_submessage(little, sizeof(little), RTPS_SM_HEARTBEAT, RTPS_FLAG_ENDIAN, BODY, 0x0102));
    TEST_ASSERT_EQUAL_HEX8(0x02, little[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01, little[3]);

    uint8_t msg[4 + 0x0102 + RTPS_HEADER_LEN];
    size_t n = build_header(msg, sizeof(msg));
    n += build_submessage(msg + n, sizeof(msg) - n, RTPS_SM_HEARTBEAT, 0x00, BODY, 0x0102);
    TEST_ASSERT_TRUE(walk(msg, n));
    TEST_ASSERT_EQUAL_UINT(0x0102u, g_seen[0].contents_len);
    n = build_header(msg, sizeof(msg));
    n += build_submessage(msg + n, sizeof(msg) - n, RTPS_SM_HEARTBEAT, RTPS_FLAG_ENDIAN, BODY, 0x0102);
    TEST_ASSERT_TRUE(walk(msg, n));
    TEST_ASSERT_EQUAL_UINT(0x0102u, g_seen[0].contents_len);
}

// sec 8.3.3: a Message is a Header followed by a variable number of Submessages, and sec 8.3.4.1
// rule 5 says "A valid submessageLength field must always be used to find the next Submessage".
//   20 (Header) + (4 + 8) + (4 + 4) = 40 octets, two Submessages.
void test_parse_walks_every_submessage(void)
{
    static const uint8_t HB[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    static const uint8_t DATA[4] = {0xa0, 0xa1, 0xa2, 0xa3};
    uint8_t msg[64];
    size_t n = build_header(msg, sizeof(msg));
    n += build_submessage(msg + n, sizeof(msg) - n, RTPS_SM_HEARTBEAT, RTPS_FLAG_ENDIAN, HB, 8);
    n += build_submessage(msg + n, sizeof(msg) - n, RTPS_SM_DATA, 0x00, DATA, 4);
    TEST_ASSERT_EQUAL_UINT(20u + 12u + 8u, n);

    TEST_ASSERT_TRUE(walk(msg, n));
    TEST_ASSERT_EQUAL_UINT(2u, g_seen_n);
    TEST_ASSERT_EQUAL_HEX8(RTPS_SM_HEARTBEAT, g_seen[0].id);
    TEST_ASSERT_EQUAL_HEX8(RTPS_FLAG_ENDIAN, g_seen[0].flags);
    TEST_ASSERT_EQUAL_UINT(8u, g_seen[0].contents_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(HB, g_seen[0].contents, 8);
    TEST_ASSERT_EQUAL_HEX8(RTPS_SM_DATA, g_seen[1].id);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_seen[1].flags);
    TEST_ASSERT_EQUAL_UINT(4u, g_seen[1].contents_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DATA, g_seen[1].contents, 4);
    TEST_ASSERT_EQUAL_PTR(g_seen, g_seen_arg);
}

// sec 9.4.5.1: "In case octetsToNextHeader==0 and the kind of Submessage is NOT PAD or INFO_TS,
// the Submessage is the last Submessage in the Message and extends up to the end of the Message."
//   20 (Header) + 4 (SubmessageHeader) + 2 trailing octets -> contents run those 2.
void test_zero_octets_to_next_header_runs_to_the_end(void)
{
    uint8_t msg[32];
    size_t n = build_header(msg, sizeof(msg));
    msg[n + 0] = RTPS_SM_DATA;
    msg[n + 1] = 0x00;
    msg[n + 2] = 0x00;
    msg[n + 3] = 0x00;
    msg[n + 4] = 0x77;
    msg[n + 5] = 0x88;
    n += 6;

    TEST_ASSERT_TRUE(walk(msg, n));
    TEST_ASSERT_EQUAL_UINT(1u, g_seen_n);
    TEST_ASSERT_EQUAL_HEX8(RTPS_SM_DATA, g_seen[0].id);
    TEST_ASSERT_EQUAL_UINT(2u, g_seen[0].contents_len);
    TEST_ASSERT_EQUAL_HEX8(0x77, g_seen[0].contents[0]);
    TEST_ASSERT_EQUAL_HEX8(0x88, g_seen[0].contents[1]);
}

// sec 9.4.5.1: "In case the octetsToNextHeader==0 and the kind of Submessage is PAD or INFO_TS,
// the next Submessage header starts immediately after the current Submessage header OR the PAD or
// INFO_TS is the last Submessage in the Message."
//   PAD/INFO_TS header (4, zero contents) then GAP with octetsToNextHeader 2, big-endian 0x00 0x02.
void test_pad_and_info_ts_do_not_swallow_the_rest(void)
{
    static const uint8_t KIND[2] = {RTPS_SM_PAD, RTPS_SM_INFO_TS};
    for (size_t k = 0; k < 2; k++)
    {
        uint8_t msg[32];
        size_t n = build_header(msg, sizeof(msg));
        msg[n + 0] = KIND[k];
        msg[n + 1] = 0x00;
        msg[n + 2] = 0x00;
        msg[n + 3] = 0x00;
        msg[n + 4] = RTPS_SM_GAP;
        msg[n + 5] = 0x00;
        msg[n + 6] = 0x00;
        msg[n + 7] = 0x02;
        msg[n + 8] = 0xc0;
        msg[n + 9] = 0xde;
        n += 10;

        TEST_ASSERT_TRUE(walk(msg, n));
        TEST_ASSERT_EQUAL_UINT(2u, g_seen_n);
        TEST_ASSERT_EQUAL_HEX8(KIND[k], g_seen[0].id);
        TEST_ASSERT_EQUAL_UINT(0u, g_seen[0].contents_len);
        TEST_ASSERT_NULL(g_seen[0].contents);
        TEST_ASSERT_EQUAL_HEX8(RTPS_SM_GAP, g_seen[1].id);
        TEST_ASSERT_EQUAL_UINT(2u, g_seen[1].contents_len);
    }
}

// sec 8.3.6.3, second bullet: a Header is invalid when "Its protocol value does not match the
// value of PROTOCOL_RTPS", whose four octets sec 9.3.2.1 gives as 'R','T','P','S'. Flipping any
// one of them breaks the match.
void test_parse_refuses_a_foreign_protocol(void)
{
    uint8_t msg[RTPS_HEADER_LEN];
    for (size_t i = 0; i < 4; i++)
    {
        (void)build_header(msg, sizeof(msg));
        msg[i] = (uint8_t)(msg[i] ^ 0xFFu);
        TEST_ASSERT_FALSE(walk(msg, sizeof(msg)));
    }
    (void)build_header(msg, sizeof(msg));
    TEST_ASSERT_TRUE(walk(msg, sizeof(msg)));
}

// sec 8.3.6.3, first bullet: a Header is invalid when "The Message has less than the required
// number of octets to contain a full Header. The number required is defined by the PSM", which
// sec 9.4.4 makes 20. At exactly 20 the Message is a Header with no Submessage after it.
void test_parse_refuses_a_message_short_of_the_header(void)
{
    uint8_t msg[RTPS_HEADER_LEN];
    (void)build_header(msg, sizeof(msg));
    for (size_t len = 0; len < RTPS_HEADER_LEN; len++)
    {
        TEST_ASSERT_FALSE(walk(msg, len));
    }
    TEST_ASSERT_TRUE(walk(msg, RTPS_HEADER_LEN));
    TEST_ASSERT_EQUAL_UINT(0u, g_seen_n);
    TEST_ASSERT_FALSE(walk(NULL, RTPS_HEADER_LEN));
}

// sec 8.3.6.3, third bullet: a Header is invalid when "The major protocol version is larger than
// the major protocol version supported by the implementation."
void test_parse_refuses_a_larger_major_version(void)
{
    uint8_t msg[RTPS_HEADER_LEN];
    for (unsigned major = (unsigned)RTPS_VERSION[0] + 1u; major < 256u; major++)
    {
        (void)build_header(msg, sizeof(msg));
        msg[4] = (uint8_t)major;
        TEST_ASSERT_FALSE(walk(msg, sizeof(msg)));
    }
}

// sec 8.3.6.3 lists every condition that makes a Header invalid - too few octets, a protocol other
// than PROTOCOL_RTPS, a major version larger than the implementation's - and the minor version is
// not among them. sec 9.4.5.1 says the same from the other side: "Additional Submessages can be
// added in higher minor versions", which only works if a higher minor parses.
void test_any_minor_protocol_version_is_valid(void)
{
    uint8_t msg[RTPS_HEADER_LEN];
    for (unsigned minor = 0; minor < 256u; minor++)
    {
        (void)build_header(msg, sizeof(msg));
        msg[5] = (uint8_t)minor;
        TEST_ASSERT_TRUE(walk(msg, sizeof(msg)));
    }
}

// sec 8.3.4.1 rule 2: "The submessageLength field defines where the next Submessage starts or
// indicates that the Submessage extends to the end of the Message ... If this field is invalid,
// the rest of the Message is invalid."
//   20 (Header) + 4 (SubmessageHeader) + 2 present, against octetsToNextHeader 0x0010 = 16.
void test_contents_past_the_end_are_refused(void)
{
    uint8_t msg[32];
    size_t n = build_header(msg, sizeof(msg));
    msg[n + 0] = RTPS_SM_GAP;
    msg[n + 1] = 0x00;
    msg[n + 2] = 0x00;
    msg[n + 3] = 0x10;
    msg[n + 4] = 0x00;
    msg[n + 5] = 0x00;
    n += 6;
    TEST_ASSERT_FALSE(walk(msg, n));
}

// sec 8.3.4.1 rule 1: "If the full Submessage header cannot be read, the rest of the Message is
// considered invalid." A SubmessageHeader is the 4 octets of sec 9.4.5.1, so a Message that ends
// 1, 2 or 3 octets into one cannot read it. The same wording in rule 2 is the verdict
// test_contents_past_the_end_are_refused asserts.
void test_a_partial_submessage_header_invalidates_the_rest(void)
{
    static const uint8_t BODY[2] = {0x5a, 0xa5};
    for (size_t stub = 0; stub < 4u; stub++)
    {
        uint8_t msg[32];
        size_t n = build_header(msg, sizeof(msg));
        n += build_submessage(msg + n, sizeof(msg) - n, RTPS_SM_ACKNACK, 0x00, BODY, 2);
        for (size_t i = 0; i < stub; i++)
        {
            msg[n + i] = 0x00;
        }
        n += stub;

        if (stub == 0)
        {
            TEST_ASSERT_TRUE(walk(msg, n));
            TEST_ASSERT_EQUAL_UINT(1u, g_seen_n);
            TEST_ASSERT_EQUAL_HEX8(RTPS_SM_ACKNACK, g_seen[0].id);
        }
        else
        {
            TEST_ASSERT_FALSE(walk(msg, n));
        }
    }
}

// sec 9.4.5.1 "Version 2.5 defines the following Submessages":
//     @value(0x01) PAD              @value(0x0d) INFO_REPLY_IP4
//     @value(0x06) ACKNACK          @value(0x0e) INFO_DST
//     @value(0x07) HEARTBEAT        @value(0x0f) INFO_REPLY
//     @value(0x08) GAP              @value(0x15) DATA
//     @value(0x09) INFO_TS          @value(0x16) DATA_FRAG
//     @value(0x0c) INFO_SRC
// Each is carried through a build and back out of a walk unchanged.
void test_submessage_kinds_match_the_published_enum(void)
{
    static const uint8_t KIND[] = {RTPS_SM_PAD,        RTPS_SM_ACKNACK,  RTPS_SM_HEARTBEAT,      RTPS_SM_GAP,
                                   RTPS_SM_INFO_TS,    RTPS_SM_INFO_SRC, RTPS_SM_INFO_REPLY_IP4, RTPS_SM_INFO_DST,
                                   RTPS_SM_INFO_REPLY, RTPS_SM_DATA,     RTPS_SM_DATA_FRAG};
    static const uint8_t BODY[3] = {0x11, 0x22, 0x33};

    TEST_ASSERT_EQUAL_HEX8(0x01, RTPS_SM_PAD);
    TEST_ASSERT_EQUAL_HEX8(0x06, RTPS_SM_ACKNACK);
    TEST_ASSERT_EQUAL_HEX8(0x07, RTPS_SM_HEARTBEAT);
    TEST_ASSERT_EQUAL_HEX8(0x08, RTPS_SM_GAP);
    TEST_ASSERT_EQUAL_HEX8(0x09, RTPS_SM_INFO_TS);
    TEST_ASSERT_EQUAL_HEX8(0x0c, RTPS_SM_INFO_SRC);
    TEST_ASSERT_EQUAL_HEX8(0x0d, RTPS_SM_INFO_REPLY_IP4);
    TEST_ASSERT_EQUAL_HEX8(0x0e, RTPS_SM_INFO_DST);
    TEST_ASSERT_EQUAL_HEX8(0x0f, RTPS_SM_INFO_REPLY);
    TEST_ASSERT_EQUAL_HEX8(0x15, RTPS_SM_DATA);
    TEST_ASSERT_EQUAL_HEX8(0x16, RTPS_SM_DATA_FRAG);

    for (size_t i = 0; i < sizeof(KIND); i++)
    {
        uint8_t msg[32];
        size_t n = build_header(msg, sizeof(msg));
        n += build_submessage(msg + n, sizeof(msg) - n, KIND[i], RTPS_FLAG_ENDIAN, BODY, 3);
        TEST_ASSERT_TRUE(walk(msg, n));
        TEST_ASSERT_EQUAL_UINT(1u, g_seen_n);
        TEST_ASSERT_EQUAL_HEX8(KIND[i], g_seen[0].id);
        TEST_ASSERT_EQUAL_UINT(3u, g_seen[0].contents_len);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(BODY, g_seen[0].contents, 3);
    }
}

// A build handed one octet less than the 4 + contents_len of sec 9.4.5.1 writes nothing, and a
// declared contents length with no contents behind it writes nothing.
void test_submessage_refuses_a_short_buffer(void)
{
    static const uint8_t BODY[4] = {1, 2, 3, 4};
    uint8_t buf[8];
    memset(buf, 0xAA, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT(0u, build_submessage(buf, 7, RTPS_SM_DATA, 0x00, BODY, 4));
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[0]);
    TEST_ASSERT_EQUAL_UINT(8u, build_submessage(buf, 8, RTPS_SM_DATA, 0x00, BODY, 4));

    TEST_ASSERT_EQUAL_UINT(0u, build_submessage(buf, sizeof(buf), RTPS_SM_DATA, 0x00, NULL, 4));

    TEST_ASSERT_EQUAL_UINT(0u, build_submessage(NULL, 64, RTPS_SM_DATA, 0x00, BODY, 4));
}

// The verdict does not depend on the sink: the same Message reads valid with no callback, and one
// octet short of its Submessage contents reads invalid with none either.
void test_parse_without_a_sink_still_validates(void)
{
    static const uint8_t BODY[2] = {0x01, 0x02};
    uint8_t msg[32];
    size_t n = build_header(msg, sizeof(msg));
    n += build_submessage(msg + n, sizeof(msg) - n, RTPS_SM_DATA, 0x00, BODY, 2);

    Rtps.msg.msg = msg;
    Rtps.msg.len = n;
    Rtps.sink.on_submessage = NULL;
    Rtps.sink.arg = NULL;
    Rtps.parse(Rtps.internal);
    TEST_ASSERT_TRUE(Rtps.ok);

    Rtps.msg.msg = msg;
    Rtps.msg.len = n - 1;
    Rtps.parse(Rtps.internal);
    TEST_ASSERT_FALSE(Rtps.ok);
}
