// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the DNP3 (IEEE 1815) link, transport and application codec (services/energy/dnp3/dnp3.h).
//
// The load-bearing case is test_crc16_dnp_published_check_value. IEEE 1815 protects every link block
// with CRC-16/DNP (poly 0x3D65, init 0x0000, reflected in and out, final XOR 0xFFFF), whose
// catalogued check value - the CRC of the nine ASCII octets "123456789" - is 0xEA82. An outstation
// answers nothing whose block CRCs it cannot reproduce, so pinning that one number is what makes
// every framing assertion here mean anything on a real bus.
//
// The field positions are IEEE 1815's own link-frame layout (0x0564, LEN, CTRL, DEST and SRC
// little-endian, a CRC after the 8-octet header and after each <= 16-octet data block, low octet
// first), the sec 8.2 transport header (FIN bit 7, FIR bit 6, 6-bit sequence), and the sec 4.2.2
// application header (Application Control, Function Code, and two IIN octets on a response).

#include "services/energy/dnp3/dnp3.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// The catalogued check value for CRC-16/DNP, the block check IEEE 1815 specifies.
void test_crc16_dnp_published_check_value(void)
{
    static const uint8_t CHECK[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX16(0xEA82u, protocore_dnp3_crc(CHECK, sizeof(CHECK)));
    // init 0x0000 with a final XOR of 0xFFFF, so the empty message is 0xFFFF.
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, protocore_dnp3_crc(CHECK, 0));
}

// --- link-layer framing ---------------------------------------------------------------------------

// The 10-octet header block, octet by octet: the start word, LEN (which counts CTRL + DEST + SRC +
// user data and nothing else), the control octet, the two little-endian addresses, and the header
// CRC written low octet first.
void test_header_block_field_layout(void)
{
    uint8_t buf[64];
    size_t n = protocore_dnp3_build_frame(buf, sizeof(buf), 0xC4u, 0x0001u, 0x03E8u, NULL, 0);

    TEST_ASSERT_EQUAL_UINT(10u, n); // header block alone: no data blocks
    TEST_ASSERT_EQUAL_HEX8(0x05u, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x64u, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(5u, buf[2]); // LEN = DNP3_LEN_OVERHEAD + 0 user octets
    TEST_ASSERT_EQUAL_HEX8(0xC4u, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, buf[4]); // DEST 0x0001, little-endian
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[5]);
    TEST_ASSERT_EQUAL_HEX8(0xE8u, buf[6]); // SRC 0x03E8, little-endian
    TEST_ASSERT_EQUAL_HEX8(0x03u, buf[7]);

    uint16_t crc = protocore_dnp3_crc(buf, DNP3_HEADER_LEN);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc & 0xFFu), buf[8]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc >> 8), buf[9]);
}

// User data is cut into 16-octet blocks, each followed by its own CRC. 17 octets therefore make a
// full block plus a one-octet block: 10 + (16 + 2) + (1 + 2) = 31.
void test_user_data_is_carried_in_crc_protected_blocks(void)
{
    uint8_t data[17];
    uint8_t buf[64];
    for (size_t i = 0; i < sizeof(data); i++)
    {
        data[i] = (uint8_t)(0x10u + i);
    }
    size_t n = protocore_dnp3_build_frame(buf, sizeof(buf), 0x44u, 4u, 3u, data, sizeof(data));
    TEST_ASSERT_EQUAL_UINT(31u, n);
    TEST_ASSERT_EQUAL_HEX8(5u + 17u, buf[2]); // LEN counts the user data

    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, buf + 10, 16);
    uint16_t c0 = protocore_dnp3_crc(buf + 10, 16);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(c0 & 0xFFu), buf[26]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(c0 >> 8), buf[27]);

    TEST_ASSERT_EQUAL_HEX8(data[16], buf[28]);
    uint16_t c1 = protocore_dnp3_crc(buf + 28, 1);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(c1 & 0xFFu), buf[29]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(c1 >> 8), buf[30]);
}

// Build then parse returns the header fields and the de-blocked user data unchanged, at every block
// boundary and at the 250-octet ceiling LEN's single octet imposes.
void test_frame_round_trip_across_block_boundaries(void)
{
    static const size_t LENS[] = {0, 1, 15, 16, 17, 32, 33, DNP3_MAX_USER_DATA};
    uint8_t data[DNP3_MAX_USER_DATA];
    for (size_t i = 0; i < sizeof(data); i++)
    {
        data[i] = (uint8_t)(i * 7u + 1u);
    }

    for (size_t k = 0; k < sizeof(LENS) / sizeof(LENS[0]); k++)
    {
        uint8_t buf[512];
        uint8_t out[DNP3_MAX_USER_DATA];
        Dnp3Frame f;
        size_t got = 0;
        size_t len = LENS[k];
        size_t nblocks = (len + DNP3_BLOCK_LEN - 1) / DNP3_BLOCK_LEN;

        size_t n = protocore_dnp3_build_frame(buf, sizeof(buf), 0xC4u, 0x1234u, 0xABCDu, data, len);
        TEST_ASSERT_EQUAL_UINT(DNP3_HEADER_BLOCK_LEN + len + nblocks * DNP3_CRC_LEN, n);
        TEST_ASSERT_TRUE(protocore_dnp3_parse_frame(buf, n, &f, out, sizeof(out), &got));
        TEST_ASSERT_EQUAL_UINT8(DNP3_LEN_OVERHEAD + len, f.length);
        TEST_ASSERT_EQUAL_HEX8(0xC4u, f.control);
        TEST_ASSERT_EQUAL_HEX16(0x1234u, f.dest);
        TEST_ASSERT_EQUAL_HEX16(0xABCDu, f.src);
        TEST_ASSERT_EQUAL_UINT(len, got);
        if (len)
        {
            TEST_ASSERT_EQUAL_UINT8_ARRAY(data, out, len);
        }
    }
}

// A flipped bit in the header block fails the header CRC; one in a data block fails that block's CRC.
void test_parse_rejects_a_corrupted_block(void)
{
    static const uint8_t DATA[20] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
    uint8_t buf[64];
    uint8_t out[32];
    Dnp3Frame f;
    size_t got = 0;
    size_t n = protocore_dnp3_build_frame(buf, sizeof(buf), 0x44u, 4u, 3u, DATA, sizeof(DATA));
    TEST_ASSERT_TRUE(protocore_dnp3_parse_frame(buf, n, &f, out, sizeof(out), &got));

    for (size_t i = 0; i < n; i++)
    {
        uint8_t saved = buf[i];
        if (i == 2) // LEN is a length, not payload: a flip there is a truncation case, not a CRC one
        {
            continue;
        }
        buf[i] = (uint8_t)(saved ^ 0x80u);
        TEST_ASSERT_FALSE_MESSAGE(protocore_dnp3_parse_frame(buf, n, &f, out, sizeof(out), &got),
                                  "flipped octet parsed as valid");
        buf[i] = saved;
    }
    TEST_ASSERT_TRUE(protocore_dnp3_parse_frame(buf, n, &f, out, sizeof(out), &got));
}

// The framing itself is refused before any CRC: a wrong start word, a LEN that does not even cover
// CTRL + DEST + SRC, a frame that is not fully buffered, or user data with nowhere to land.
void test_parse_rejects_malformed_framing(void)
{
    static const uint8_t DATA[4] = {9, 8, 7, 6};
    uint8_t buf[64];
    uint8_t out[32];
    Dnp3Frame f;
    size_t got = 0;
    size_t n = protocore_dnp3_build_frame(buf, sizeof(buf), 0x44u, 4u, 3u, DATA, sizeof(DATA));

    TEST_ASSERT_FALSE(protocore_dnp3_parse_frame(buf, n - 1, &f, out, sizeof(out), &got));
    TEST_ASSERT_FALSE(protocore_dnp3_parse_frame(buf, DNP3_HEADER_BLOCK_LEN - 1, &f, out, sizeof(out), &got));
    TEST_ASSERT_FALSE(protocore_dnp3_parse_frame(NULL, n, &f, out, sizeof(out), &got));
    TEST_ASSERT_FALSE(protocore_dnp3_parse_frame(buf, n, NULL, out, sizeof(out), &got));
    TEST_ASSERT_FALSE(protocore_dnp3_parse_frame(buf, n, &f, out, 3u, &got)); // out_cap < user data

    uint8_t saved = buf[0];
    buf[0] = 0x06u;
    TEST_ASSERT_FALSE(protocore_dnp3_parse_frame(buf, n, &f, out, sizeof(out), &got));
    buf[0] = saved;
    saved = buf[1];
    buf[1] = 0x65u;
    TEST_ASSERT_FALSE(protocore_dnp3_parse_frame(buf, n, &f, out, sizeof(out), &got));
    buf[1] = saved;

    buf[2] = 4u; // LEN below DNP3_LEN_OVERHEAD
    TEST_ASSERT_FALSE(protocore_dnp3_parse_frame(buf, n, &f, out, sizeof(out), &got));
}

// LEN is one octet and counts five overhead octets, so 250 user octets is the hard ceiling.
void test_build_refuses_oversized_or_unbuffered_frames(void)
{
    uint8_t data[DNP3_MAX_USER_DATA + 1];
    uint8_t buf[512];
    memset(data, 0xA5, sizeof(data));

    TEST_ASSERT_TRUE(protocore_dnp3_build_frame(buf, sizeof(buf), 0u, 0u, 0u, data, DNP3_MAX_USER_DATA) > 0);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnp3_build_frame(buf, sizeof(buf), 0u, 0u, 0u, data, DNP3_MAX_USER_DATA + 1));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnp3_build_frame(buf, 9u, 0u, 0u, 0u, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnp3_build_frame(NULL, sizeof(buf), 0u, 0u, 0u, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnp3_build_frame(buf, sizeof(buf), 0u, 0u, 0u, NULL, 4));
}

// --- transport function (IEEE 1815 sec 8.2) -------------------------------------------------------

// FIN is bit 7, FIR is bit 6, and the sequence occupies the low 6 bits.
void test_transport_header_bit_layout(void)
{
    TEST_ASSERT_EQUAL_HEX8(0xC0u, protocore_dnp3_transport_header(PROTO_TRUE, PROTO_TRUE, 0u));
    TEST_ASSERT_EQUAL_HEX8(0x40u, protocore_dnp3_transport_header(PROTO_TRUE, PROTO_FALSE, 0u));
    TEST_ASSERT_EQUAL_HEX8(0x80u, protocore_dnp3_transport_header(PROTO_FALSE, PROTO_TRUE, 0u));
    TEST_ASSERT_EQUAL_HEX8(0x45u, protocore_dnp3_transport_header(PROTO_TRUE, PROTO_FALSE, 5u));
    TEST_ASSERT_EQUAL_HEX8(0x3Fu, protocore_dnp3_transport_header(PROTO_FALSE, PROTO_FALSE, 63u));
    // The sequence is 6 bits wide, so a wider value cannot reach the FIR / FIN bits.
    TEST_ASSERT_EQUAL_HEX8(0x00u, protocore_dnp3_transport_header(PROTO_FALSE, PROTO_FALSE, 64u));
    TEST_ASSERT_EQUAL_HEX8(0x3Fu, protocore_dnp3_transport_header(PROTO_FALSE, PROTO_FALSE, 0xFFu));
}

// A segment is the header octet followed by up to 249 application octets.
void test_transport_segment_build(void)
{
    static const uint8_t APP[3] = {0xC0, 0x01, 0x3C};
    uint8_t seg[8];
    TEST_ASSERT_EQUAL_UINT(
        4u, protocore_dnp3_build_transport_segment(seg, sizeof(seg), PROTO_TRUE, PROTO_TRUE, 7u, APP, sizeof(APP)));
    TEST_ASSERT_EQUAL_HEX8(0xC7u, seg[0]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(APP, seg + 1, sizeof(APP));

    uint8_t big[DNP3_TR_MAX_APP + 1];
    memset(big, 0x5A, sizeof(big));
    uint8_t out[DNP3_TR_MAX_APP + 2];
    TEST_ASSERT_EQUAL_UINT(
        1u + DNP3_TR_MAX_APP,
        protocore_dnp3_build_transport_segment(out, sizeof(out), PROTO_TRUE, PROTO_TRUE, 0u, big, DNP3_TR_MAX_APP));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnp3_build_transport_segment(out, sizeof(out), PROTO_TRUE, PROTO_TRUE, 0u, big,
                                                                      DNP3_TR_MAX_APP + 1));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnp3_build_transport_segment(seg, 3u, PROTO_TRUE, PROTO_TRUE, 0u, APP, 3));
    TEST_ASSERT_EQUAL_UINT(
        0u, protocore_dnp3_build_transport_segment(NULL, sizeof(seg), PROTO_TRUE, PROTO_TRUE, 0u, APP, 3));
}

// A fragment opens with FIR, continues with sequence-incrementing segments, and closes with FIN;
// the reassembled octets are the concatenation of the segment bodies in order.
void test_transport_reassembles_a_multi_segment_fragment(void)
{
    Dnp3TransportRx rx;
    uint8_t frag[64];
    uint8_t seg[16];
    protocore_dnp3_transport_rx_init(&rx, frag, sizeof(frag));

    size_t n = protocore_dnp3_build_transport_segment(seg, sizeof(seg), PROTO_TRUE, PROTO_FALSE, 5u,
                                                      (const uint8_t *)"ABCD", 4);
    TEST_ASSERT_EQUAL_INT(DNP3_TR_PROGRESS, protocore_dnp3_transport_feed(&rx, seg, n));

    n = protocore_dnp3_build_transport_segment(seg, sizeof(seg), PROTO_FALSE, PROTO_FALSE, 6u, (const uint8_t *)"EFGH",
                                               4);
    TEST_ASSERT_EQUAL_INT(DNP3_TR_PROGRESS, protocore_dnp3_transport_feed(&rx, seg, n));

    n = protocore_dnp3_build_transport_segment(seg, sizeof(seg), PROTO_FALSE, PROTO_TRUE, 7u, (const uint8_t *)"IJ", 2);
    TEST_ASSERT_EQUAL_INT(DNP3_TR_COMPLETE, protocore_dnp3_transport_feed(&rx, seg, n));

    TEST_ASSERT_EQUAL_UINT(10u, rx.len);
    TEST_ASSERT_EQUAL_MEMORY("ABCDEFGHIJ", rx.buf, 10);
    TEST_ASSERT_TRUE(rx.done);

    // A single-frame fragment sets both FIR and FIN and completes on its own.
    protocore_dnp3_transport_rx_init(&rx, frag, sizeof(frag));
    n = protocore_dnp3_build_transport_segment(seg, sizeof(seg), PROTO_TRUE, PROTO_TRUE, 0u, (const uint8_t *)"X", 1);
    TEST_ASSERT_EQUAL_INT(DNP3_TR_COMPLETE, protocore_dnp3_transport_feed(&rx, seg, n));
    TEST_ASSERT_EQUAL_UINT(1u, rx.len);
}

// The 6-bit sequence wraps at 64, so the segment after 63 carries 0 and is still in order.
void test_transport_sequence_wraps_at_sixty_four(void)
{
    Dnp3TransportRx rx;
    uint8_t frag[32];
    uint8_t seg[8];
    protocore_dnp3_transport_rx_init(&rx, frag, sizeof(frag));

    size_t n = protocore_dnp3_build_transport_segment(seg, sizeof(seg), PROTO_TRUE, PROTO_FALSE, 63u,
                                                      (const uint8_t *)"AB", 2);
    TEST_ASSERT_EQUAL_INT(DNP3_TR_PROGRESS, protocore_dnp3_transport_feed(&rx, seg, n));
    n = protocore_dnp3_build_transport_segment(seg, sizeof(seg), PROTO_FALSE, PROTO_TRUE, 0u, (const uint8_t *)"CD", 2);
    TEST_ASSERT_EQUAL_INT(DNP3_TR_COMPLETE, protocore_dnp3_transport_feed(&rx, seg, n));
    TEST_ASSERT_EQUAL_MEMORY("ABCD", rx.buf, 4);
}

// A continuation with no fragment in progress, and one that skips a sequence number, are discarded
// rather than spliced into whatever was already accumulated.
void test_transport_discards_out_of_sequence_segments(void)
{
    Dnp3TransportRx rx;
    uint8_t frag[32];
    uint8_t seg[8];
    protocore_dnp3_transport_rx_init(&rx, frag, sizeof(frag));

    size_t n = protocore_dnp3_build_transport_segment(seg, sizeof(seg), PROTO_FALSE, PROTO_FALSE, 1u,
                                                      (const uint8_t *)"AB", 2);
    TEST_ASSERT_EQUAL_INT(DNP3_TR_IGNORED, protocore_dnp3_transport_feed(&rx, seg, n)); // no FIR yet

    n = protocore_dnp3_build_transport_segment(seg, sizeof(seg), PROTO_TRUE, PROTO_FALSE, 0u, (const uint8_t *)"AB", 2);
    TEST_ASSERT_EQUAL_INT(DNP3_TR_PROGRESS, protocore_dnp3_transport_feed(&rx, seg, n));
    n = protocore_dnp3_build_transport_segment(seg, sizeof(seg), PROTO_FALSE, PROTO_TRUE, 2u, (const uint8_t *)"CD", 2);
    TEST_ASSERT_EQUAL_INT(DNP3_TR_IGNORED, protocore_dnp3_transport_feed(&rx, seg, n)); // expected 1
    TEST_ASSERT_FALSE(rx.active);
    TEST_ASSERT_FALSE(rx.done);

    // A fresh FIR restarts the fragment from zero rather than appending to the abandoned one.
    n = protocore_dnp3_build_transport_segment(seg, sizeof(seg), PROTO_TRUE, PROTO_TRUE, 9u, (const uint8_t *)"Z", 1);
    TEST_ASSERT_EQUAL_INT(DNP3_TR_COMPLETE, protocore_dnp3_transport_feed(&rx, seg, n));
    TEST_ASSERT_EQUAL_UINT(1u, rx.len);
    TEST_ASSERT_EQUAL_MEMORY("Z", rx.buf, 1);

    TEST_ASSERT_EQUAL_INT(DNP3_TR_IGNORED, protocore_dnp3_transport_feed(&rx, seg, 0)); // no header octet
    TEST_ASSERT_EQUAL_INT(DNP3_TR_IGNORED, protocore_dnp3_transport_feed(&rx, NULL, 4));
}

// A fragment larger than the caller's buffer is abandoned, never truncated into it.
void test_transport_overflow_abandons_the_fragment(void)
{
    Dnp3TransportRx rx;
    uint8_t frag[6];
    uint8_t seg[16];
    protocore_dnp3_transport_rx_init(&rx, frag, sizeof(frag));

    size_t n = protocore_dnp3_build_transport_segment(seg, sizeof(seg), PROTO_TRUE, PROTO_FALSE, 0u,
                                                      (const uint8_t *)"ABCD", 4);
    TEST_ASSERT_EQUAL_INT(DNP3_TR_PROGRESS, protocore_dnp3_transport_feed(&rx, seg, n));
    n = protocore_dnp3_build_transport_segment(seg, sizeof(seg), PROTO_FALSE, PROTO_TRUE, 1u, (const uint8_t *)"EFGH",
                                               4);
    TEST_ASSERT_EQUAL_INT(DNP3_TR_ERROR, protocore_dnp3_transport_feed(&rx, seg, n));
    TEST_ASSERT_FALSE(rx.active);
    TEST_ASSERT_FALSE(rx.done);
    TEST_ASSERT_EQUAL_UINT(4u, rx.len); // the accepted prefix, with nothing written past the buffer
}

// --- application layer (IEEE 1815 sec 4.2.2) ------------------------------------------------------

// The Application Control octet: FIR bit 7, FIN bit 6, CON bit 5, UNS bit 4, 4-bit sequence.
void test_application_control_bit_layout(void)
{
    TEST_ASSERT_EQUAL_HEX8(0xC0u, protocore_dnp3_app_control(PROTO_TRUE, PROTO_TRUE, PROTO_FALSE, PROTO_FALSE, 0u));
    TEST_ASSERT_EQUAL_HEX8(0x20u, protocore_dnp3_app_control(PROTO_FALSE, PROTO_FALSE, PROTO_TRUE, PROTO_FALSE, 0u));
    TEST_ASSERT_EQUAL_HEX8(0x10u, protocore_dnp3_app_control(PROTO_FALSE, PROTO_FALSE, PROTO_FALSE, PROTO_TRUE, 0u));
    TEST_ASSERT_EQUAL_HEX8(0xC5u, protocore_dnp3_app_control(PROTO_TRUE, PROTO_TRUE, PROTO_FALSE, PROTO_FALSE, 5u));
    // The sequence is 4 bits, so it cannot bleed into UNS.
    TEST_ASSERT_EQUAL_HEX8(0x0Fu,
                           protocore_dnp3_app_control(PROTO_FALSE, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE, 0xFFu));
}

// A request fragment is AC + FC + object octets; the header parse hands back the objects that follow.
void test_application_request_round_trip(void)
{
    static const uint8_t OBJECTS[3] = {60, 2, 0x06};
    uint8_t frag[16];
    Dnp3AppHeader h;

    uint8_t ac = protocore_dnp3_app_control(PROTO_TRUE, PROTO_TRUE, PROTO_FALSE, PROTO_FALSE, 3u);
    size_t n = protocore_dnp3_build_app_request(frag, sizeof(frag), ac, DNP3_FC_READ, OBJECTS, sizeof(OBJECTS));
    TEST_ASSERT_EQUAL_UINT(2u + sizeof(OBJECTS), n);
    TEST_ASSERT_EQUAL_HEX8(ac, frag[0]);
    TEST_ASSERT_EQUAL_HEX8(DNP3_FC_READ, frag[1]);

    TEST_ASSERT_TRUE(protocore_dnp3_parse_app_header(frag, n, &h));
    TEST_ASSERT_TRUE(h.fir);
    TEST_ASSERT_TRUE(h.fin);
    TEST_ASSERT_FALSE(h.con);
    TEST_ASSERT_FALSE(h.uns);
    TEST_ASSERT_EQUAL_UINT8(3u, h.seq);
    TEST_ASSERT_EQUAL_HEX8(DNP3_FC_READ, h.fc);
    TEST_ASSERT_FALSE(h.is_response);
    TEST_ASSERT_EQUAL_HEX16(0u, h.iin); // a request carries no internal indications
    TEST_ASSERT_EQUAL_UINT(sizeof(OBJECTS), h.obj_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(OBJECTS, h.objects, sizeof(OBJECTS));

    // A bare 2-octet fragment parses with no object data at all.
    TEST_ASSERT_EQUAL_UINT(2u, protocore_dnp3_build_app_request(frag, sizeof(frag), ac, DNP3_FC_CONFIRM, NULL, 0));
    TEST_ASSERT_TRUE(protocore_dnp3_parse_app_header(frag, 2u, &h));
    TEST_ASSERT_EQUAL_UINT(0u, h.obj_len);
    TEST_ASSERT_NULL(h.objects);
    TEST_ASSERT_FALSE(protocore_dnp3_parse_app_header(frag, 1u, &h));
}

// A response inserts the two IIN octets between the function code and the objects, IIN1 first.
void test_application_response_carries_internal_indications(void)
{
    static const uint8_t OBJECTS[2] = {1, 2};
    uint8_t frag[16];
    Dnp3AppHeader h;
    uint16_t iin = DNP3_IIN_DEVICE_RESTART | DNP3_IIN_CLASS1_EVENTS | DNP3_IIN_OBJECT_UNKNOWN;

    uint8_t ac = protocore_dnp3_app_control(PROTO_TRUE, PROTO_TRUE, PROTO_FALSE, PROTO_FALSE, 1u);
    size_t n =
        protocore_dnp3_build_app_response(frag, sizeof(frag), ac, DNP3_FC_RESPONSE, iin, OBJECTS, sizeof(OBJECTS));
    TEST_ASSERT_EQUAL_UINT(4u + sizeof(OBJECTS), n);
    TEST_ASSERT_EQUAL_HEX8(0x82u, frag[2]); // IIN1: bit 7 device restart | bit 1 class 1 events
    TEST_ASSERT_EQUAL_HEX8(0x02u, frag[3]); // IIN2: bit 1 object unknown

    TEST_ASSERT_TRUE(protocore_dnp3_parse_app_header(frag, n, &h));
    TEST_ASSERT_TRUE(h.is_response);
    TEST_ASSERT_EQUAL_HEX16(iin, h.iin);
    TEST_ASSERT_EQUAL_UINT(sizeof(OBJECTS), h.obj_len);

    // An unsolicited response is the other form that carries IIN.
    n = protocore_dnp3_build_app_response(frag, sizeof(frag), ac, DNP3_FC_UNSOLICITED_RESPONSE, 0u, NULL, 0);
    TEST_ASSERT_EQUAL_UINT(4u, n);
    TEST_ASSERT_TRUE(protocore_dnp3_parse_app_header(frag, n, &h));
    TEST_ASSERT_TRUE(h.is_response);
    // A response truncated before its IIN octets is not a header.
    TEST_ASSERT_FALSE(protocore_dnp3_parse_app_header(frag, 3u, &h));
}

// --- object headers (IEEE 1815 sec 4.3) -----------------------------------------------------------

// The range form is the narrowest that holds the stop index: 1-octet (qualifier 0x00), 2-octet
// (0x01), or 4-octet (0x02) start/stop pairs, each little-endian.
void test_object_header_range_picks_the_narrowest_form(void)
{
    uint8_t buf[16];
    Dnp3ObjectHeader h;

    TEST_ASSERT_EQUAL_UINT(5u, protocore_dnp3_build_object_header_range(buf, sizeof(buf), 1u, 2u, 0u, 9u));
    TEST_ASSERT_EQUAL_HEX8(1u, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(2u, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(DNP3_RANGE_START_STOP_1, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0u, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(9u, buf[4]);
    TEST_ASSERT_TRUE(protocore_dnp3_parse_object_header(buf, 5u, &h));
    TEST_ASSERT_EQUAL_UINT32(0u, h.start);
    TEST_ASSERT_EQUAL_UINT32(9u, h.stop);
    TEST_ASSERT_EQUAL_UINT32(10u, h.count); // stop - start + 1
    TEST_ASSERT_FALSE(h.is_count);
    TEST_ASSERT_EQUAL_UINT8(0u, h.prefix_code);

    TEST_ASSERT_EQUAL_UINT(7u, protocore_dnp3_build_object_header_range(buf, sizeof(buf), 30u, 1u, 0u, 0x0100u));
    TEST_ASSERT_EQUAL_HEX8(DNP3_RANGE_START_STOP_2, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[5]); // stop 0x0100, little-endian
    TEST_ASSERT_EQUAL_HEX8(0x01u, buf[6]);
    TEST_ASSERT_TRUE(protocore_dnp3_parse_object_header(buf, 7u, &h));
    TEST_ASSERT_EQUAL_UINT32(0x0100u, h.stop);
    TEST_ASSERT_EQUAL_UINT32(0x0101u, h.count);

    TEST_ASSERT_EQUAL_UINT(11u, protocore_dnp3_build_object_header_range(buf, sizeof(buf), 30u, 1u, 1u, 0x00012345u));
    TEST_ASSERT_EQUAL_HEX8(DNP3_RANGE_START_STOP_4, buf[2]);
    TEST_ASSERT_TRUE(protocore_dnp3_parse_object_header(buf, 11u, &h));
    TEST_ASSERT_EQUAL_UINT32(1u, h.start);
    TEST_ASSERT_EQUAL_UINT32(0x00012345u, h.stop);

    // A stop before the start names no objects, and a buffer too small writes nothing.
    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnp3_build_object_header_range(buf, sizeof(buf), 1u, 2u, 5u, 4u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnp3_build_object_header_range(buf, 4u, 1u, 2u, 0u, 9u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnp3_build_object_header_range(NULL, sizeof(buf), 1u, 2u, 0u, 9u));
}

// Qualifier 0x06 addresses every object of the group and carries no range field at all - the form a
// Class-data poll of group 60 uses.
void test_object_header_all_objects(void)
{
    uint8_t buf[8];
    Dnp3ObjectHeader h;
    TEST_ASSERT_EQUAL_UINT(3u, protocore_dnp3_build_object_header_all(buf, sizeof(buf), 60u, 2u));
    TEST_ASSERT_EQUAL_HEX8(60u, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(2u, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(DNP3_RANGE_NO_RANGE, buf[2]);

    TEST_ASSERT_TRUE(protocore_dnp3_parse_object_header(buf, 3u, &h));
    TEST_ASSERT_EQUAL_UINT8(60u, h.group);
    TEST_ASSERT_EQUAL_UINT8(2u, h.variation);
    TEST_ASSERT_EQUAL_UINT8(DNP3_RANGE_NO_RANGE, h.range_code);
    TEST_ASSERT_EQUAL_UINT32(0u, h.count);
    TEST_ASSERT_NULL(h.objects);
    TEST_ASSERT_EQUAL_UINT(0u, h.objects_len);

    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnp3_build_object_header_all(buf, 2u, 60u, 2u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnp3_build_object_header_all(NULL, sizeof(buf), 60u, 2u));
}

// The count forms (qualifiers 0x07 / 0x08 / 0x09) carry an object count instead of a range, in 1, 2,
// or 4 little-endian octets, and the prefix code lives in qualifier bits 6-4.
void test_object_header_count_forms_and_prefix_code(void)
{
    Dnp3ObjectHeader h;

    static const uint8_t C1[4] = {12, 1, 0x17, 3}; // g12v1, prefix code 1, count 3
    TEST_ASSERT_TRUE(protocore_dnp3_parse_object_header(C1, sizeof(C1), &h));
    TEST_ASSERT_TRUE(h.is_count);
    TEST_ASSERT_EQUAL_UINT32(3u, h.count);
    TEST_ASSERT_EQUAL_UINT8(1u, h.prefix_code);
    TEST_ASSERT_EQUAL_UINT8(DNP3_RANGE_COUNT_1, h.range_code);

    static const uint8_t C2[5] = {2, 2, 0x08, 0x34, 0x12};
    TEST_ASSERT_TRUE(protocore_dnp3_parse_object_header(C2, sizeof(C2), &h));
    TEST_ASSERT_EQUAL_UINT32(0x1234u, h.count);

    static const uint8_t C4[7] = {2, 2, 0x09, 0x78, 0x56, 0x34, 0x12};
    TEST_ASSERT_TRUE(protocore_dnp3_parse_object_header(C4, sizeof(C4), &h));
    TEST_ASSERT_EQUAL_UINT32(0x12345678u, h.count);

    // A truncated range field, and a qualifier form this decoder does not accept, are both refused.
    TEST_ASSERT_FALSE(protocore_dnp3_parse_object_header(C2, 4u, &h));
    TEST_ASSERT_FALSE(protocore_dnp3_parse_object_header(C4, 6u, &h));
    static const uint8_t BAD[4] = {2, 2, 0x0B, 0};
    TEST_ASSERT_FALSE(protocore_dnp3_parse_object_header(BAD, sizeof(BAD), &h));
    TEST_ASSERT_FALSE(protocore_dnp3_parse_object_header(C1, 2u, &h));
    TEST_ASSERT_FALSE(protocore_dnp3_parse_object_header(NULL, 4u, &h));
}

// --- control objects ------------------------------------------------------------------------------

// A g12v1 Control Relay Output Block: control code (op type in the low nibble, clear at bit 5, the
// trip/close code at bits 6-7), count, on-time and off-time as 4-octet little-endian milliseconds,
// and a status octet the request leaves at 0.
void test_crob_field_layout(void)
{
    uint8_t buf[16];
    TEST_ASSERT_EQUAL_UINT(DNP3_CROB_LEN, protocore_dnp3_build_crob(buf, sizeof(buf), DNP3_CROB_OP_LATCH_ON,
                                                                    DNP3_CROB_TCC_CLOSE, PROTO_FALSE, 1u, 1000u, 0u));
    TEST_ASSERT_EQUAL_HEX8(0x43u, buf[0]); // (1 << 6) | 0x03
    TEST_ASSERT_EQUAL_HEX8(1u, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0xE8u, buf[2]); // 1000 ms = 0x000003E8, little-endian
    TEST_ASSERT_EQUAL_HEX8(0x03u, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[5]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[6]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[7]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[8]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[9]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[10]);

    // The clear bit is bit 5, and the trip code is 2 in bits 6-7.
    TEST_ASSERT_EQUAL_UINT(DNP3_CROB_LEN,
                           protocore_dnp3_build_crob(buf, sizeof(buf), DNP3_CROB_OP_PULSE_ON, DNP3_CROB_TCC_TRIP,
                                                     PROTO_TRUE, 2u, 0u, 0x01020304u));
    TEST_ASSERT_EQUAL_HEX8(0xA1u, buf[0]); // (2 << 6) | 0x20 | 0x01
    TEST_ASSERT_EQUAL_HEX8(0x04u, buf[6]); // off-time 0x01020304, little-endian
    TEST_ASSERT_EQUAL_HEX8(0x03u, buf[7]);
    TEST_ASSERT_EQUAL_HEX8(0x02u, buf[8]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, buf[9]);

    TEST_ASSERT_EQUAL_UINT(
        0u, protocore_dnp3_build_crob(buf, DNP3_CROB_LEN - 1, DNP3_CROB_OP_NUL, 0u, PROTO_FALSE, 0u, 0u, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnp3_build_crob(buf, sizeof(buf), 0x10u, 0u, PROTO_FALSE, 0u, 0u, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnp3_build_crob(buf, sizeof(buf), 0u, 4u, PROTO_FALSE, 0u, 0u, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnp3_build_crob(NULL, sizeof(buf), 0u, 0u, PROTO_FALSE, 0u, 0u, 0u));
}

// A g41v1 Analog Output Block: a signed 32-bit setpoint in two's complement, little-endian, then the
// status octet.
void test_analog_output_block_int32(void)
{
    uint8_t buf[8];
    TEST_ASSERT_EQUAL_UINT(DNP3_AOB_LEN, protocore_dnp3_build_aob32(buf, sizeof(buf), 1000));
    TEST_ASSERT_EQUAL_HEX8(0xE8u, buf[0]); // 1000 = 0x000003E8
    TEST_ASSERT_EQUAL_HEX8(0x03u, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[4]);

    TEST_ASSERT_EQUAL_UINT(DNP3_AOB_LEN, protocore_dnp3_build_aob32(buf, sizeof(buf), -1));
    TEST_ASSERT_EQUAL_HEX8(0xFFu, buf[0]); // two's complement -1 = 0xFFFFFFFF
    TEST_ASSERT_EQUAL_HEX8(0xFFu, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, buf[3]);

    TEST_ASSERT_EQUAL_UINT(DNP3_AOB_LEN, protocore_dnp3_build_aob32(buf, sizeof(buf), -2147483647 - 1));
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[0]); // INT32_MIN = 0x80000000
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x80u, buf[3]);

    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnp3_build_aob32(buf, DNP3_AOB_LEN - 1, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnp3_build_aob32(NULL, sizeof(buf), 0));
}

// A g41v3 Analog Output Block carries the IEEE 754 binary32 encoding little-endian. 1.0 is sign 0,
// biased exponent 127 (0x7F), zero significand: 0 01111111 0000... = 0x3F800000. -2.0 is sign 1,
// biased exponent 128 (0x80), zero significand: 1 10000000 0000... = 0xC0000000.
void test_analog_output_block_float(void)
{
    uint8_t buf[8];
    TEST_ASSERT_EQUAL_UINT(DNP3_AOB_LEN, protocore_dnp3_build_aob_float(buf, sizeof(buf), 1.0f));
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x80u, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x3Fu, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[4]);

    TEST_ASSERT_EQUAL_UINT(DNP3_AOB_LEN, protocore_dnp3_build_aob_float(buf, sizeof(buf), -2.0f));
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0xC0u, buf[3]);

    TEST_ASSERT_EQUAL_UINT(DNP3_AOB_LEN, protocore_dnp3_build_aob_float(buf, sizeof(buf), 0.0f));
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[3]);

    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnp3_build_aob_float(buf, DNP3_AOB_LEN - 1, 1.0f));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnp3_build_aob_float(NULL, sizeof(buf), 1.0f));
}
