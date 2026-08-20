// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

static uint8_t cotp_work[16]; // the borrow an entry takes; Cotp never reads it

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
    CotpV.build_dt_args.buf = dt;
    CotpV.build_dt_args.cap = sizeof(dt);
    CotpV.build_dt_args.data = NULL;
    CotpV.build_dt_args.data_len = 0;
    CotpV.build_dt_args.eot = PROTO_TRUE;
    Cotp.build_dt(cotp_work);
    size_t dlen = CotpV.n;
    TEST_ASSERT_EQUAL_size_t(3u, dlen);

    uint8_t frame[16];
    memset(frame, 0xEE, sizeof(frame));
    CotpV.tpkt_build_args.buf = frame;
    CotpV.tpkt_build_args.cap = sizeof(frame);
    CotpV.tpkt_build_args.payload = dt;
    CotpV.tpkt_build_args.payload_len = dlen;
    Cotp.tpkt_build(cotp_work);
    size_t flen = CotpV.n;
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
    CotpV.tpkt_build_args.buf = frame;
    CotpV.tpkt_build_args.cap = sizeof(frame);
    CotpV.tpkt_build_args.payload = PAYLOAD;
    CotpV.tpkt_build_args.payload_len = sizeof(PAYLOAD);
    Cotp.tpkt_build(cotp_work);
    size_t n = CotpV.n;
    TEST_ASSERT_EQUAL_size_t(8u, n);
    TEST_ASSERT_EQUAL_HEX8(0x00u, frame[2]);
    TEST_ASSERT_EQUAL_HEX8(0x08u, frame[3]);

    const uint8_t *slice = NULL;
    size_t slice_len = 0, consumed = 0;
    CotpV.tpkt_parse_args.buf = frame;
    CotpV.tpkt_parse_args.len = n;
    CotpV.tpkt_parse_args.payload = &slice;
    CotpV.tpkt_parse_args.payload_len = &slice_len;
    CotpV.tpkt_parse_args.consumed = &consumed;
    Cotp.tpkt_parse(cotp_work);
    TEST_ASSERT_TRUE(CotpV.ok);
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
    CotpV.tpkt_build_args.buf = stream;
    CotpV.tpkt_build_args.cap = sizeof(stream);
    CotpV.tpkt_build_args.payload = A;
    CotpV.tpkt_build_args.payload_len = sizeof(A);
    Cotp.tpkt_build(cotp_work);
    size_t n = CotpV.n;
    CotpV.tpkt_build_args.buf = stream + n;
    CotpV.tpkt_build_args.cap = sizeof(stream) - n;
    CotpV.tpkt_build_args.payload = B;
    CotpV.tpkt_build_args.payload_len = sizeof(B);
    Cotp.tpkt_build(cotp_work);
    size_t m = CotpV.n;
    TEST_ASSERT_EQUAL_size_t(7u, n);
    TEST_ASSERT_EQUAL_size_t(9u, m);

    const uint8_t *slice;
    size_t slice_len, consumed;
    CotpV.tpkt_parse_args.buf = stream;
    CotpV.tpkt_parse_args.len = n + m;
    CotpV.tpkt_parse_args.payload = &slice;
    CotpV.tpkt_parse_args.payload_len = &slice_len;
    CotpV.tpkt_parse_args.consumed = &consumed;
    Cotp.tpkt_parse(cotp_work);
    TEST_ASSERT_TRUE(CotpV.ok);
    TEST_ASSERT_EQUAL_size_t(7u, consumed); // trimmed to the first packet, not the whole read
    TEST_ASSERT_EQUAL_size_t(3u, slice_len);

    CotpV.tpkt_parse_args.buf = stream + consumed;
    CotpV.tpkt_parse_args.len = n + m - consumed;
    CotpV.tpkt_parse_args.payload = &slice;
    CotpV.tpkt_parse_args.payload_len = &slice_len;
    CotpV.tpkt_parse_args.consumed = &consumed;
    Cotp.tpkt_parse(cotp_work);
    TEST_ASSERT_TRUE(CotpV.ok);
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

    CotpV.tpkt_parse_args.buf = frame;
    CotpV.tpkt_parse_args.len = 8;
    CotpV.tpkt_parse_args.payload = &slice;
    CotpV.tpkt_parse_args.payload_len = &slice_len;
    CotpV.tpkt_parse_args.consumed = &consumed;
    Cotp.tpkt_parse(cotp_work);
    TEST_ASSERT_TRUE(CotpV.ok);
    CotpV.tpkt_parse_args.buf = frame;
    CotpV.tpkt_parse_args.len = 7;
    CotpV.tpkt_parse_args.payload = &slice;
    CotpV.tpkt_parse_args.payload_len = &slice_len;
    CotpV.tpkt_parse_args.consumed = &consumed;
    Cotp.tpkt_parse(cotp_work);
    TEST_ASSERT_FALSE(CotpV.ok); // one octet short

    frame[0] = 0x04; // only version 3 is defined
    CotpV.tpkt_parse_args.buf = frame;
    CotpV.tpkt_parse_args.len = 8;
    CotpV.tpkt_parse_args.payload = &slice;
    CotpV.tpkt_parse_args.payload_len = &slice_len;
    CotpV.tpkt_parse_args.consumed = &consumed;
    Cotp.tpkt_parse(cotp_work);
    TEST_ASSERT_FALSE(CotpV.ok);
    frame[0] = 0x03;

    frame[3] = 0x03; // a length below the header size cannot be right
    CotpV.tpkt_parse_args.buf = frame;
    CotpV.tpkt_parse_args.len = 8;
    CotpV.tpkt_parse_args.payload = &slice;
    CotpV.tpkt_parse_args.payload_len = &slice_len;
    CotpV.tpkt_parse_args.consumed = &consumed;
    Cotp.tpkt_parse(cotp_work);
    TEST_ASSERT_FALSE(CotpV.ok);

    CotpV.tpkt_parse_args.buf = frame;
    CotpV.tpkt_parse_args.len = 3;
    CotpV.tpkt_parse_args.payload = &slice;
    CotpV.tpkt_parse_args.payload_len = &slice_len;
    CotpV.tpkt_parse_args.consumed = &consumed;
    Cotp.tpkt_parse(cotp_work);
    TEST_ASSERT_FALSE(CotpV.ok);
    CotpV.tpkt_parse_args.buf = NULL;
    CotpV.tpkt_parse_args.len = 8;
    CotpV.tpkt_parse_args.payload = &slice;
    CotpV.tpkt_parse_args.payload_len = &slice_len;
    CotpV.tpkt_parse_args.consumed = &consumed;
    Cotp.tpkt_parse(cotp_work);
    TEST_ASSERT_FALSE(CotpV.ok);

    uint8_t out[8];
    static const uint8_t PAYLOAD[5] = {1, 2, 3, 4, 5};
    CotpV.tpkt_build_args.buf = out;
    CotpV.tpkt_build_args.cap = 8;
    CotpV.tpkt_build_args.payload = PAYLOAD;
    CotpV.tpkt_build_args.payload_len = sizeof(PAYLOAD);
    Cotp.tpkt_build(cotp_work);
    TEST_ASSERT_EQUAL_size_t(0u, CotpV.n); // 4+5 > 8
    CotpV.tpkt_build_args.buf = NULL;
    CotpV.tpkt_build_args.cap = sizeof(out);
    CotpV.tpkt_build_args.payload = PAYLOAD;
    CotpV.tpkt_build_args.payload_len = sizeof(PAYLOAD);
    Cotp.tpkt_build(cotp_work);
    TEST_ASSERT_EQUAL_size_t(0u, CotpV.n);
    CotpV.tpkt_build_args.buf = out;
    CotpV.tpkt_build_args.cap = sizeof(out);
    CotpV.tpkt_build_args.payload = NULL;
    CotpV.tpkt_build_args.payload_len = 2;
    Cotp.tpkt_build(cotp_work);
    TEST_ASSERT_EQUAL_size_t(0u, CotpV.n);
}

// A DT TPDU: LI counts the octets after itself, so LI = 2 for the code plus the EOT/TPDU-NR octet,
// and the user data follows the 3-octet header.
void test_data_tpdu_layout(void)
{
    static const uint8_t DATA[4] = {0x32, 0x01, 0x00, 0x00};
    uint8_t buf[16];
    CotpV.build_dt_args.buf = buf;
    CotpV.build_dt_args.cap = sizeof(buf);
    CotpV.build_dt_args.data = DATA;
    CotpV.build_dt_args.data_len = sizeof(DATA);
    CotpV.build_dt_args.eot = PROTO_TRUE;
    Cotp.build_dt(cotp_work);
    size_t n = CotpV.n;
    static const uint8_t WANT[7] = {0x02, 0xF0, 0x80, 0x32, 0x01, 0x00, 0x00};
    TEST_ASSERT_EQUAL_size_t(7u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 7);

    CotpHeader h;
    CotpV.parse_args.buf = buf;
    CotpV.parse_args.len = n;
    CotpV.parse_args.out = &h;
    Cotp.parse(cotp_work);
    TEST_ASSERT_TRUE(CotpV.ok);
    TEST_ASSERT_EQUAL_HEX8(COTP_DT, h.code);
    TEST_ASSERT_TRUE(h.eot);
    TEST_ASSERT_EQUAL_size_t(4u, h.data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, h.data, 4);

    // Without EOT the third octet is 0: this TPDU is not the end of the TSDU.
    CotpV.build_dt_args.buf = buf;
    CotpV.build_dt_args.cap = sizeof(buf);
    CotpV.build_dt_args.data = DATA;
    CotpV.build_dt_args.data_len = sizeof(DATA);
    CotpV.build_dt_args.eot = PROTO_FALSE;
    Cotp.build_dt(cotp_work);
    n = CotpV.n;
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[2]);
    CotpV.parse_args.buf = buf;
    CotpV.parse_args.len = n;
    CotpV.parse_args.out = &h;
    Cotp.parse(cotp_work);
    TEST_ASSERT_TRUE(CotpV.ok);
    TEST_ASSERT_FALSE(h.eot);

    CotpV.build_dt_args.buf = buf;
    CotpV.build_dt_args.cap = 2;
    CotpV.build_dt_args.data = DATA;
    CotpV.build_dt_args.data_len = sizeof(DATA);
    CotpV.build_dt_args.eot = PROTO_TRUE;
    Cotp.build_dt(cotp_work);
    TEST_ASSERT_EQUAL_size_t(0u, CotpV.n);
    CotpV.build_dt_args.buf = NULL;
    CotpV.build_dt_args.cap = sizeof(buf);
    CotpV.build_dt_args.data = DATA;
    CotpV.build_dt_args.data_len = sizeof(DATA);
    CotpV.build_dt_args.eot = PROTO_TRUE;
    Cotp.build_dt(cotp_work);
    TEST_ASSERT_EQUAL_size_t(0u, CotpV.n);
    CotpV.build_dt_args.buf = buf;
    CotpV.build_dt_args.cap = sizeof(buf);
    CotpV.build_dt_args.data = NULL;
    CotpV.build_dt_args.data_len = 4;
    CotpV.build_dt_args.eot = PROTO_TRUE;
    Cotp.build_dt(cotp_work);
    TEST_ASSERT_EQUAL_size_t(0u, CotpV.n);
}

// A Connection Request: LI, code, destination reference (0 - unknown on a request), source
// reference, class option (0), then the TPDU-size parameter as code / length / exponent. With no
// extra parameters LI = 1 + 2 + 2 + 1 + 3 = 9, so the TPDU is 10 octets.
void test_connection_request_layout(void)
{
    uint8_t buf[32];
    CotpV.build_cr_args.buf = buf;
    CotpV.build_cr_args.cap = sizeof(buf);
    CotpV.build_cr_args.src_ref = 0x0001u;
    CotpV.build_cr_args.tpdu_size_code = 0x0Au;
    CotpV.build_cr_args.extra_params = NULL;
    CotpV.build_cr_args.extra_len = 0;
    Cotp.build_cr(cotp_work);
    size_t n = CotpV.n;
    static const uint8_t WANT[10] = {0x09, 0xE0, 0x00, 0x00, 0x00, 0x01, 0x00, 0xC0, 0x01, 0x0A};
    TEST_ASSERT_EQUAL_size_t(10u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 10);

    // The TPDU-size parameter's value is an exponent: 0x0A means 2^10 = 1024 octets.
    TEST_ASSERT_EQUAL_UINT32(1024u, 1u << buf[9]);

    CotpHeader h;
    CotpV.parse_args.buf = buf;
    CotpV.parse_args.len = n;
    CotpV.parse_args.out = &h;
    Cotp.parse(cotp_work);
    TEST_ASSERT_TRUE(CotpV.ok);
    TEST_ASSERT_EQUAL_HEX8(COTP_CR, h.code);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, h.dst_ref);
    TEST_ASSERT_EQUAL_HEX16(0x0001u, h.src_ref);

    // The references are two-octet fields in network order, so 0x1234 lays down 12 34.
    CotpV.build_cr_args.buf = buf;
    CotpV.build_cr_args.cap = sizeof(buf);
    CotpV.build_cr_args.src_ref = 0x1234u;
    CotpV.build_cr_args.tpdu_size_code = 0x0Au;
    CotpV.build_cr_args.extra_params = NULL;
    CotpV.build_cr_args.extra_len = 0;
    Cotp.build_cr(cotp_work);
    n = CotpV.n;
    TEST_ASSERT_EQUAL_HEX8(0x12u, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x34u, buf[5]);
    CotpV.parse_args.buf = buf;
    CotpV.parse_args.len = n;
    CotpV.parse_args.out = &h;
    Cotp.parse(cotp_work);
    TEST_ASSERT_TRUE(CotpV.ok);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, h.src_ref);
}

// Extra variable parameters ride after the TPDU-size parameter and are counted in LI. The S7
// connection setup appends the source and destination TSAP parameters (codes 0xC1 and 0xC2), which
// takes LI to 9 + 8 = 17 and the whole TPKT to 4 + 18 = 22 octets.
void test_connection_request_with_tsap_parameters(void)
{
    static const uint8_t TSAPS[8] = {0xC1, 0x02, 0x01, 0x00, 0xC2, 0x02, 0x01, 0x02};
    uint8_t cr[32];
    CotpV.build_cr_args.buf = cr;
    CotpV.build_cr_args.cap = sizeof(cr);
    CotpV.build_cr_args.src_ref = 0x0001u;
    CotpV.build_cr_args.tpdu_size_code = 0x0Au;
    CotpV.build_cr_args.extra_params = TSAPS;
    CotpV.build_cr_args.extra_len = sizeof(TSAPS);
    Cotp.build_cr(cotp_work);
    size_t n = CotpV.n;
    TEST_ASSERT_EQUAL_size_t(18u, n);
    TEST_ASSERT_EQUAL_HEX8(0x11u, cr[0]); // LI = 17
    TEST_ASSERT_EQUAL_HEX8_ARRAY(TSAPS, cr + 10, 8);

    uint8_t frame[64];
    CotpV.tpkt_build_args.buf = frame;
    CotpV.tpkt_build_args.cap = sizeof(frame);
    CotpV.tpkt_build_args.payload = cr;
    CotpV.tpkt_build_args.payload_len = n;
    Cotp.tpkt_build(cotp_work);
    size_t flen = CotpV.n;
    TEST_ASSERT_EQUAL_size_t(22u, flen);
    static const uint8_t WANT[22] = {0x03, 0x00, 0x00, 0x16, 0x11, 0xE0, 0x00, 0x00, 0x00, 0x01, 0x00,
                                     0xC0, 0x01, 0x0A, 0xC1, 0x02, 0x01, 0x00, 0xC2, 0x02, 0x01, 0x02};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, frame, 22);

    // Parsed back out of the stream.
    const uint8_t *slice;
    size_t slice_len, consumed;
    CotpV.tpkt_parse_args.buf = frame;
    CotpV.tpkt_parse_args.len = flen;
    CotpV.tpkt_parse_args.payload = &slice;
    CotpV.tpkt_parse_args.payload_len = &slice_len;
    CotpV.tpkt_parse_args.consumed = &consumed;
    Cotp.tpkt_parse(cotp_work);
    TEST_ASSERT_TRUE(CotpV.ok);
    CotpHeader h;
    CotpV.parse_args.buf = slice;
    CotpV.parse_args.len = slice_len;
    CotpV.parse_args.out = &h;
    Cotp.parse(cotp_work);
    TEST_ASSERT_TRUE(CotpV.ok);
    TEST_ASSERT_EQUAL_HEX8(COTP_CR, h.code);
    TEST_ASSERT_EQUAL_HEX16(0x0001u, h.src_ref);
}

// The Connection Confirm echoes the peer's source reference as its destination reference, so a
// server that swaps the two hands the client a connection it cannot match to its request.
void test_connection_confirm_echoes_the_peer_reference(void)
{
    uint8_t buf[32];
    CotpV.build_cc_args.buf = buf;
    CotpV.build_cc_args.cap = sizeof(buf);
    CotpV.build_cc_args.dst_ref = 0x1234u;
    CotpV.build_cc_args.src_ref = 0xABCDu;
    CotpV.build_cc_args.tpdu_size_code = 0x0Au;
    CotpV.build_cc_args.extra_params = NULL;
    CotpV.build_cc_args.extra_len = 0;
    Cotp.build_cc(cotp_work);
    size_t n = CotpV.n;
    static const uint8_t WANT[10] = {0x09, 0xD0, 0x12, 0x34, 0xAB, 0xCD, 0x00, 0xC0, 0x01, 0x0A};
    TEST_ASSERT_EQUAL_size_t(10u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 10);

    CotpHeader h;
    CotpV.parse_args.buf = buf;
    CotpV.parse_args.len = n;
    CotpV.parse_args.out = &h;
    Cotp.parse(cotp_work);
    TEST_ASSERT_TRUE(CotpV.ok);
    TEST_ASSERT_EQUAL_HEX8(COTP_CC, h.code);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, h.dst_ref);
    TEST_ASSERT_EQUAL_HEX16(0xABCDu, h.src_ref);

    // A CR's src-ref becomes the CC's dst-ref: the two halves of one connection.
    uint8_t cr[32];
    CotpV.build_cr_args.buf = cr;
    CotpV.build_cr_args.cap = sizeof(cr);
    CotpV.build_cr_args.src_ref = 0x0055u;
    CotpV.build_cr_args.tpdu_size_code = 0x0Au;
    CotpV.build_cr_args.extra_params = NULL;
    CotpV.build_cr_args.extra_len = 0;
    Cotp.build_cr(cotp_work);
    TEST_ASSERT_EQUAL_size_t(10u, CotpV.n);
    CotpHeader req;
    CotpV.parse_args.buf = cr;
    CotpV.parse_args.len = 10;
    CotpV.parse_args.out = &req;
    Cotp.parse(cotp_work);
    TEST_ASSERT_TRUE(CotpV.ok);
    CotpV.build_cc_args.buf = buf;
    CotpV.build_cc_args.cap = sizeof(buf);
    CotpV.build_cc_args.dst_ref = req.src_ref;
    CotpV.build_cc_args.src_ref = 0x0077u;
    CotpV.build_cc_args.tpdu_size_code = 0x0Au;
    CotpV.build_cc_args.extra_params = NULL;
    CotpV.build_cc_args.extra_len = 0;
    Cotp.build_cc(cotp_work);
    n = CotpV.n;
    CotpV.parse_args.buf = buf;
    CotpV.parse_args.len = n;
    CotpV.parse_args.out = &h;
    Cotp.parse(cotp_work);
    TEST_ASSERT_TRUE(CotpV.ok);
    TEST_ASSERT_EQUAL_HEX16(req.src_ref, h.dst_ref);

    CotpV.build_cc_args.buf = buf;
    CotpV.build_cc_args.cap = 9;
    CotpV.build_cc_args.dst_ref = 1;
    CotpV.build_cc_args.src_ref = 2;
    CotpV.build_cc_args.tpdu_size_code = 0x0Au;
    CotpV.build_cc_args.extra_params = NULL;
    CotpV.build_cc_args.extra_len = 0;
    Cotp.build_cc(cotp_work);
    TEST_ASSERT_EQUAL_size_t(0u, CotpV.n);
    CotpV.build_cc_args.buf = NULL;
    CotpV.build_cc_args.cap = sizeof(buf);
    CotpV.build_cc_args.dst_ref = 1;
    CotpV.build_cc_args.src_ref = 2;
    CotpV.build_cc_args.tpdu_size_code = 0x0Au;
    CotpV.build_cc_args.extra_params = NULL;
    CotpV.build_cc_args.extra_len = 0;
    Cotp.build_cc(cotp_work);
    TEST_ASSERT_EQUAL_size_t(0u, CotpV.n);
    CotpV.build_cc_args.buf = buf;
    CotpV.build_cc_args.cap = sizeof(buf);
    CotpV.build_cc_args.dst_ref = 1;
    CotpV.build_cc_args.src_ref = 2;
    CotpV.build_cc_args.tpdu_size_code = 0x0Au;
    CotpV.build_cc_args.extra_params = NULL;
    CotpV.build_cc_args.extra_len = 4;
    Cotp.build_cc(cotp_work);
    TEST_ASSERT_EQUAL_size_t(0u, CotpV.n);
    CotpV.build_cr_args.buf = buf;
    CotpV.build_cr_args.cap = 9;
    CotpV.build_cr_args.src_ref = 1;
    CotpV.build_cr_args.tpdu_size_code = 0x0Au;
    CotpV.build_cr_args.extra_params = NULL;
    CotpV.build_cr_args.extra_len = 0;
    Cotp.build_cr(cotp_work);
    TEST_ASSERT_EQUAL_size_t(0u, CotpV.n);
    CotpV.build_cr_args.buf = NULL;
    CotpV.build_cr_args.cap = sizeof(buf);
    CotpV.build_cr_args.src_ref = 1;
    CotpV.build_cr_args.tpdu_size_code = 0x0Au;
    CotpV.build_cr_args.extra_params = NULL;
    CotpV.build_cr_args.extra_len = 0;
    Cotp.build_cr(cotp_work);
    TEST_ASSERT_EQUAL_size_t(0u, CotpV.n);
    CotpV.build_cr_args.buf = buf;
    CotpV.build_cr_args.cap = sizeof(buf);
    CotpV.build_cr_args.src_ref = 1;
    CotpV.build_cr_args.tpdu_size_code = 0x0Au;
    CotpV.build_cr_args.extra_params = NULL;
    CotpV.build_cr_args.extra_len = 4;
    Cotp.build_cr(cotp_work);
    TEST_ASSERT_EQUAL_size_t(0u, CotpV.n);
}

// The TPDU type is the high nibble of the code octet; the low nibble is the credit, which class 0
// leaves at zero. Types this codec does not decode are still reported by type.
void test_type_is_the_high_nibble(void)
{
    CotpHeader h;

    static const uint8_t DR[7] = {0x06, 0x80, 0x00, 0x01, 0x00, 0x02, 0x00}; // Disconnect Request
    CotpV.parse_args.buf = DR;
    CotpV.parse_args.len = sizeof(DR);
    CotpV.parse_args.out = &h;
    Cotp.parse(cotp_work);
    TEST_ASSERT_TRUE(CotpV.ok);
    TEST_ASSERT_EQUAL_HEX8(COTP_DR, h.code);
    TEST_ASSERT_EQUAL_size_t(0u, h.data_len);
    TEST_ASSERT_NULL(h.data);

    static const uint8_t ER[5] = {0x04, 0x70, 0x00, 0x00, 0x01}; // TPDU Error
    CotpV.parse_args.buf = ER;
    CotpV.parse_args.len = sizeof(ER);
    CotpV.parse_args.out = &h;
    Cotp.parse(cotp_work);
    TEST_ASSERT_TRUE(CotpV.ok);
    TEST_ASSERT_EQUAL_HEX8(COTP_ER, h.code);

    // A credit in the low nibble does not change the type.
    static const uint8_t DT_WITH_CDT[4] = {0x02, 0xF5, 0x80, 0xAA};
    CotpV.parse_args.buf = DT_WITH_CDT;
    CotpV.parse_args.len = sizeof(DT_WITH_CDT);
    CotpV.parse_args.out = &h;
    Cotp.parse(cotp_work);
    TEST_ASSERT_TRUE(CotpV.ok);
    TEST_ASSERT_EQUAL_HEX8(COTP_DT, h.code);
    TEST_ASSERT_EQUAL_size_t(1u, h.data_len);
}

// An LI that does not fit the buffer, or that is too small for the TPDU it claims to be, is
// refused rather than read past.
void test_cotp_refusals(void)
{
    CotpHeader h;

    static const uint8_t LI_ZERO[3] = {0x00, 0xF0, 0x80};
    CotpV.parse_args.buf = LI_ZERO;
    CotpV.parse_args.len = sizeof(LI_ZERO);
    CotpV.parse_args.out = &h;
    Cotp.parse(cotp_work);
    TEST_ASSERT_FALSE(CotpV.ok);

    static const uint8_t LI_TOO_BIG[3] = {0x20, 0xF0, 0x80}; // header of 33 octets in a 3-octet TPDU
    CotpV.parse_args.buf = LI_TOO_BIG;
    CotpV.parse_args.len = sizeof(LI_TOO_BIG);
    CotpV.parse_args.out = &h;
    Cotp.parse(cotp_work);
    TEST_ASSERT_FALSE(CotpV.ok);

    static const uint8_t DT_SHORT[2] = {0x01, 0xF0}; // DT needs the EOT/TPDU-NR octet
    CotpV.parse_args.buf = DT_SHORT;
    CotpV.parse_args.len = sizeof(DT_SHORT);
    CotpV.parse_args.out = &h;
    Cotp.parse(cotp_work);
    TEST_ASSERT_FALSE(CotpV.ok);

    static const uint8_t CR_SHORT[6] = {0x05, 0xE0, 0x00, 0x00, 0x00, 0x01}; // CR needs LI >= 6
    CotpV.parse_args.buf = CR_SHORT;
    CotpV.parse_args.len = sizeof(CR_SHORT);
    CotpV.parse_args.out = &h;
    Cotp.parse(cotp_work);
    TEST_ASSERT_FALSE(CotpV.ok);

    static const uint8_t ONE[1] = {0x02};
    CotpV.parse_args.buf = ONE;
    CotpV.parse_args.len = sizeof(ONE);
    CotpV.parse_args.out = &h;
    Cotp.parse(cotp_work);
    TEST_ASSERT_FALSE(CotpV.ok);
    CotpV.parse_args.buf = NULL;
    CotpV.parse_args.len = 3;
    CotpV.parse_args.out = &h;
    Cotp.parse(cotp_work);
    TEST_ASSERT_FALSE(CotpV.ok);
    CotpV.parse_args.buf = LI_ZERO;
    CotpV.parse_args.len = 3;
    CotpV.parse_args.out = NULL;
    Cotp.parse(cotp_work);
    TEST_ASSERT_FALSE(CotpV.ok);
}

// A payload framed as a DT inside a TPKT and taken apart again is the payload it started as: the
// whole "ISO transport on TCP" stack round-trips.
void test_stack_round_trip(void)
{
    static const uint8_t S7[10] = {0x32, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0E, 0x00, 0x00};
    uint8_t dt[32];
    CotpV.build_dt_args.buf = dt;
    CotpV.build_dt_args.cap = sizeof(dt);
    CotpV.build_dt_args.data = S7;
    CotpV.build_dt_args.data_len = sizeof(S7);
    CotpV.build_dt_args.eot = PROTO_TRUE;
    Cotp.build_dt(cotp_work);
    size_t dlen = CotpV.n;
    uint8_t frame[64];
    CotpV.tpkt_build_args.buf = frame;
    CotpV.tpkt_build_args.cap = sizeof(frame);
    CotpV.tpkt_build_args.payload = dt;
    CotpV.tpkt_build_args.payload_len = dlen;
    Cotp.tpkt_build(cotp_work);
    size_t flen = CotpV.n;
    TEST_ASSERT_EQUAL_size_t(4u + 3u + 10u, flen);

    const uint8_t *slice;
    size_t slice_len, consumed;
    CotpV.tpkt_parse_args.buf = frame;
    CotpV.tpkt_parse_args.len = flen;
    CotpV.tpkt_parse_args.payload = &slice;
    CotpV.tpkt_parse_args.payload_len = &slice_len;
    CotpV.tpkt_parse_args.consumed = &consumed;
    Cotp.tpkt_parse(cotp_work);
    TEST_ASSERT_TRUE(CotpV.ok);
    TEST_ASSERT_EQUAL_size_t(flen, consumed);

    CotpHeader h;
    CotpV.parse_args.buf = slice;
    CotpV.parse_args.len = slice_len;
    CotpV.parse_args.out = &h;
    Cotp.parse(cotp_work);
    TEST_ASSERT_TRUE(CotpV.ok);
    TEST_ASSERT_EQUAL_HEX8(COTP_DT, h.code);
    TEST_ASSERT_TRUE(h.eot);
    TEST_ASSERT_EQUAL_size_t(sizeof(S7), h.data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(S7, h.data, sizeof(S7));
}
