// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the IEC 61850 GOOSE publisher / subscriber codec (services/energy/goose/goose.h).
//
// GOOSE carries protection trips, so the encoding has to be exact rather than merely self-consistent.
// The load-bearing case is test_pdu_is_ber_encoded_in_tag_order: every octet it asserts is derived
// from ITU-T X.690 - sec 8.1.3.4 short-form definite length, sec 8.3.2 minimal two's-complement
// INTEGER contents, sec 11.1 BOOLEAN TRUE as 0xFF - laid over the IECGoosePdu context tags 0x61,
// 0x80..0x8A and 0xAB that IEC 61850-8-1 assigns. test_long_form_length_boundary then crosses
// X.690 sec 8.1.3.5's 128-octet boundary, where a subscriber that only ever saw short lengths
// silently reads the length octet as the first content octet.
//
// The Ethernet framing is IEC 61850-8-1's: ethertype 0x88B8 and the 01-0C-CD-01-xx-xx multicast
// destination range reserved for GOOSE.

#include "services/energy/goose/goose.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static const uint8_t ZERO_TIME[8] = {0, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t ALL_DATA[2] = {0x83, 0x00}; // one Data element: a zero-length boolean

static protocore_goose sample(void)
{
    protocore_goose g;
    g.gocb_ref = "A";
    g.time_allowed_to_live = 1000u;
    g.dat_set = "B";
    g.go_id = "C";
    g.t = ZERO_TIME;
    g.st_num = 1u;
    g.sq_num = 2u;
    g.simulation = PROTO_FALSE;
    g.conf_rev = 1u;
    g.nds_com = PROTO_FALSE;
    g.num_entries = 0u;
    g.all_data = ALL_DATA;
    g.all_data_len = sizeof(ALL_DATA);
    return g;
}

// The whole PDU, octet for octet. Every length here is X.690 sec 8.1.3.4 short form (one octet, the
// value itself, because each is below 128), every INTEGER is sec 8.3.2 minimal big-endian
// two's complement, and every BOOLEAN is one octet.
//
//   80 01 41            gocbRef "A"
//   81 02 03 E8         timeAllowedToLive 1000 = 0x03E8 (0x03 has no sign bit, so no 0x00 pad)
//   82 01 42            datSet "B"
//   83 01 43            goID "C"
//   84 08 00 x8         t, the 8-octet UtcTime
//   85 01 01            stNum 1
//   86 01 02            sqNum 2
//   87 01 00            simulation FALSE
//   88 01 01            confRev 1
//   89 01 00            ndsCom FALSE
//   8A 01 00            numDatSetEntries 0
//   AB 02 83 00         allData
//   ------------------- 3+4+3+3+10+3+3+3+3+3+3+4 = 45 content octets, so the wrapper is 61 2D
void test_pdu_is_ber_encoded_in_tag_order(void)
{
    static const uint8_t WANT[] = {
        0x61, 0x2D,                               //
        0x80, 0x01, 0x41,                         //
        0x81, 0x02, 0x03, 0xE8,                   //
        0x82, 0x01, 0x42,                         //
        0x83, 0x01, 0x43,                         //
        0x84, 0x08, 0,    0,    0, 0, 0, 0, 0, 0, //
        0x85, 0x01, 0x01,                         //
        0x86, 0x01, 0x02,                         //
        0x87, 0x01, 0x00,                         //
        0x88, 0x01, 0x01,                         //
        0x89, 0x01, 0x00,                         //
        0x8A, 0x01, 0x00,                         //
        0xAB, 0x02, 0x83, 0x00,                   //
    };
    uint8_t out[128];
    protocore_goose g = sample();
    size_t n = protocore_goose_pdu(&g, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT, out, sizeof(WANT));
}

// X.690 sec 11.1: "If the boolean value is TRUE the octet shall have any non-zero value" and DER
// fixes that as 1111 1111. The two flags flip together and independently of everything else.
void test_boolean_true_is_all_ones(void)
{
    uint8_t out[128];
    protocore_goose g = sample();
    g.simulation = PROTO_TRUE;
    g.nds_com = PROTO_TRUE;
    size_t n = protocore_goose_pdu(&g, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(47u, n);
    TEST_ASSERT_EQUAL_HEX8(0x87u, out[31]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, out[32]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, out[33]);
    TEST_ASSERT_EQUAL_HEX8(0x89u, out[37]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, out[38]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, out[39]);
}

// X.690 sec 8.3.2: an INTEGER's contents are the minimal number of two's-complement octets, so a
// value whose top octet has the sign bit set takes a leading 0x00 to stay positive.
void test_integer_contents_are_minimal_and_positive(void)
{
    uint8_t out[128];
    protocore_goose g = sample();

    g.st_num = 0u;
    g.sq_num = 0x80u;
    g.conf_rev = 0x0100u;
    size_t n = protocore_goose_pdu(&g, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0u);

    // stNum 0 is one 0x00 octet, not an empty one.
    TEST_ASSERT_EQUAL_HEX8(0x85u, out[25]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, out[26]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[27]);
    // sqNum 0x80 needs the 0x00 sign pad: two content octets.
    TEST_ASSERT_EQUAL_HEX8(0x86u, out[28]);
    TEST_ASSERT_EQUAL_HEX8(0x02u, out[29]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[30]);
    TEST_ASSERT_EQUAL_HEX8(0x80u, out[31]);

    // Read the same values back, since a pad octet that leaked into the value would show here.
    uint8_t frame[256];
    protocore_goose_rx rx;
    static const uint8_t MAC[6] = {0x01, 0x0C, 0xCD, 0x01, 0x00, 0x01};
    size_t f = protocore_goose_frame(MAC, MAC, 0x3000u, &g, frame, sizeof(frame));
    TEST_ASSERT_TRUE(protocore_goose_parse_frame(frame, f, &rx));
    TEST_ASSERT_EQUAL_UINT32(0u, rx.st_num);
    TEST_ASSERT_EQUAL_UINT32(0x80u, rx.sq_num);
    TEST_ASSERT_EQUAL_UINT32(0x0100u, rx.conf_rev);
}

// X.690 sec 8.1.3.5: a content length of 128 or more takes the long form - an initial octet of
// 0x80 | (number of subsequent length octets), then the length itself, big-endian.
void test_long_form_length_boundary(void)
{
    uint8_t big[200];
    uint8_t out[512];
    protocore_goose g = sample();
    memset(big, 0x5A, sizeof(big));
    g.all_data = big;
    g.all_data_len = sizeof(big);

    // The allData TLV is 1 tag + 2 length octets + 200 = 203; the other eleven fields are 41 octets,
    // so the content is 244 and the wrapper becomes 61 81 F4.
    size_t n = protocore_goose_pdu(&g, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(247u, n);
    TEST_ASSERT_EQUAL_HEX8(0x61u, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x81u, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0xF4u, out[2]);
    // The allData field itself carries the same long form: AB 81 C8.
    TEST_ASSERT_EQUAL_HEX8(0xABu, out[44]);
    TEST_ASSERT_EQUAL_HEX8(0x81u, out[45]);
    TEST_ASSERT_EQUAL_HEX8(0xC8u, out[46]);
    TEST_ASSERT_EQUAL_HEX8(0x5Au, out[47]);

    // And it survives the round trip at that size.
    uint8_t frame[512];
    protocore_goose_rx rx;
    static const uint8_t MAC[6] = {0x01, 0x0C, 0xCD, 0x01, 0x00, 0x01};
    size_t f = protocore_goose_frame(MAC, MAC, 0x3000u, &g, frame, sizeof(frame));
    TEST_ASSERT_EQUAL_UINT(22u + n, f);
    TEST_ASSERT_TRUE(protocore_goose_parse_frame(frame, f, &rx));
    TEST_ASSERT_EQUAL_UINT(sizeof(big), rx.all_data_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(big, rx.all_data, sizeof(big));
}

// The Ethernet + GOOSE header: destination, source, ethertype 0x88B8, then APPID, the length field
// (the 8-octet GOOSE header plus the APDU), and two reserved words that publish as zero.
void test_ethernet_and_goose_header_layout(void)
{
    static const uint8_t DST[6] = {0x01, 0x0C, 0xCD, 0x01, 0x00, 0x01}; // the GOOSE multicast range
    static const uint8_t SRC[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t frame[256];
    protocore_goose g = sample();

    size_t n = protocore_goose_frame(DST, SRC, 0x3001u, &g, frame, sizeof(frame));
    TEST_ASSERT_EQUAL_UINT(22u + 47u, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DST, frame, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SRC, frame + 6, 6);
    TEST_ASSERT_EQUAL_HEX8(0x88u, frame[12]);
    TEST_ASSERT_EQUAL_HEX8(0xB8u, frame[13]);
    TEST_ASSERT_EQUAL_HEX8(0x30u, frame[14]); // APPID 0x3001, big-endian
    TEST_ASSERT_EQUAL_HEX8(0x01u, frame[15]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, frame[16]); // length = 8 + 47 = 55
    TEST_ASSERT_EQUAL_HEX8(55u, frame[17]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, frame[18]); // reserved1
    TEST_ASSERT_EQUAL_HEX8(0x00u, frame[19]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, frame[20]); // reserved2
    TEST_ASSERT_EQUAL_HEX8(0x00u, frame[21]);
    TEST_ASSERT_EQUAL_HEX8(0x61u, frame[22]); // the IECGoosePdu starts here
}

// Publish then subscribe: every control field comes back with the value it went in with.
void test_publish_subscribe_round_trip(void)
{
    static const uint8_t DST[6] = {0x01, 0x0C, 0xCD, 0x01, 0x00, 0x02};
    static const uint8_t SRC[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    static const uint8_t UTC[8] = {0x2E, 0xBC, 0x5C, 0x61, 0x80, 0x00, 0x00, 0x0A};
    uint8_t frame[256];
    protocore_goose_rx rx;

    protocore_goose g = sample();
    g.gocb_ref = "SUBST/LLN0$GO$gcb01";
    g.dat_set = "SUBST/LLN0$DS01";
    g.go_id = "TRIP1";
    g.t = UTC;
    g.time_allowed_to_live = 4000u;
    g.st_num = 0x00010203u;
    g.sq_num = 0x00FFFFFFu;
    g.simulation = PROTO_TRUE;
    g.conf_rev = 7u;
    g.nds_com = PROTO_FALSE;
    g.num_entries = 3u;

    size_t n = protocore_goose_frame(DST, SRC, 0x1234u, &g, frame, sizeof(frame));
    TEST_ASSERT_TRUE(n > 22u);
    TEST_ASSERT_TRUE(protocore_goose_parse_frame(frame, n, &rx));

    TEST_ASSERT_EQUAL_HEX16(0x1234u, rx.appid);
    TEST_ASSERT_EQUAL_UINT(strlen(g.gocb_ref), rx.gocb_ref_len);
    TEST_ASSERT_EQUAL_MEMORY(g.gocb_ref, rx.gocb_ref, rx.gocb_ref_len);
    TEST_ASSERT_EQUAL_UINT(strlen(g.dat_set), rx.dat_set_len);
    TEST_ASSERT_EQUAL_MEMORY(g.dat_set, rx.dat_set, rx.dat_set_len);
    TEST_ASSERT_EQUAL_UINT(strlen(g.go_id), rx.go_id_len);
    TEST_ASSERT_EQUAL_MEMORY(g.go_id, rx.go_id, rx.go_id_len);
    TEST_ASSERT_NOT_NULL(rx.t);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(UTC, rx.t, 8);
    TEST_ASSERT_EQUAL_UINT32(4000u, rx.time_allowed_to_live);
    TEST_ASSERT_EQUAL_UINT32(0x00010203u, rx.st_num);
    TEST_ASSERT_EQUAL_UINT32(0x00FFFFFFu, rx.sq_num);
    TEST_ASSERT_TRUE(rx.simulation);
    TEST_ASSERT_EQUAL_UINT32(7u, rx.conf_rev);
    TEST_ASSERT_FALSE(rx.nds_com);
    TEST_ASSERT_EQUAL_UINT32(3u, rx.num_entries);
    TEST_ASSERT_EQUAL_UINT(sizeof(ALL_DATA), rx.all_data_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ALL_DATA, rx.all_data, sizeof(ALL_DATA));

    // The decoded strings and blobs point into the caller's frame, not into a copy.
    TEST_ASSERT_TRUE(rx.gocb_ref > (const char *)frame);
    TEST_ASSERT_TRUE(rx.gocb_ref < (const char *)frame + n);
}

// An unknown or future PDU tag is stepped over, so a publisher that adds a field does not stop an
// existing subscriber from reading the ones it knows.
void test_unknown_pdu_tags_are_skipped(void)
{
    // 22 octets of Ethernet + GOOSE header, then 61 0A { 80 01 41, 8F 02 AA BB, 85 01 07 }.
    static const uint8_t FRAME[34] = {
        0x01, 0x0C, 0xCD, 0x01, 0x00, 0x01, // destination
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, // source
        0x88, 0xB8,                         // ethertype
        0x40, 0x01,                         // APPID
        0x00, 0x14,                         // length: 8 header + 12 APDU
        0x00, 0x00, 0x00, 0x00,             // reserved
        0x61, 0x0A,                         // IECGoosePdu, 10 content octets
        0x80, 0x01, 0x41,                   // gocbRef "A"
        0x8F, 0x02, 0xAA, 0xBB,             // an unassigned context tag
        0x85, 0x01, 0x07,                   // stNum 7
    };
    protocore_goose_rx rx;
    TEST_ASSERT_TRUE(protocore_goose_parse_frame(FRAME, sizeof(FRAME), &rx));
    TEST_ASSERT_EQUAL_HEX16(0x4001u, rx.appid);
    TEST_ASSERT_EQUAL_UINT(1u, rx.gocb_ref_len);
    TEST_ASSERT_EQUAL_MEMORY("A", rx.gocb_ref, 1);
    TEST_ASSERT_EQUAL_UINT32(7u, rx.st_num);
    TEST_ASSERT_NULL(rx.dat_set); // absent stays absent rather than picking up the unknown field
    TEST_ASSERT_NULL(rx.all_data);
}

// A frame that is not GOOSE, or whose BER is truncated, is refused rather than half-decoded.
void test_parse_rejects_a_frame_that_is_not_a_valid_goose_apdu(void)
{
    static const uint8_t DST[6] = {0x01, 0x0C, 0xCD, 0x01, 0x00, 0x01};
    uint8_t frame[256];
    protocore_goose_rx rx;
    protocore_goose g = sample();
    size_t n = protocore_goose_frame(DST, DST, 0x3000u, &g, frame, sizeof(frame));
    TEST_ASSERT_TRUE(protocore_goose_parse_frame(frame, n, &rx));

    TEST_ASSERT_FALSE(protocore_goose_parse_frame(frame, 23u, &rx)); // below the minimum
    TEST_ASSERT_FALSE(protocore_goose_parse_frame(NULL, n, &rx));
    TEST_ASSERT_FALSE(protocore_goose_parse_frame(frame, n, NULL));

    uint8_t saved = frame[13];
    frame[13] = 0xB9u; // ethertype 0x88B9 is not GOOSE
    TEST_ASSERT_FALSE(protocore_goose_parse_frame(frame, n, &rx));
    frame[13] = saved;

    saved = frame[22];
    frame[22] = 0x60u; // not the IECGoosePdu tag
    TEST_ASSERT_FALSE(protocore_goose_parse_frame(frame, n, &rx));
    frame[22] = saved;

    saved = frame[23];
    frame[23] = 0x7Fu; // an outer length past the end of the buffer
    TEST_ASSERT_FALSE(protocore_goose_parse_frame(frame, n, &rx));
    frame[23] = saved;

    TEST_ASSERT_TRUE(protocore_goose_parse_frame(frame, n, &rx)); // restored
}

// A buffer that cannot hold the encoding writes nothing and reports 0, at both layers.
void test_build_refuses_an_undersized_buffer(void)
{
    static const uint8_t MAC[6] = {0x01, 0x0C, 0xCD, 0x01, 0x00, 0x01};
    uint8_t out[128];
    protocore_goose g = sample();

    size_t pdu = protocore_goose_pdu(&g, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(47u, pdu);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_goose_pdu(&g, out, pdu - 1u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_goose_pdu(&g, out, 3u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_goose_pdu(NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_goose_pdu(&g, NULL, sizeof(out)));

    size_t frame = protocore_goose_frame(MAC, MAC, 1u, &g, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(22u + pdu, frame);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_goose_frame(MAC, MAC, 1u, &g, out, frame - 1u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_goose_frame(MAC, MAC, 1u, &g, out, 21u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_goose_frame(NULL, MAC, 1u, &g, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_goose_frame(MAC, NULL, 1u, &g, out, sizeof(out)));
}

// A null string field encodes as a zero-length one rather than dereferencing, and a null UtcTime
// publishes eight zero octets so the field width never changes.
void test_absent_optional_fields_encode_as_empty(void)
{
    uint8_t out[128];
    protocore_goose g = sample();
    g.gocb_ref = NULL;
    g.dat_set = NULL;
    g.go_id = NULL;
    g.t = NULL;
    g.all_data = NULL;
    g.all_data_len = 0u;

    size_t n = protocore_goose_pdu(&g, out, sizeof(out));
    // Content: 2+2+2 (three empty strings) + 4 (ttl 1000) + 10 (t) + 3+3+3+3 (stNum, sqNum, confRev,
    // numDatSetEntries) + 3+3 (two booleans) + 2 (empty allData) = 40, so the wrapper is 61 28.
    TEST_ASSERT_EQUAL_UINT(42u, n);
    TEST_ASSERT_EQUAL_HEX8(0x61u, out[0]);
    TEST_ASSERT_EQUAL_HEX8(40u, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x80u, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[3]); // zero-length gocbRef
    TEST_ASSERT_EQUAL_HEX8(0x82u, out[8]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[9]);
    TEST_ASSERT_EQUAL_HEX8(0x83u, out[10]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[11]);
    TEST_ASSERT_EQUAL_HEX8(0x84u, out[12]);
    TEST_ASSERT_EQUAL_HEX8(0x08u, out[13]); // t is still eight octets
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[14]);
    TEST_ASSERT_EQUAL_HEX8(0xABu, out[n - 2]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[n - 1]);
}
