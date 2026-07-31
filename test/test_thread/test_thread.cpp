// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the Thread spinel / HDLC-lite framing codec (services/radio/thread): the FCS
// (CRC-16/X-25) against its catalog check value (0x906E), an encode -> decode round trip,
// the byte-stuffing of reserved bytes, and malformed framing (bad FCS, dangling escape,
// buffer-too-small, no flag). Pure host tests.
//
// The env sizes PC_THREAD_MAX_DATA = 64.

#include "services/radio/thread/thread.h"
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_fcs_x25_check_value()
{
    // CRC-16/X-25 (poly 0x8408, init 0xFFFF, reflected, xorout 0xFFFF) of "123456789" = 0x906E.
    const uint8_t check[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX16(0x906E, pc_spinel_fcs(check, 9));
}

void test_encode_decode_round_trip()
{
    // A tiny spinel frame: header (flag|iid|tid) + command (PROP_VALUE_GET) + property.
    const uint8_t payload[3] = {0x81, 0x02, 0x02};
    uint8_t frame[32];
    uint16_t n = pc_spinel_frame_encode(payload, 3, frame, sizeof(frame));
    TEST_ASSERT_GREATER_THAN_UINT16(0, n);
    TEST_ASSERT_EQUAL_HEX8(ThreadHdlc::HDLC_FLAG, frame[n - 1]);

    uint8_t pay[16];
    uint16_t plen = 0;
    int c = pc_spinel_frame_decode(frame, n, pay, sizeof(pay), &plen);
    TEST_ASSERT_EQUAL_INT((int)n, c);
    TEST_ASSERT_EQUAL_UINT16(3, plen);
    TEST_ASSERT_EQUAL_MEMORY(payload, pay, 3);
}

void test_byte_stuffing_round_trip()
{
    const uint8_t payload[4] = {0x7E, 0x7D, 0x11, 0x13}; // all reserved
    uint8_t frame[32];
    uint16_t n = pc_spinel_frame_encode(payload, 4, frame, sizeof(frame));
    TEST_ASSERT_GREATER_THAN_UINT16(0, n);
    for (uint16_t i = 0; i + 1 < n; i++) // no raw reserved byte in the body
    {
        TEST_ASSERT_NOT_EQUAL_HEX8(0x7E, frame[i]);
        TEST_ASSERT_NOT_EQUAL_HEX8(0x11, frame[i]);
        TEST_ASSERT_NOT_EQUAL_HEX8(0x13, frame[i]);
    }
    uint8_t pay[16];
    uint16_t plen = 0;
    TEST_ASSERT_EQUAL_INT((int)n, pc_spinel_frame_decode(frame, n, pay, sizeof(pay), &plen));
    TEST_ASSERT_EQUAL_UINT16(4, plen);
    TEST_ASSERT_EQUAL_MEMORY(payload, pay, 4);
}

void test_decode_needs_more_without_flag()
{
    const uint8_t partial[3] = {0x81, 0x02, 0x02};
    uint8_t pay[8];
    uint16_t plen = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_spinel_frame_decode(partial, sizeof(partial), pay, sizeof(pay), &plen));
}

void test_decode_rejects_bad_fcs()
{
    const uint8_t payload[3] = {0x81, 0x02, 0x02};
    uint8_t frame[32];
    uint16_t n = pc_spinel_frame_encode(payload, 3, frame, sizeof(frame));
    frame[0] ^= 0xFF; // corrupt the payload so the FCS no longer matches
    uint8_t pay[8];
    uint16_t plen = 0;
    TEST_ASSERT_EQUAL_INT(-1, pc_spinel_frame_decode(frame, n, pay, sizeof(pay), &plen));
}

void test_decode_rejects_dangling_escape()
{
    const uint8_t frame[2] = {ThreadHdlc::HDLC_ESCAPE, ThreadHdlc::HDLC_FLAG}; // escape with nothing before the flag
    uint8_t pay[8];
    uint16_t plen = 0;
    TEST_ASSERT_EQUAL_INT(-1, pc_spinel_frame_decode(frame, sizeof(frame), pay, sizeof(pay), &plen));
}

void test_decode_rejects_small_payload_buffer()
{
    const uint8_t payload[6] = {1, 2, 3, 4, 5, 6};
    uint8_t frame[32];
    uint16_t n = pc_spinel_frame_encode(payload, 6, frame, sizeof(frame));
    uint8_t tiny[3];
    uint16_t plen = 0;
    TEST_ASSERT_EQUAL_INT(-1, pc_spinel_frame_decode(frame, n, tiny, sizeof(tiny), &plen));
}

void test_encode_bounds()
{
    uint8_t data[80] = {0};
    uint8_t out[256];
    TEST_ASSERT_EQUAL_UINT16(0, pc_spinel_frame_encode(data, 65, out, sizeof(out))); // > MAX_DATA 64
    uint8_t small[3];
    TEST_ASSERT_EQUAL_UINT16(0, pc_spinel_frame_encode(data, 8, small, sizeof(small))); // will not fit
}

// --- Spinel command layer ---------------------------------------------------------------

void test_spinel_pack_uint_kats()
{
    uint8_t out[8];
    TEST_ASSERT_EQUAL_UINT8(1, pc_spinel_pack_uint(0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x00, out[0]);
    TEST_ASSERT_EQUAL_UINT8(1, pc_spinel_pack_uint(127, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x7F, out[0]);
    TEST_ASSERT_EQUAL_UINT8(2, pc_spinel_pack_uint(128, out, sizeof(out))); // 0x80 0x01
    TEST_ASSERT_EQUAL_HEX8(0x80, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, out[1]);
    TEST_ASSERT_EQUAL_UINT8(2, pc_spinel_pack_uint(1337, out, sizeof(out))); // 0xB9 0x0A
    TEST_ASSERT_EQUAL_HEX8(0xB9, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x0A, out[1]);
}

void test_spinel_pack_unpack_round_trip()
{
    const uint32_t values[5] = {0, 42, 127, 128, 200000};
    for (int i = 0; i < 5; i++)
    {
        uint8_t out[8];
        uint8_t n = pc_spinel_pack_uint(values[i], out, sizeof(out));
        TEST_ASSERT_GREATER_THAN_UINT8(0, n);
        uint32_t v = 0;
        int c = pc_spinel_unpack_uint(out, n, &v);
        TEST_ASSERT_EQUAL_INT((int)n, c);
        TEST_ASSERT_EQUAL_UINT32(values[i], v);
    }
}

void test_spinel_unpack_needs_more_and_overflow()
{
    const uint8_t truncated[2] = {0x80, 0x80}; // continuation with no terminator
    uint32_t v = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_spinel_unpack_uint(truncated, 2, &v)); // need more
    const uint8_t overflow[6] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
    TEST_ASSERT_EQUAL_INT(-1, pc_spinel_unpack_uint(overflow, 6, &v)); // > uint32
}

void test_spinel_command_build_parse_round_trip()
{
    const uint8_t val[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t buf[32];
    // header 0x81, CMD_PROP_VALUE_SET, a large property id (multi-byte packed), a value.
    uint16_t n = pc_spinel_command_build(0x81, SpinelCmd::SPINEL_CMD_PROP_VALUE_SET, 1337, val, 4, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN_UINT16(0, n);

    uint8_t header = 0;
    uint32_t cmd = 0, prop = 0;
    const uint8_t *v = nullptr;
    uint16_t vlen = 0;
    int off = pc_spinel_command_parse(buf, n, &header, &cmd, &prop, &v, &vlen);
    TEST_ASSERT_GREATER_THAN_INT(0, off);
    TEST_ASSERT_EQUAL_HEX8(0x81, header);
    TEST_ASSERT_EQUAL_UINT32(SpinelCmd::SPINEL_CMD_PROP_VALUE_SET, cmd);
    TEST_ASSERT_EQUAL_UINT32(1337, prop);
    TEST_ASSERT_EQUAL_UINT16(4, vlen);
    TEST_ASSERT_EQUAL_MEMORY(val, v, 4);
}

void test_spinel_command_through_hdlc()
{
    // The command payload rides inside an HDLC frame: build the command, frame it, decode
    // the frame, then parse the command back out - the full Thread codec stack.
    uint8_t payload[16];
    uint16_t plen = pc_spinel_command_build(0x82, SpinelCmd::SPINEL_CMD_PROP_VALUE_GET, 2 /*NCP_VERSION*/, nullptr, 0,
                                            payload, sizeof(payload));
    uint8_t frame[32];
    uint16_t fn = pc_spinel_frame_encode(payload, plen, frame, sizeof(frame));
    TEST_ASSERT_GREATER_THAN_UINT16(0, fn);

    uint8_t got[16];
    uint16_t glen = 0;
    TEST_ASSERT_EQUAL_INT((int)fn, pc_spinel_frame_decode(frame, fn, got, sizeof(got), &glen));
    uint8_t header = 0;
    uint32_t cmd = 0, prop = 0;
    const uint8_t *v = nullptr;
    uint16_t vlen = 0;
    TEST_ASSERT_GREATER_THAN_INT(0, pc_spinel_command_parse(got, glen, &header, &cmd, &prop, &v, &vlen));
    TEST_ASSERT_EQUAL_UINT32(SpinelCmd::SPINEL_CMD_PROP_VALUE_GET, cmd);
    TEST_ASSERT_EQUAL_UINT32(2, prop);
    TEST_ASSERT_EQUAL_UINT16(0, vlen);
}

void test_spinel_guards()
{
    uint8_t out[8];
    TEST_ASSERT_EQUAL_UINT8(0, pc_spinel_pack_uint(0x12345, out, 0)); // zero cap
    uint8_t trunc[1] = {0x81};                                        // continuation bit set, truncated
    uint32_t v = 0;
    TEST_ASSERT_TRUE(pc_spinel_unpack_uint(trunc, sizeof(trunc), &v) <= 0); // truncated -> non-positive
    uint8_t pay[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_UINT16(0, pc_spinel_frame_encode(pay, sizeof(pay), out, 2)); // overflow
    uint8_t fpay[8];
    uint16_t fl = 0;
    uint8_t short_frame[2] = {0x7E, 0x7E};
    TEST_ASSERT_EQUAL_INT(-1, pc_spinel_frame_decode(short_frame, sizeof(short_frame), fpay, sizeof(fpay), &fl));
}

void test_thread_more_guards()
{
    uint8_t out[64];

    // pack/unpack null-pointer guards.
    TEST_ASSERT_EQUAL_UINT8(0, pc_spinel_pack_uint(5, nullptr, 8));
    uint32_t v = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_spinel_unpack_uint(nullptr, 4, &v));

    // command_build guards: null out, cap < 1, value==null with a positive length.
    uint8_t val[2] = {0xAA, 0xBB};
    TEST_ASSERT_EQUAL_UINT16(0, pc_spinel_command_build(0x81, 1, 1, val, 2, nullptr, 8));
    TEST_ASSERT_EQUAL_UINT16(0, pc_spinel_command_build(0x81, 1, 1, val, 2, out, 0));
    TEST_ASSERT_EQUAL_UINT16(0, pc_spinel_command_build(0x81, 1, 1, nullptr, 2, out, 8));
    // command_build overflow at each stage: cmd pack, prop pack, value copy.
    TEST_ASSERT_EQUAL_UINT16(0, pc_spinel_command_build(0x81, 1, 1, nullptr, 0, out, 1));    // cmd pack (cap 0)
    TEST_ASSERT_EQUAL_UINT16(0, pc_spinel_command_build(0x81, 1337, 1, nullptr, 0, out, 3)); // prop pack
    TEST_ASSERT_EQUAL_UINT16(0, pc_spinel_command_build(0x81, 1, 1, val, 10, out, 5));       // value copy

    // command_parse guards: null payload, then a truncated cmd, then a truncated prop.
    uint8_t hdr = 0;
    uint32_t cmd = 0, prop = 0;
    const uint8_t *pv = nullptr;
    uint16_t pvl = 0;
    TEST_ASSERT_EQUAL_INT(-1, pc_spinel_command_parse(nullptr, 4, &hdr, &cmd, &prop, &pv, &pvl));
    const uint8_t tc[2] = {0x81, 0x80}; // header + cmd byte with continuation, no terminator
    TEST_ASSERT_EQUAL_INT(-1, pc_spinel_command_parse(tc, 2, &hdr, &cmd, &prop, &pv, &pvl));
    const uint8_t tp[3] = {0x81, 0x01, 0x80}; // header + valid cmd + truncated prop
    TEST_ASSERT_EQUAL_INT(-1, pc_spinel_command_parse(tp, 3, &hdr, &cmd, &prop, &pv, &pvl));

    // frame_encode: an escaped byte that overflows, an FCS byte that overflows, a flag that overflows.
    uint8_t resv[1] = {0x7E};
    TEST_ASSERT_EQUAL_UINT16(0, pc_spinel_frame_encode(resv, 1, out, 1)); // escape needs 2, cap 1
    uint8_t one[1] = {0x01};
    TEST_ASSERT_EQUAL_UINT16(0, pc_spinel_frame_encode(one, 1, out, 1)); // payload fits, FCS byte overflows
    uint8_t p4[4] = {0x01, 0x02, 0x03, 0x04};
    uint8_t full[32];
    uint16_t fulln = pc_spinel_frame_encode(p4, 4, full, sizeof(full));
    TEST_ASSERT_TRUE(fulln > 1);
    TEST_ASSERT_EQUAL_UINT16(0, pc_spinel_frame_encode(p4, 4, out, (uint16_t)(fulln - 1))); // only the flag can't fit

    // frame_decode: null raw, an over-long unstuffed run, and a real FCS mismatch.
    uint8_t pay[16];
    uint16_t pl = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_spinel_frame_decode(nullptr, 4, pay, sizeof(pay), &pl));
    uint8_t big[80];
    for (int i = 0; i < 70; i++)
    {
        big[i] = 0x00;
    }
    big[70] = ThreadHdlc::HDLC_FLAG;
    TEST_ASSERT_EQUAL_INT(-1, pc_spinel_frame_decode(big, 71, pay, sizeof(pay), &pl)); // unstuffed > scratch
    uint8_t p3[3] = {0x01, 0x02, 0x03};
    uint8_t fr[32];
    uint16_t frn = pc_spinel_frame_encode(p3, 3, fr, sizeof(fr));
    fr[0] ^= 0x01; // 0x01 -> 0x00 (not flag/escape): the payload changes so the stored FCS no longer matches
    uint8_t got[16];
    uint16_t gl = 0;
    TEST_ASSERT_EQUAL_INT(-1, pc_spinel_frame_decode(fr, frn, got, sizeof(got), &gl));
}

// --- Spinel value semantics -------------------------------------------------------------

void test_spinel_value_round_trip()
{
    // Build a heterogeneous value with the writer, read it back with the reader.
    uint8_t buf[64];
    SpinelWriter w;
    pc_spinel_writer_init(&w, buf, sizeof(buf));
    const uint8_t eui[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    const uint8_t v6[16] = {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0x02, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    TEST_ASSERT_TRUE(pc_spinel_put_bool(&w, true));
    TEST_ASSERT_TRUE(pc_spinel_put_u8(&w, 0xAB));
    TEST_ASSERT_TRUE(pc_spinel_put_i8(&w, -5));
    TEST_ASSERT_TRUE(pc_spinel_put_u16(&w, 0x1234));
    TEST_ASSERT_TRUE(pc_spinel_put_i16(&w, -1000));
    TEST_ASSERT_TRUE(pc_spinel_put_u32(&w, 0xDEADBEEF));
    TEST_ASSERT_TRUE(pc_spinel_put_i32(&w, -123456));
    TEST_ASSERT_TRUE(pc_spinel_put_uint(&w, 200000));
    TEST_ASSERT_TRUE(pc_spinel_put_eui64(&w, eui));
    TEST_ASSERT_TRUE(pc_spinel_put_ipv6(&w, v6));
    TEST_ASSERT_TRUE(pc_spinel_put_utf8(&w, "OT"));
    uint16_t n = pc_spinel_writer_len(&w);
    TEST_ASSERT_GREATER_THAN_UINT16(0, n);

    SpinelReader r;
    pc_spinel_reader_init(&r, buf, n);
    bool b = false;
    uint8_t u8 = 0;
    int8_t i8 = 0;
    uint16_t u16 = 0;
    int16_t i16 = 0;
    uint32_t u32 = 0;
    int32_t i32 = 0;
    uint32_t pu = 0;
    const uint8_t *e = nullptr;
    const uint8_t *a6 = nullptr;
    const char *s = nullptr;
    uint16_t slen = 0;
    TEST_ASSERT_TRUE(pc_spinel_get_bool(&r, &b));
    TEST_ASSERT_TRUE(pc_spinel_get_u8(&r, &u8));
    TEST_ASSERT_TRUE(pc_spinel_get_i8(&r, &i8));
    TEST_ASSERT_TRUE(pc_spinel_get_u16(&r, &u16));
    TEST_ASSERT_TRUE(pc_spinel_get_i16(&r, &i16));
    TEST_ASSERT_TRUE(pc_spinel_get_u32(&r, &u32));
    TEST_ASSERT_TRUE(pc_spinel_get_i32(&r, &i32));
    TEST_ASSERT_TRUE(pc_spinel_get_uint(&r, &pu));
    TEST_ASSERT_TRUE(pc_spinel_get_eui64(&r, &e));
    TEST_ASSERT_TRUE(pc_spinel_get_ipv6(&r, &a6));
    TEST_ASSERT_TRUE(pc_spinel_get_utf8(&r, &s, &slen));
    TEST_ASSERT_TRUE(pc_spinel_reader_ok(&r));

    TEST_ASSERT_TRUE(b);
    TEST_ASSERT_EQUAL_HEX8(0xAB, u8);
    TEST_ASSERT_EQUAL_INT8(-5, i8);
    TEST_ASSERT_EQUAL_HEX16(0x1234, u16);
    TEST_ASSERT_EQUAL_INT16(-1000, i16);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, u32);
    TEST_ASSERT_EQUAL_INT32(-123456, i32);
    TEST_ASSERT_EQUAL_UINT32(200000, pu);
    TEST_ASSERT_EQUAL_MEMORY(eui, e, 8);
    TEST_ASSERT_EQUAL_MEMORY(v6, a6, 16);
    TEST_ASSERT_EQUAL_UINT16(2, slen);
    TEST_ASSERT_EQUAL_MEMORY("OT", s, 2);
    TEST_ASSERT_EQUAL_UINT16(n, r.off); // consumed exactly the whole value
}

void test_spinel_put_bool_false()
{
    // Every other test only exercises pc_spinel_put_bool(true); cover the v == false arm of
    // its ternary here too, and confirm it round-trips through the reader as false.
    uint8_t buf[4];
    SpinelWriter w;
    pc_spinel_writer_init(&w, buf, sizeof(buf));
    TEST_ASSERT_TRUE(pc_spinel_put_bool(&w, false));
    TEST_ASSERT_EQUAL_UINT16(1, pc_spinel_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[0]);

    SpinelReader r;
    pc_spinel_reader_init(&r, buf, 1);
    bool b = true;
    TEST_ASSERT_TRUE(pc_spinel_get_bool(&r, &b));
    TEST_ASSERT_FALSE(b);
}

void test_spinel_le_wire_layout()
{
    // Confirm the on-wire encoding is little-endian for the fixed-width integers.
    uint8_t buf[8];
    SpinelWriter w;
    pc_spinel_writer_init(&w, buf, sizeof(buf));
    pc_spinel_put_u16(&w, 0x1234);
    pc_spinel_put_u32(&w, 0xAABBCCDD);
    TEST_ASSERT_EQUAL_UINT16(6, pc_spinel_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8(0x34, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x12, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0xDD, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[5]);
}

void test_spinel_protocol_version_and_caps()
{
    // PROTOCOL_VERSION is two packed uints; CAPS is a packed-uint array - decode as a real
    // gateway would (the reader reads successive 'i' fields).
    uint8_t buf[16];
    SpinelWriter w;
    pc_spinel_writer_init(&w, buf, sizeof(buf));
    pc_spinel_put_uint(&w, 4); // major
    pc_spinel_put_uint(&w, 3); // minor
    uint16_t n = pc_spinel_writer_len(&w);

    SpinelReader r;
    pc_spinel_reader_init(&r, buf, n);
    uint32_t major = 0, minor = 0;
    TEST_ASSERT_TRUE(pc_spinel_get_uint(&r, &major));
    TEST_ASSERT_TRUE(pc_spinel_get_uint(&r, &minor));
    TEST_ASSERT_EQUAL_UINT32(4, major);
    TEST_ASSERT_EQUAL_UINT32(3, minor);

    // A CAPS list: {1, 4, 128, 1337} as packed uints, read to exhaustion.
    pc_spinel_writer_init(&w, buf, sizeof(buf));
    const uint32_t caps[4] = {1, 4, 128, 1337};
    for (int i = 0; i < 4; i++)
    {
        pc_spinel_put_uint(&w, caps[i]);
    }
    n = pc_spinel_writer_len(&w);
    pc_spinel_reader_init(&r, buf, n);
    int count = 0;
    while (r.off < r.len && pc_spinel_reader_ok(&r))
    {
        uint32_t c = 0;
        TEST_ASSERT_TRUE(pc_spinel_get_uint(&r, &c));
        TEST_ASSERT_EQUAL_UINT32(caps[count], c);
        count++;
    }
    TEST_ASSERT_EQUAL_INT(4, count);
    TEST_ASSERT_TRUE(pc_spinel_reader_ok(&r));
}

void test_spinel_data_wlen_and_utf8()
{
    // STREAM_RAW-style 'd' data (uint16 length prefix), then STREAM_DEBUG-style 'U' text.
    uint8_t buf[64];
    SpinelWriter w;
    pc_spinel_writer_init(&w, buf, sizeof(buf));
    const uint8_t frame[5] = {0x41, 0x88, 0x01, 0xAB, 0xCD};
    TEST_ASSERT_TRUE(pc_spinel_put_data_wlen(&w, frame, 5));
    TEST_ASSERT_TRUE(pc_spinel_put_utf8(&w, "OPENTHREAD/x"));
    uint16_t n = pc_spinel_writer_len(&w);

    SpinelReader r;
    pc_spinel_reader_init(&r, buf, n);
    const uint8_t *d = nullptr;
    uint16_t dlen = 0;
    TEST_ASSERT_TRUE(pc_spinel_get_data_wlen(&r, &d, &dlen));
    TEST_ASSERT_EQUAL_UINT16(5, dlen);
    TEST_ASSERT_EQUAL_MEMORY(frame, d, 5);
    const char *s = nullptr;
    uint16_t slen = 0;
    TEST_ASSERT_TRUE(pc_spinel_get_utf8(&r, &s, &slen));
    TEST_ASSERT_EQUAL_UINT16(12, slen);
    TEST_ASSERT_EQUAL_MEMORY("OPENTHREAD/x", s, 12);
}

void test_spinel_get_data_rest()
{
    const uint8_t val[6] = {0x05, 0xDE, 0xAD, 0xBE, 0xEF, 0x99};
    SpinelReader r;
    pc_spinel_reader_init(&r, val, sizeof(val));
    uint8_t chan = 0;
    TEST_ASSERT_TRUE(pc_spinel_get_u8(&r, &chan)); // consume one leading field
    TEST_ASSERT_EQUAL_HEX8(0x05, chan);
    const uint8_t *rest = nullptr;
    uint16_t rlen = 0;
    TEST_ASSERT_TRUE(pc_spinel_get_data(&r, &rest, &rlen));
    TEST_ASSERT_EQUAL_UINT16(5, rlen);
    TEST_ASSERT_EQUAL_MEMORY(val + 1, rest, 5);
}

void test_spinel_reader_bounds_latch()
{
    // A too-short value latches err and every later read fails.
    const uint8_t val[1] = {0x01};
    SpinelReader r;
    pc_spinel_reader_init(&r, val, sizeof(val));
    uint32_t u32 = 0;
    TEST_ASSERT_FALSE(pc_spinel_get_u32(&r, &u32)); // needs 4, has 1
    TEST_ASSERT_FALSE(pc_spinel_reader_ok(&r));
    uint8_t u8 = 0;
    TEST_ASSERT_FALSE(pc_spinel_get_u8(&r, &u8)); // stays failed even though a byte remains

    // UTF8 with no NUL terminator is malformed.
    const uint8_t noterm[3] = {'a', 'b', 'c'};
    pc_spinel_reader_init(&r, noterm, sizeof(noterm));
    const char *s = nullptr;
    uint16_t slen = 0;
    TEST_ASSERT_FALSE(pc_spinel_get_utf8(&r, &s, &slen));

    // data_wlen whose declared length runs past the buffer.
    const uint8_t badlen[3] = {0x10, 0x00, 0xAA}; // says 16 bytes, only 1 present
    pc_spinel_reader_init(&r, badlen, sizeof(badlen));
    const uint8_t *d = nullptr;
    uint16_t dlen = 0;
    TEST_ASSERT_FALSE(pc_spinel_get_data_wlen(&r, &d, &dlen));
}

void test_spinel_writer_overflow_latch()
{
    uint8_t small[3];
    SpinelWriter w;
    pc_spinel_writer_init(&w, small, sizeof(small));
    TEST_ASSERT_TRUE(pc_spinel_put_u16(&w, 0x1122));
    TEST_ASSERT_FALSE(pc_spinel_put_u32(&w, 0));           // 2 used, 4 more won't fit
    TEST_ASSERT_EQUAL_UINT16(0, pc_spinel_writer_len(&w)); // err -> length 0

    // null-buffer guards.
    SpinelWriter nw;
    pc_spinel_writer_init(&nw, nullptr, 8);
    TEST_ASSERT_FALSE(pc_spinel_put_u8(&nw, 1));
    // null pointer args to put_eui64 / put_utf8 latch err.
    uint8_t ok[16];
    pc_spinel_writer_init(&w, ok, sizeof(ok));
    TEST_ASSERT_FALSE(pc_spinel_put_eui64(&w, nullptr));
    TEST_ASSERT_FALSE(pc_spinel_put_utf8(&w, nullptr));
}

void test_spinel_header_helpers()
{
    uint8_t h = pc_spinel_header(0, 5);
    TEST_ASSERT_EQUAL_HEX8(0x85, h); // 0x80 flag | tid 5
    TEST_ASSERT_EQUAL_UINT8(5, pc_spinel_header_tid(h));
    TEST_ASSERT_EQUAL_UINT8(0, pc_spinel_header_iid(h));
    uint8_t h2 = pc_spinel_header(2, 3);
    TEST_ASSERT_EQUAL_HEX8(0xA3, h2); // 0x80 | (2<<4) | 3
    TEST_ASSERT_EQUAL_UINT8(2, pc_spinel_header_iid(h2));
    TEST_ASSERT_EQUAL_UINT8(3, pc_spinel_header_tid(h2));
}

void test_spinel_prop_registry()
{
    TEST_ASSERT_EQUAL_STRING("NCP_VERSION", pc_spinel_prop_name(SpinelProp::SPINEL_PROP_NCP_VERSION));
    TEST_ASSERT_EQUAL_STRING("MAC_15_4_PANID", pc_spinel_prop_name(SpinelProp::SPINEL_PROP_MAC_15_4_PANID));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", pc_spinel_prop_name(0xFFFFFF));

    const SpinelPropInfo *e = pc_spinel_prop_lookup(SpinelProp::SPINEL_PROP_HWADDR);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT32(SpinelProp::SPINEL_PROP_HWADDR, e->id);
    TEST_ASSERT_EQUAL_INT8('E', e->type); // EUI64
    TEST_ASSERT_NULL(pc_spinel_prop_lookup(0x12345));

    // A couple of type-char spot checks.
    TEST_ASSERT_EQUAL_INT8('U', pc_spinel_prop_lookup(SpinelProp::SPINEL_PROP_NCP_VERSION)->type);
    TEST_ASSERT_EQUAL_INT8('C', pc_spinel_prop_lookup(SpinelProp::SPINEL_PROP_PHY_CHAN)->type);
    TEST_ASSERT_EQUAL_INT8('6', pc_spinel_prop_lookup(SpinelProp::SPINEL_PROP_IPV6_LL_ADDR)->type);
}

void test_spinel_status_names()
{
    TEST_ASSERT_EQUAL_STRING("OK", pc_spinel_status_name(SpinelStatus::SPINEL_STATUS_OK));
    TEST_ASSERT_EQUAL_STRING("PROP_NOT_FOUND", pc_spinel_status_name(SpinelStatus::SPINEL_STATUS_PROP_NOT_FOUND));
    TEST_ASSERT_EQUAL_STRING("RESET", pc_spinel_status_name(SpinelStatus::SPINEL_STATUS_RESET_POWER_ON));
    TEST_ASSERT_EQUAL_STRING("RESET", pc_spinel_status_name(115)); // 0x70..0x77 are reset causes
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", pc_spinel_status_name(9999));
}

void test_spinel_last_status_decode()
{
    // A real NCP unsolicited frame: header | CMD_PROP_VALUE_IS | PROP_LAST_STATUS | status(i).
    uint8_t payload[8];
    uint8_t val[4];
    SpinelWriter w;
    pc_spinel_writer_init(&w, val, sizeof(val));
    pc_spinel_put_uint(&w, SpinelStatus::SPINEL_STATUS_RESET_POWER_ON);
    uint16_t vlen = pc_spinel_writer_len(&w);
    uint16_t plen = pc_spinel_command_build(pc_spinel_header(0, 0), SpinelCmd::SPINEL_CMD_PROP_VALUE_IS,
                                            SpinelProp::SPINEL_PROP_LAST_STATUS, val, vlen, payload, sizeof(payload));
    TEST_ASSERT_GREATER_THAN_UINT16(0, plen);

    uint8_t header = 0;
    uint32_t cmd = 0, prop = 0;
    const uint8_t *v = nullptr;
    uint16_t got_vlen = 0;
    TEST_ASSERT_GREATER_THAN_INT(0, pc_spinel_command_parse(payload, plen, &header, &cmd, &prop, &v, &got_vlen));
    TEST_ASSERT_EQUAL_UINT32(SpinelCmd::SPINEL_CMD_PROP_VALUE_IS, cmd);
    TEST_ASSERT_EQUAL_UINT32(SpinelProp::SPINEL_PROP_LAST_STATUS, prop);
    SpinelReader r;
    pc_spinel_reader_init(&r, v, got_vlen);
    uint32_t status = 0;
    TEST_ASSERT_TRUE(pc_spinel_get_uint(&r, &status));
    TEST_ASSERT_EQUAL_UINT32(SpinelStatus::SPINEL_STATUS_RESET_POWER_ON, status);
    TEST_ASSERT_EQUAL_STRING("RESET", pc_spinel_status_name(status));
}

// --- Null-argument and bounds guards on every codec entry point ---------------------------

void test_spinel_null_out_params()
{
    // unpack_uint with no value out-parameter still reports the bytes consumed.
    const uint8_t one[1] = {0x2A};
    TEST_ASSERT_EQUAL_INT(1, pc_spinel_unpack_uint(one, 1, nullptr));

    uint8_t buf[32];
    const uint8_t val[2] = {0xAA, 0xBB};
    uint16_t n = pc_spinel_command_build(0x81, SpinelCmd::SPINEL_CMD_PROP_VALUE_SET, 1337, val, 2, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN_UINT16(0, n);

    // A zero-length payload has no header byte to read.
    TEST_ASSERT_EQUAL_INT(-1, pc_spinel_command_parse(buf, 0, nullptr, nullptr, nullptr, nullptr, nullptr));
    // Every out-parameter is optional: the parse still succeeds and reports the value offset.
    TEST_ASSERT_GREATER_THAN_INT(0, pc_spinel_command_parse(buf, n, nullptr, nullptr, nullptr, nullptr, nullptr));
}

void test_spinel_reader_init_variants()
{
    pc_spinel_reader_init(nullptr, nullptr, 0); // a null cursor is a no-op, not a crash

    SpinelReader r;
    const uint8_t v[2] = {1, 2};
    pc_spinel_reader_init(&r, v, 0); // a real buffer with an empty value
    TEST_ASSERT_EQUAL_UINT16(0, r.len);
    TEST_ASSERT_TRUE(pc_spinel_reader_ok(&r));

    pc_spinel_reader_init(&r, nullptr, 0); // no value at all is not an error
    TEST_ASSERT_EQUAL_UINT16(0, r.len);
    TEST_ASSERT_TRUE(pc_spinel_reader_ok(&r));

    pc_spinel_reader_init(&r, nullptr, 5); // a null value with a positive length is malformed
    TEST_ASSERT_EQUAL_UINT16(0, r.len);
    TEST_ASSERT_FALSE(pc_spinel_reader_ok(&r));

    TEST_ASSERT_FALSE(pc_spinel_reader_ok(nullptr)); // a null cursor is never ok
}

void test_spinel_getters_null_reader()
{
    bool b = false;
    uint8_t u8 = 0;
    int8_t i8 = 0;
    uint16_t u16 = 0;
    int16_t i16 = 0;
    uint32_t u32 = 0;
    int32_t i32 = 0;
    const uint8_t *p = nullptr;
    const char *s = nullptr;
    uint16_t l = 0;

    TEST_ASSERT_FALSE(pc_spinel_get_bool(nullptr, &b));
    TEST_ASSERT_FALSE(pc_spinel_get_u8(nullptr, &u8));
    TEST_ASSERT_FALSE(pc_spinel_get_i8(nullptr, &i8));
    TEST_ASSERT_FALSE(pc_spinel_get_u16(nullptr, &u16));
    TEST_ASSERT_FALSE(pc_spinel_get_i16(nullptr, &i16));
    TEST_ASSERT_FALSE(pc_spinel_get_u32(nullptr, &u32));
    TEST_ASSERT_FALSE(pc_spinel_get_i32(nullptr, &i32));
    TEST_ASSERT_FALSE(pc_spinel_get_uint(nullptr, &u32));
    TEST_ASSERT_FALSE(pc_spinel_get_eui64(nullptr, &p));
    TEST_ASSERT_FALSE(pc_spinel_get_ipv6(nullptr, &p));
    TEST_ASSERT_FALSE(pc_spinel_get_utf8(nullptr, &s, &l));
    TEST_ASSERT_FALSE(pc_spinel_get_data(nullptr, &p, &l));
    TEST_ASSERT_FALSE(pc_spinel_get_data_wlen(nullptr, &p, &l));
}

void test_spinel_getters_short_value()
{
    // An empty value: every typed read runs off the end at its first byte.
    const uint8_t v[1] = {0x42};
    SpinelReader r;
    bool b = false;
    int8_t i8 = 0;
    uint16_t u16 = 0;
    int16_t i16 = 0;
    int32_t i32 = 0;
    const uint8_t *p = nullptr;
    uint16_t l = 0;

    pc_spinel_reader_init(&r, v, 0);
    TEST_ASSERT_FALSE(pc_spinel_get_bool(&r, &b));
    pc_spinel_reader_init(&r, v, 0);
    TEST_ASSERT_FALSE(pc_spinel_get_i8(&r, &i8));
    pc_spinel_reader_init(&r, v, 0);
    TEST_ASSERT_FALSE(pc_spinel_get_u16(&r, &u16));
    pc_spinel_reader_init(&r, v, 0);
    TEST_ASSERT_FALSE(pc_spinel_get_i16(&r, &i16));
    pc_spinel_reader_init(&r, v, 0);
    TEST_ASSERT_FALSE(pc_spinel_get_i32(&r, &i32));
    pc_spinel_reader_init(&r, v, 0);
    TEST_ASSERT_FALSE(pc_spinel_get_eui64(&r, &p));
    pc_spinel_reader_init(&r, v, 0);
    TEST_ASSERT_FALSE(pc_spinel_get_ipv6(&r, &p));
    pc_spinel_reader_init(&r, v, 0);
    TEST_ASSERT_FALSE(pc_spinel_get_data_wlen(&r, &p, &l)); // not even the length prefix fits
}

void test_spinel_get_uint_edges()
{
    SpinelReader r;
    uint32_t uv = 0;

    // A packed uint whose continuation bit is set but which has no terminator.
    const uint8_t trunc[1] = {0x80};
    pc_spinel_reader_init(&r, trunc, sizeof(trunc));
    TEST_ASSERT_FALSE(pc_spinel_get_uint(&r, &uv));
    TEST_ASSERT_FALSE(pc_spinel_reader_ok(&r)); // the failure latches

    // An already-failed reader short-circuits before decoding.
    pc_spinel_reader_init(&r, nullptr, 4);
    TEST_ASSERT_FALSE(pc_spinel_get_uint(&r, &uv));

    // The out-parameter is optional.
    const uint8_t one[1] = {0x05};
    pc_spinel_reader_init(&r, one, sizeof(one));
    TEST_ASSERT_TRUE(pc_spinel_get_uint(&r, nullptr));
    TEST_ASSERT_EQUAL_UINT16(1, r.off);

    // 'U' and 'D' also short-circuit on a failed reader.
    const char *s = nullptr;
    const uint8_t *d = nullptr;
    uint16_t l = 0;
    pc_spinel_reader_init(&r, nullptr, 4);
    TEST_ASSERT_FALSE(pc_spinel_get_utf8(&r, &s, &l));
    pc_spinel_reader_init(&r, nullptr, 4);
    TEST_ASSERT_FALSE(pc_spinel_get_data(&r, &d, &l));
}

void test_spinel_getters_null_out_params()
{
    // Build one value holding every fixed-width field, then read it back discarding each result.
    uint8_t buf[64];
    SpinelWriter w;
    pc_spinel_writer_init(&w, buf, sizeof(buf));
    const uint8_t eui[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint8_t v6[16] = {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    TEST_ASSERT_TRUE(pc_spinel_put_bool(&w, true));
    TEST_ASSERT_TRUE(pc_spinel_put_u8(&w, 0x11));
    TEST_ASSERT_TRUE(pc_spinel_put_i8(&w, -2));
    TEST_ASSERT_TRUE(pc_spinel_put_u16(&w, 0x2233));
    TEST_ASSERT_TRUE(pc_spinel_put_i16(&w, -3));
    TEST_ASSERT_TRUE(pc_spinel_put_u32(&w, 0x44556677));
    TEST_ASSERT_TRUE(pc_spinel_put_i32(&w, -4));
    TEST_ASSERT_TRUE(pc_spinel_put_uint(&w, 4000));
    TEST_ASSERT_TRUE(pc_spinel_put_eui64(&w, eui));
    TEST_ASSERT_TRUE(pc_spinel_put_ipv6(&w, v6));
    TEST_ASSERT_TRUE(pc_spinel_put_utf8(&w, "z"));
    uint16_t n = pc_spinel_writer_len(&w);
    TEST_ASSERT_GREATER_THAN_UINT16(0, n);

    SpinelReader r;
    pc_spinel_reader_init(&r, buf, n);
    TEST_ASSERT_TRUE(pc_spinel_get_bool(&r, nullptr));
    TEST_ASSERT_TRUE(pc_spinel_get_u8(&r, nullptr));
    TEST_ASSERT_TRUE(pc_spinel_get_i8(&r, nullptr));
    TEST_ASSERT_TRUE(pc_spinel_get_u16(&r, nullptr));
    TEST_ASSERT_TRUE(pc_spinel_get_i16(&r, nullptr));
    TEST_ASSERT_TRUE(pc_spinel_get_u32(&r, nullptr));
    TEST_ASSERT_TRUE(pc_spinel_get_i32(&r, nullptr));
    TEST_ASSERT_TRUE(pc_spinel_get_uint(&r, nullptr));
    TEST_ASSERT_TRUE(pc_spinel_get_eui64(&r, nullptr));
    TEST_ASSERT_TRUE(pc_spinel_get_ipv6(&r, nullptr));
    TEST_ASSERT_TRUE(pc_spinel_get_utf8(&r, nullptr, nullptr));
    TEST_ASSERT_TRUE(pc_spinel_reader_ok(&r));
    TEST_ASSERT_EQUAL_UINT16(n, r.off); // every field was still consumed

    // 'd' then 'D' with both out-parameters dropped.
    const uint8_t dv[4] = {0x02, 0x00, 0xAA, 0xBB};
    pc_spinel_reader_init(&r, dv, sizeof(dv));
    TEST_ASSERT_TRUE(pc_spinel_get_data_wlen(&r, nullptr, nullptr));
    pc_spinel_reader_init(&r, dv, sizeof(dv));
    TEST_ASSERT_TRUE(pc_spinel_get_data(&r, nullptr, nullptr));
}

void test_spinel_writer_init_and_null_writer()
{
    pc_spinel_writer_init(nullptr, nullptr, 0); // a null cursor is a no-op
    TEST_ASSERT_EQUAL_UINT16(0, pc_spinel_writer_len(nullptr));

    SpinelWriter zw;
    pc_spinel_writer_init(&zw, nullptr, 0); // no buffer and no capacity is not an error
    TEST_ASSERT_EQUAL_UINT16(0, zw.cap);
    TEST_ASSERT_FALSE(zw.err);

    // Every put through a null writer fails without dereferencing it.
    const uint8_t eui[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint8_t v6[16] = {0};
    TEST_ASSERT_FALSE(pc_spinel_put_bool(nullptr, true));
    TEST_ASSERT_FALSE(pc_spinel_put_u8(nullptr, 1));
    TEST_ASSERT_FALSE(pc_spinel_put_i8(nullptr, 1));
    TEST_ASSERT_FALSE(pc_spinel_put_u16(nullptr, 1));
    TEST_ASSERT_FALSE(pc_spinel_put_i16(nullptr, 1));
    TEST_ASSERT_FALSE(pc_spinel_put_u32(nullptr, 1));
    TEST_ASSERT_FALSE(pc_spinel_put_i32(nullptr, 1));
    TEST_ASSERT_FALSE(pc_spinel_put_uint(nullptr, 1));
    TEST_ASSERT_FALSE(pc_spinel_put_eui64(nullptr, eui));
    TEST_ASSERT_FALSE(pc_spinel_put_ipv6(nullptr, v6));
    TEST_ASSERT_FALSE(pc_spinel_put_utf8(nullptr, "a"));
    TEST_ASSERT_FALSE(pc_spinel_put_data(nullptr, eui, 8));
    TEST_ASSERT_FALSE(pc_spinel_put_data_wlen(nullptr, eui, 8));
}

void test_spinel_put_null_args()
{
    uint8_t buf[32];
    SpinelWriter w;

    // A null data pointer with a zero length is a legal empty 'D' field.
    pc_spinel_writer_init(&w, buf, sizeof(buf));
    TEST_ASSERT_TRUE(pc_spinel_put_data(&w, nullptr, 0));
    TEST_ASSERT_EQUAL_UINT16(0, pc_spinel_writer_len(&w));

    // A null data pointer with a positive length latches err.
    pc_spinel_writer_init(&w, buf, sizeof(buf));
    TEST_ASSERT_FALSE(pc_spinel_put_data(&w, nullptr, 4));
    TEST_ASSERT_EQUAL_UINT16(0, pc_spinel_writer_len(&w));

    // So does a null IPv6 address.
    pc_spinel_writer_init(&w, buf, sizeof(buf));
    TEST_ASSERT_FALSE(pc_spinel_put_ipv6(&w, nullptr));
    TEST_ASSERT_EQUAL_UINT16(0, pc_spinel_writer_len(&w));

    // The same argument guards must not dereference a null writer either.
    TEST_ASSERT_FALSE(pc_spinel_put_eui64(nullptr, nullptr));
    TEST_ASSERT_FALSE(pc_spinel_put_ipv6(nullptr, nullptr));
    TEST_ASSERT_FALSE(pc_spinel_put_utf8(nullptr, nullptr));
    TEST_ASSERT_FALSE(pc_spinel_put_data(nullptr, nullptr, 4));

    // An empty UTF8 string still writes its NUL.
    pc_spinel_writer_init(&w, buf, sizeof(buf));
    TEST_ASSERT_TRUE(pc_spinel_put_utf8(&w, ""));
    TEST_ASSERT_EQUAL_UINT16(1, pc_spinel_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[0]);
}

void test_spinel_put_no_room_each_type()
{
    // A zero-capacity writer: every field type fails at the room reservation.
    uint8_t buf[8];
    const uint8_t eui[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint8_t v6[16] = {0};
    SpinelWriter w;

    pc_spinel_writer_init(&w, buf, 0);
    TEST_ASSERT_FALSE(pc_spinel_put_bool(&w, true));
    pc_spinel_writer_init(&w, buf, 0);
    TEST_ASSERT_FALSE(pc_spinel_put_u16(&w, 1));
    pc_spinel_writer_init(&w, buf, 0);
    TEST_ASSERT_FALSE(pc_spinel_put_uint(&w, 1));
    pc_spinel_writer_init(&w, buf, 0);
    TEST_ASSERT_FALSE(pc_spinel_put_eui64(&w, eui));
    pc_spinel_writer_init(&w, buf, 0);
    TEST_ASSERT_FALSE(pc_spinel_put_ipv6(&w, v6));
    pc_spinel_writer_init(&w, buf, 0);
    TEST_ASSERT_FALSE(pc_spinel_put_utf8(&w, "hi"));
    pc_spinel_writer_init(&w, buf, 0);
    TEST_ASSERT_FALSE(pc_spinel_put_data(&w, eui, 8));
    pc_spinel_writer_init(&w, buf, 0);
    TEST_ASSERT_FALSE(pc_spinel_put_data_wlen(&w, eui, 8)); // the length prefix itself does not fit

    // A 'd' field whose length prefix fits but whose bytes do not.
    pc_spinel_writer_init(&w, buf, 2);
    TEST_ASSERT_FALSE(pc_spinel_put_data_wlen(&w, eui, 8));

    // An already-failed writer short-circuits before packing or copying.
    pc_spinel_writer_init(&w, buf, 0);
    TEST_ASSERT_FALSE(pc_spinel_put_u8(&w, 1)); // latches err
    TEST_ASSERT_FALSE(pc_spinel_put_uint(&w, 1));
    TEST_ASSERT_FALSE(pc_spinel_put_data(&w, eui, 8));
}

void test_spinel_frame_edges()
{
    uint8_t out[64];
    uint8_t pay[64];
    uint16_t pl = 0;
    const uint8_t p4[4] = {1, 2, 3, 4};

    // encode: a null output buffer, and a null payload with a positive length.
    TEST_ASSERT_EQUAL_UINT16(0, pc_spinel_frame_encode(p4, 4, nullptr, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT16(0, pc_spinel_frame_encode(nullptr, 4, out, sizeof(out)));

    // A zero-length payload is legal: FCS(lo,hi) + flag only, and it round-trips.
    uint16_t n = pc_spinel_frame_encode(nullptr, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT16(3, n);
    TEST_ASSERT_EQUAL_HEX8(ThreadHdlc::HDLC_FLAG, out[2]);
    TEST_ASSERT_EQUAL_INT((int)n, pc_spinel_frame_decode(out, n, pay, sizeof(pay), &pl));
    TEST_ASSERT_EQUAL_UINT16(0, pl);

    // A cap where the payload and the FCS low byte fit but the FCS high byte does not.
    const uint8_t one[1] = {0x01};
    TEST_ASSERT_EQUAL_UINT16(0, pc_spinel_frame_encode(one, 1, out, 2));

    // decode with no pay_len out-parameter still reports the consumed length.
    uint16_t fn = pc_spinel_frame_encode(p4, 4, out, sizeof(out));
    TEST_ASSERT_GREATER_THAN_UINT16(0, fn);
    TEST_ASSERT_EQUAL_INT((int)fn, pc_spinel_frame_decode(out, fn, pay, sizeof(pay), nullptr));
}

void test_spinel_status_name_below_reset_range()
{
    // Unregistered codes on either side of the 0x70..0x77 reset-cause window.
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", pc_spinel_status_name(100)); // below the window
    TEST_ASSERT_EQUAL_STRING("RESET", pc_spinel_status_name(119));   // the last reset cause
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", pc_spinel_status_name(120)); // just past the window
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_fcs_x25_check_value);
    RUN_TEST(test_encode_decode_round_trip);
    RUN_TEST(test_byte_stuffing_round_trip);
    RUN_TEST(test_decode_needs_more_without_flag);
    RUN_TEST(test_decode_rejects_bad_fcs);
    RUN_TEST(test_decode_rejects_dangling_escape);
    RUN_TEST(test_decode_rejects_small_payload_buffer);
    RUN_TEST(test_encode_bounds);
    RUN_TEST(test_spinel_pack_uint_kats);
    RUN_TEST(test_spinel_pack_unpack_round_trip);
    RUN_TEST(test_spinel_unpack_needs_more_and_overflow);
    RUN_TEST(test_spinel_command_build_parse_round_trip);
    RUN_TEST(test_spinel_command_through_hdlc);
    RUN_TEST(test_spinel_guards);
    RUN_TEST(test_thread_more_guards);
    RUN_TEST(test_spinel_value_round_trip);
    RUN_TEST(test_spinel_put_bool_false);
    RUN_TEST(test_spinel_le_wire_layout);
    RUN_TEST(test_spinel_protocol_version_and_caps);
    RUN_TEST(test_spinel_data_wlen_and_utf8);
    RUN_TEST(test_spinel_get_data_rest);
    RUN_TEST(test_spinel_reader_bounds_latch);
    RUN_TEST(test_spinel_writer_overflow_latch);
    RUN_TEST(test_spinel_header_helpers);
    RUN_TEST(test_spinel_prop_registry);
    RUN_TEST(test_spinel_status_names);
    RUN_TEST(test_spinel_last_status_decode);
    RUN_TEST(test_spinel_null_out_params);
    RUN_TEST(test_spinel_reader_init_variants);
    RUN_TEST(test_spinel_getters_null_reader);
    RUN_TEST(test_spinel_getters_short_value);
    RUN_TEST(test_spinel_get_uint_edges);
    RUN_TEST(test_spinel_getters_null_out_params);
    RUN_TEST(test_spinel_writer_init_and_null_writer);
    RUN_TEST(test_spinel_put_null_args);
    RUN_TEST(test_spinel_put_no_room_each_type);
    RUN_TEST(test_spinel_frame_edges);
    RUN_TEST(test_spinel_status_name_below_reset_range);
    return UNITY_END();
}
