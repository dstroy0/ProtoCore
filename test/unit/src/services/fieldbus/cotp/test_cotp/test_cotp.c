// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the TPKT + COTP class-0 codec (services/fieldbus/cotp/cotp.h).
//
// The load-bearing case is test_smallest_tpkt_is_seven_octets. RFC 1006 sec 6 fixes the TPKT
// header - "vrsn ... is always 3", "packet length 16 bits (min=7, max=65535) ... contains the
// length of entire packet in octets, including packet-header" - and that published minimum of 7 is
// only reachable if the 4-octet TPKT header wraps a 3-octet X.224 class-0 Data TPDU with no user
// data. Reproducing 03 00 00 07 02 F0 80 therefore pins the version octet, the big-endian
// self-inclusive length, and the DT header width all at once; a length that forgot to count its own
// header would read 3 and the peer would resynchronize on garbage.

#include "services/fieldbus/cotp/cotp.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// RFC 1006 sec 6 and the ISO 8073 X.224 TPDU codes.
void test_published_constants(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x03u, TPKT_VERSION);
    TEST_ASSERT_EQUAL_INT(4, TPKT_HEADER_SIZE);
    TEST_ASSERT_EQUAL_HEX8(0xF0u, COTP_DT);
    TEST_ASSERT_EQUAL_HEX8(0xE0u, COTP_CR);
    TEST_ASSERT_EQUAL_HEX8(0xD0u, COTP_CC);
    TEST_ASSERT_EQUAL_HEX8(0x80u, COTP_DR);
    TEST_ASSERT_EQUAL_HEX8(0xC0u, COTP_DC);
    TEST_ASSERT_EQUAL_HEX8(0x70u, COTP_ER);
    TEST_ASSERT_EQUAL_HEX8(0x80u, COTP_EOT);
    TEST_ASSERT_EQUAL_HEX8(0xC0u, COTP_PARAM_TPDU_SIZE);
    TEST_ASSERT_EQUAL_INT(3, COTP_DT_HEADER_LEN);
}

// RFC 1006's stated minimum packet length, built:
//   03       vrsn, always 3
//   00       reserved
//   00 07    packet length 7, big-endian, counting these four octets
//   02       LI = 2, the octets of TPDU header after LI
//   F0       DT
//   80       EOT set, TPDU-NR 0 (class 0 never numbers a TPDU)
void test_smallest_tpkt_is_seven_octets(void)
{
    static const uint8_t WANT[7] = {0x03, 0x00, 0x00, 0x07, 0x02, 0xF0, 0x80};

    uint8_t dt[8];
    size_t dlen = protocore_cotp_build_dt(dt, sizeof(dt), NULL, 0, PROTO_TRUE);
    TEST_ASSERT_EQUAL_size_t(3u, dlen);

    uint8_t frame[16];
    memset(frame, 0xEE, sizeof(frame));
    size_t flen = protocore_tpkt_build(frame, sizeof(frame), dt, dlen);
    TEST_ASSERT_EQUAL_size_t(7u, flen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, frame, 7);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, frame[7]);
}

// The TPKT length counts its own header, so a 4-octet payload gives 8 and the parser hands the
// payload back whole.
void test_tpkt_length_includes_the_header(void)
{
    static const uint8_t PAYLOAD[4] = {0x02, 0xF0, 0x80, 0x32};
    uint8_t frame[16];
    size_t n = protocore_tpkt_build(frame, sizeof(frame), PAYLOAD, sizeof(PAYLOAD));
    TEST_ASSERT_EQUAL_size_t(8u, n);
    TEST_ASSERT_EQUAL_HEX8(0x00u, frame[2]);
    TEST_ASSERT_EQUAL_HEX8(0x08u, frame[3]);

    const uint8_t *slice = NULL;
    size_t slice_len = 0, consumed = 0;
    TEST_ASSERT_TRUE(protocore_tpkt_parse(frame, n, &slice, &slice_len, &consumed));
    TEST_ASSERT_EQUAL_size_t(4u, slice_len);
    TEST_ASSERT_EQUAL_size_t(8u, consumed);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, slice, 4);
}

// TCP delivers a stream with no message boundaries, so two TPKTs can arrive in one read. The
// consumed count is what advances the reader to the second one.
void test_consumed_advances_past_one_packet_in_a_stream(void)
{
    static const uint8_t A[3] = {0x02, 0xF0, 0x80};
    static const uint8_t B[5] = {0x02, 0xF0, 0x80, 0xAA, 0xBB};
    uint8_t stream[32];
    size_t n = protocore_tpkt_build(stream, sizeof(stream), A, sizeof(A));
    size_t m = protocore_tpkt_build(stream + n, sizeof(stream) - n, B, sizeof(B));
    TEST_ASSERT_EQUAL_size_t(7u, n);
    TEST_ASSERT_EQUAL_size_t(9u, m);

    const uint8_t *slice;
    size_t slice_len, consumed;
    TEST_ASSERT_TRUE(protocore_tpkt_parse(stream, n + m, &slice, &slice_len, &consumed));
    TEST_ASSERT_EQUAL_size_t(7u, consumed); // trimmed to the first packet, not the whole read
    TEST_ASSERT_EQUAL_size_t(3u, slice_len);

    TEST_ASSERT_TRUE(protocore_tpkt_parse(stream + consumed, n + m - consumed, &slice, &slice_len, &consumed));
    TEST_ASSERT_EQUAL_size_t(9u, consumed);
    TEST_ASSERT_EQUAL_size_t(5u, slice_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(B, slice, 5);
}

// A packet not yet fully buffered is refused rather than reported short: the reader must wait for
// the rest instead of handing a truncated TPDU up.
void test_tpkt_refusals(void)
{
    uint8_t frame[16] = {0x03, 0x00, 0x00, 0x08, 0x02, 0xF0, 0x80, 0x32};
    const uint8_t *slice;
    size_t slice_len, consumed;

    TEST_ASSERT_TRUE(protocore_tpkt_parse(frame, 8, &slice, &slice_len, &consumed));
    TEST_ASSERT_FALSE(protocore_tpkt_parse(frame, 7, &slice, &slice_len, &consumed)); // one octet short

    frame[0] = 0x04; // only version 3 is defined
    TEST_ASSERT_FALSE(protocore_tpkt_parse(frame, 8, &slice, &slice_len, &consumed));
    frame[0] = 0x03;

    frame[3] = 0x03; // a length below the header size cannot be right
    TEST_ASSERT_FALSE(protocore_tpkt_parse(frame, 8, &slice, &slice_len, &consumed));

    TEST_ASSERT_FALSE(protocore_tpkt_parse(frame, 3, &slice, &slice_len, &consumed));
    TEST_ASSERT_FALSE(protocore_tpkt_parse(NULL, 8, &slice, &slice_len, &consumed));

    uint8_t out[8];
    static const uint8_t PAYLOAD[5] = {1, 2, 3, 4, 5};
    TEST_ASSERT_EQUAL_size_t(0u, protocore_tpkt_build(out, 8, PAYLOAD, sizeof(PAYLOAD))); // 4+5 > 8
    TEST_ASSERT_EQUAL_size_t(0u, protocore_tpkt_build(NULL, sizeof(out), PAYLOAD, sizeof(PAYLOAD)));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_tpkt_build(out, sizeof(out), NULL, 2));
}

// A DT TPDU: LI counts the octets after itself, so LI = 2 for the code plus the EOT/TPDU-NR octet,
// and the user data follows the 3-octet header.
void test_data_tpdu_layout(void)
{
    static const uint8_t DATA[4] = {0x32, 0x01, 0x00, 0x00};
    uint8_t buf[16];
    size_t n = protocore_cotp_build_dt(buf, sizeof(buf), DATA, sizeof(DATA), PROTO_TRUE);
    static const uint8_t WANT[7] = {0x02, 0xF0, 0x80, 0x32, 0x01, 0x00, 0x00};
    TEST_ASSERT_EQUAL_size_t(7u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 7);

    CotpHeader h;
    TEST_ASSERT_TRUE(protocore_cotp_parse(buf, n, &h));
    TEST_ASSERT_EQUAL_HEX8(COTP_DT, h.code);
    TEST_ASSERT_TRUE(h.eot);
    TEST_ASSERT_EQUAL_size_t(4u, h.data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, h.data, 4);

    // Without EOT the third octet is 0: this TPDU is not the end of the TSDU.
    n = protocore_cotp_build_dt(buf, sizeof(buf), DATA, sizeof(DATA), PROTO_FALSE);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[2]);
    TEST_ASSERT_TRUE(protocore_cotp_parse(buf, n, &h));
    TEST_ASSERT_FALSE(h.eot);

    TEST_ASSERT_EQUAL_size_t(0u, protocore_cotp_build_dt(buf, 2, DATA, sizeof(DATA), PROTO_TRUE));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cotp_build_dt(NULL, sizeof(buf), DATA, sizeof(DATA), PROTO_TRUE));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cotp_build_dt(buf, sizeof(buf), NULL, 4, PROTO_TRUE));
}

// A Connection Request: LI, code, destination reference (0 - unknown on a request), source
// reference, class option (0), then the TPDU-size parameter as code / length / exponent. With no
// extra parameters LI = 1 + 2 + 2 + 1 + 3 = 9, so the TPDU is 10 octets.
void test_connection_request_layout(void)
{
    uint8_t buf[32];
    size_t n = protocore_cotp_build_cr(buf, sizeof(buf), 0x0001u, 0x0Au, NULL, 0);
    static const uint8_t WANT[10] = {0x09, 0xE0, 0x00, 0x00, 0x00, 0x01, 0x00, 0xC0, 0x01, 0x0A};
    TEST_ASSERT_EQUAL_size_t(10u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 10);

    // The TPDU-size parameter's value is an exponent: 0x0A means 2^10 = 1024 octets.
    TEST_ASSERT_EQUAL_UINT32(1024u, 1u << buf[9]);

    CotpHeader h;
    TEST_ASSERT_TRUE(protocore_cotp_parse(buf, n, &h));
    TEST_ASSERT_EQUAL_HEX8(COTP_CR, h.code);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, h.dst_ref);
    TEST_ASSERT_EQUAL_HEX16(0x0001u, h.src_ref);

    // The references are two-octet fields in network order, so 0x1234 lays down 12 34.
    n = protocore_cotp_build_cr(buf, sizeof(buf), 0x1234u, 0x0Au, NULL, 0);
    TEST_ASSERT_EQUAL_HEX8(0x12u, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x34u, buf[5]);
    TEST_ASSERT_TRUE(protocore_cotp_parse(buf, n, &h));
    TEST_ASSERT_EQUAL_HEX16(0x1234u, h.src_ref);
}

// Extra variable parameters ride after the TPDU-size parameter and are counted in LI. The S7
// connection setup appends the source and destination TSAP parameters (codes 0xC1 and 0xC2), which
// takes LI to 9 + 8 = 17 and the whole TPKT to 4 + 18 = 22 octets.
void test_connection_request_with_tsap_parameters(void)
{
    static const uint8_t TSAPS[8] = {0xC1, 0x02, 0x01, 0x00, 0xC2, 0x02, 0x01, 0x02};
    uint8_t cr[32];
    size_t n = protocore_cotp_build_cr(cr, sizeof(cr), 0x0001u, 0x0Au, TSAPS, sizeof(TSAPS));
    TEST_ASSERT_EQUAL_size_t(18u, n);
    TEST_ASSERT_EQUAL_HEX8(0x11u, cr[0]); // LI = 17
    TEST_ASSERT_EQUAL_HEX8_ARRAY(TSAPS, cr + 10, 8);

    uint8_t frame[64];
    size_t flen = protocore_tpkt_build(frame, sizeof(frame), cr, n);
    TEST_ASSERT_EQUAL_size_t(22u, flen);
    static const uint8_t WANT[22] = {0x03, 0x00, 0x00, 0x16, 0x11, 0xE0, 0x00, 0x00, 0x00, 0x01, 0x00,
                                     0xC0, 0x01, 0x0A, 0xC1, 0x02, 0x01, 0x00, 0xC2, 0x02, 0x01, 0x02};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, frame, 22);

    // Parsed back out of the stream.
    const uint8_t *slice;
    size_t slice_len, consumed;
    TEST_ASSERT_TRUE(protocore_tpkt_parse(frame, flen, &slice, &slice_len, &consumed));
    CotpHeader h;
    TEST_ASSERT_TRUE(protocore_cotp_parse(slice, slice_len, &h));
    TEST_ASSERT_EQUAL_HEX8(COTP_CR, h.code);
    TEST_ASSERT_EQUAL_HEX16(0x0001u, h.src_ref);
}

// The Connection Confirm echoes the peer's source reference as its destination reference, so a
// server that swaps the two hands the client a connection it cannot match to its request.
void test_connection_confirm_echoes_the_peer_reference(void)
{
    uint8_t buf[32];
    size_t n = protocore_cotp_build_cc(buf, sizeof(buf), 0x1234u, 0xABCDu, 0x0Au, NULL, 0);
    static const uint8_t WANT[10] = {0x09, 0xD0, 0x12, 0x34, 0xAB, 0xCD, 0x00, 0xC0, 0x01, 0x0A};
    TEST_ASSERT_EQUAL_size_t(10u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 10);

    CotpHeader h;
    TEST_ASSERT_TRUE(protocore_cotp_parse(buf, n, &h));
    TEST_ASSERT_EQUAL_HEX8(COTP_CC, h.code);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, h.dst_ref);
    TEST_ASSERT_EQUAL_HEX16(0xABCDu, h.src_ref);

    // A CR's src-ref becomes the CC's dst-ref: the two halves of one connection.
    uint8_t cr[32];
    TEST_ASSERT_EQUAL_size_t(10u, protocore_cotp_build_cr(cr, sizeof(cr), 0x0055u, 0x0Au, NULL, 0));
    CotpHeader req;
    TEST_ASSERT_TRUE(protocore_cotp_parse(cr, 10, &req));
    n = protocore_cotp_build_cc(buf, sizeof(buf), req.src_ref, 0x0077u, 0x0Au, NULL, 0);
    TEST_ASSERT_TRUE(protocore_cotp_parse(buf, n, &h));
    TEST_ASSERT_EQUAL_HEX16(req.src_ref, h.dst_ref);

    TEST_ASSERT_EQUAL_size_t(0u, protocore_cotp_build_cc(buf, 9, 1, 2, 0x0Au, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cotp_build_cc(NULL, sizeof(buf), 1, 2, 0x0Au, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cotp_build_cc(buf, sizeof(buf), 1, 2, 0x0Au, NULL, 4));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cotp_build_cr(buf, 9, 1, 0x0Au, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cotp_build_cr(NULL, sizeof(buf), 1, 0x0Au, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_cotp_build_cr(buf, sizeof(buf), 1, 0x0Au, NULL, 4));
}

// The TPDU type is the high nibble of the code octet; the low nibble is the credit, which class 0
// leaves at zero. Types this codec does not decode are still reported by type.
void test_type_is_the_high_nibble(void)
{
    CotpHeader h;

    static const uint8_t DR[7] = {0x06, 0x80, 0x00, 0x01, 0x00, 0x02, 0x00}; // Disconnect Request
    TEST_ASSERT_TRUE(protocore_cotp_parse(DR, sizeof(DR), &h));
    TEST_ASSERT_EQUAL_HEX8(COTP_DR, h.code);
    TEST_ASSERT_EQUAL_size_t(0u, h.data_len);
    TEST_ASSERT_NULL(h.data);

    static const uint8_t ER[5] = {0x04, 0x70, 0x00, 0x00, 0x01}; // TPDU Error
    TEST_ASSERT_TRUE(protocore_cotp_parse(ER, sizeof(ER), &h));
    TEST_ASSERT_EQUAL_HEX8(COTP_ER, h.code);

    // A credit in the low nibble does not change the type.
    static const uint8_t DT_WITH_CDT[4] = {0x02, 0xF5, 0x80, 0xAA};
    TEST_ASSERT_TRUE(protocore_cotp_parse(DT_WITH_CDT, sizeof(DT_WITH_CDT), &h));
    TEST_ASSERT_EQUAL_HEX8(COTP_DT, h.code);
    TEST_ASSERT_EQUAL_size_t(1u, h.data_len);
}

// An LI that does not fit the buffer, or that is too small for the TPDU it claims to be, is
// refused rather than read past.
void test_cotp_refusals(void)
{
    CotpHeader h;

    static const uint8_t LI_ZERO[3] = {0x00, 0xF0, 0x80};
    TEST_ASSERT_FALSE(protocore_cotp_parse(LI_ZERO, sizeof(LI_ZERO), &h));

    static const uint8_t LI_TOO_BIG[3] = {0x20, 0xF0, 0x80}; // header of 33 octets in a 3-octet TPDU
    TEST_ASSERT_FALSE(protocore_cotp_parse(LI_TOO_BIG, sizeof(LI_TOO_BIG), &h));

    static const uint8_t DT_SHORT[2] = {0x01, 0xF0}; // DT needs the EOT/TPDU-NR octet
    TEST_ASSERT_FALSE(protocore_cotp_parse(DT_SHORT, sizeof(DT_SHORT), &h));

    static const uint8_t CR_SHORT[6] = {0x05, 0xE0, 0x00, 0x00, 0x00, 0x01}; // CR needs LI >= 6
    TEST_ASSERT_FALSE(protocore_cotp_parse(CR_SHORT, sizeof(CR_SHORT), &h));

    static const uint8_t ONE[1] = {0x02};
    TEST_ASSERT_FALSE(protocore_cotp_parse(ONE, sizeof(ONE), &h));
    TEST_ASSERT_FALSE(protocore_cotp_parse(NULL, 3, &h));
    TEST_ASSERT_FALSE(protocore_cotp_parse(LI_ZERO, 3, NULL));
}

// A payload framed as a DT inside a TPKT and taken apart again is the payload it started as: the
// whole "ISO transport on TCP" stack round-trips.
void test_stack_round_trip(void)
{
    static const uint8_t S7[10] = {0x32, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0E, 0x00, 0x00};
    uint8_t dt[32];
    size_t dlen = protocore_cotp_build_dt(dt, sizeof(dt), S7, sizeof(S7), PROTO_TRUE);
    uint8_t frame[64];
    size_t flen = protocore_tpkt_build(frame, sizeof(frame), dt, dlen);
    TEST_ASSERT_EQUAL_size_t(4u + 3u + 10u, flen);

    const uint8_t *slice;
    size_t slice_len, consumed;
    TEST_ASSERT_TRUE(protocore_tpkt_parse(frame, flen, &slice, &slice_len, &consumed));
    TEST_ASSERT_EQUAL_size_t(flen, consumed);

    CotpHeader h;
    TEST_ASSERT_TRUE(protocore_cotp_parse(slice, slice_len, &h));
    TEST_ASSERT_EQUAL_HEX8(COTP_DT, h.code);
    TEST_ASSERT_TRUE(h.eot);
    TEST_ASSERT_EQUAL_size_t(sizeof(S7), h.data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(S7, h.data, sizeof(S7));
}
