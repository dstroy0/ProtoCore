// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the SunSpec Modbus device-information-model codec (services/energy/sunspec/sunspec.h).
//
// The load-bearing case is test_sunspec_identifier_is_the_ascii_marker. The SunSpec Device
// Information Model specification fixes one well-known 32-bit value at the head of every map, and
// publishes it as the four ASCII characters "SunS". A reader that hunts for any other constant
// finds no device at all, so the expected number here is spelled out from the ASCII code points
// rather than copied off SUNSPEC_MARKER. Every model header expectation below likewise comes from
// the published layout: [ID][L][L body registers], registers big-endian, terminated by [0xFFFF][0].

#include "services/energy/sunspec/sunspec.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// The identifier is the four ASCII characters 'S','u','n','S' laid down as two big-endian
// registers: 'S' = 0x53, 'u' = 0x75, 'n' = 0x6E, 'S' = 0x53, so the u32 is 0x53756E53.
void test_sunspec_identifier_is_the_ascii_marker(void)
{
    static const uint8_t MARKER[4] = {(uint8_t)'S', (uint8_t)'u', (uint8_t)'n', (uint8_t)'S'};
    TEST_ASSERT_EQUAL_HEX32(0x53756E53u, SUNSPEC_MARKER);
    TEST_ASSERT_TRUE(protocore_sunspec_check_marker(MARKER, sizeof(MARKER)));

    // "Suns" differs from "SunS" in one bit of one octet and is not the identifier.
    static const uint8_t NEAR[4] = {(uint8_t)'S', (uint8_t)'u', (uint8_t)'n', (uint8_t)'s'};
    TEST_ASSERT_FALSE(protocore_sunspec_check_marker(NEAR, sizeof(NEAR)));

    // The marker is two registers, so three octets cannot carry it.
    TEST_ASSERT_FALSE(protocore_sunspec_check_marker(MARKER, 3));
    TEST_ASSERT_FALSE(protocore_sunspec_check_marker(NULL, 4));
}

// The walk starts just past the 2-register identifier, i.e. at octet 4.
void test_begin_positions_past_the_two_marker_registers(void)
{
    static const uint8_t MAP[6] = {'S', 'u', 'n', 'S', 0x00, 0x01};
    size_t off = 0xDEAD;
    TEST_ASSERT_TRUE(protocore_sunspec_begin(MAP, sizeof(MAP), &off));
    TEST_ASSERT_EQUAL_size_t(4u, off);

    static const uint8_t NOT_A_MAP[6] = {'X', 'X', 'X', 'X', 0x00, 0x01};
    TEST_ASSERT_FALSE(protocore_sunspec_begin(NOT_A_MAP, sizeof(NOT_A_MAP), &off));
    TEST_ASSERT_FALSE(protocore_sunspec_begin(MAP, sizeof(MAP), NULL));
}

// A hand-laid map with the published layout: marker, common model (ID 1) with a two-register body,
// a second model (ID 103), then the end model. Each field is written here as the big-endian octet
// pair the spec calls for, so the walker is checked against the layout and not against the writer.
static const uint8_t MAP[] = {
    'S',  'u',  'n', 'S', // identifier, 2 registers
    0x00, 0x01,           // model ID 1 (common)
    0x00, 0x02,           // length 2 (body registers after the length point)
    0x12, 0x34,           // body register 0
    0xFF, 0xFE,           // body register 1
    0x00, 0x67,           // model ID 103 = 0x67 (inverter, three phase)
    0x00, 0x01,           // length 1
    0xAB, 0xCD,           // body register 0
    0xFF, 0xFF,           // end model
    0x00, 0x00,           // end model length 0
};

void test_walks_the_model_chain_to_the_end_model(void)
{
    size_t off = 0;
    SunSpecModel m;
    TEST_ASSERT_TRUE(protocore_sunspec_begin(MAP, sizeof(MAP), &off));

    TEST_ASSERT_TRUE(protocore_sunspec_next_model(MAP, sizeof(MAP), &off, &m));
    TEST_ASSERT_EQUAL_UINT16(SUNSPEC_COMMON_MODEL, m.id);
    TEST_ASSERT_EQUAL_UINT16(2u, m.length);
    TEST_ASSERT_EQUAL_size_t(4u, m.body_len); // length * 2 octets
    TEST_ASSERT_EQUAL_PTR(MAP + 8, m.body);   // 4 marker + 4 header
    TEST_ASSERT_EQUAL_size_t(12u, off);       // models are contiguous: next header follows the body

    TEST_ASSERT_TRUE(protocore_sunspec_next_model(MAP, sizeof(MAP), &off, &m));
    TEST_ASSERT_EQUAL_UINT16(103u, m.id);
    TEST_ASSERT_EQUAL_UINT16(1u, m.length);
    TEST_ASSERT_EQUAL_size_t(2u, m.body_len);
    TEST_ASSERT_EQUAL_size_t(18u, off);

    // 0xFFFF terminates the chain; the cursor is left on the end model rather than advanced past it.
    TEST_ASSERT_FALSE(protocore_sunspec_next_model(MAP, sizeof(MAP), &off, &m));
    TEST_ASSERT_EQUAL_size_t(18u, off);
    TEST_ASSERT_EQUAL_HEX16(SUNSPEC_END_MODEL, 0xFFFFu);
}

// A model whose declared length runs past the received registers is refused, not sliced short: the
// length point is attacker-controlled data from the register block.
void test_truncated_body_is_refused(void)
{
    static const uint8_t SHORT[] = {
        'S', 'u', 'n', 'S', 0x00, 0x01, 0x00, 0x04, 0x11, 0x22, 0x33, 0x44, // declares 4 registers, carries 2
    };
    size_t off = 0;
    SunSpecModel m;
    TEST_ASSERT_TRUE(protocore_sunspec_begin(SHORT, sizeof(SHORT), &off));
    TEST_ASSERT_FALSE(protocore_sunspec_next_model(SHORT, sizeof(SHORT), &off, &m));

    // A header cut in half is refused too.
    static const uint8_t STUB[] = {'S', 'u', 'n', 'S', 0x00, 0x01, 0x00};
    off = 0;
    TEST_ASSERT_TRUE(protocore_sunspec_begin(STUB, sizeof(STUB), &off));
    TEST_ASSERT_FALSE(protocore_sunspec_next_model(STUB, sizeof(STUB), &off, &m));
}

// Points are big-endian registers addressed by register index, so point n starts at octet 2n.
// The signed readers reinterpret the same octets in two's complement: 0xFFFE is -2, 0xFFFFFFFE is -2.
void test_typed_point_readers_are_big_endian(void)
{
    static const uint8_t BODY[8] = {0x12, 0x34, 0xFF, 0xFE, 0x00, 0x01, 0x86, 0xA0};

    TEST_ASSERT_EQUAL_HEX16(0x1234u, protocore_sunspec_u16(BODY, 0));
    TEST_ASSERT_EQUAL_HEX16(0xFFFEu, protocore_sunspec_u16(BODY, 1));
    TEST_ASSERT_EQUAL_INT16(0x1234, protocore_sunspec_i16(BODY, 0));
    TEST_ASSERT_EQUAL_INT16(-2, protocore_sunspec_i16(BODY, 1));

    // registers 2..3 = 0x000186A0 = 100000
    TEST_ASSERT_EQUAL_HEX32(0x000186A0u, protocore_sunspec_u32(BODY, 2));
    TEST_ASSERT_EQUAL_INT32(100000, protocore_sunspec_i32(BODY, 2));
    // registers 0..1 = 0x1234FFFE read as a u32 spanning the register pair
    TEST_ASSERT_EQUAL_HEX32(0x1234FFFEu, protocore_sunspec_u32(BODY, 0));

    static const uint8_t NEG[4] = {0xFF, 0xFF, 0xFF, 0xFE};
    TEST_ASSERT_EQUAL_INT32(-2, protocore_sunspec_i32(NEG, 0));
}

// A SunSpec string point is a fixed run of registers holding ASCII, NUL-padded to the field width.
// The content ends at the first NUL, and a full-width string has no NUL to end it.
void test_string_point_stops_at_the_nul_padding(void)
{
    static const uint8_t BODY[8] = {'A', 'C', 'M', 'E', 0x00, 0x00, 0x00, 0x00};
    char out[16];
    TEST_ASSERT_TRUE(protocore_sunspec_string(BODY, 0, 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("ACME", out);

    // A field with no padding: all 4 octets are content.
    static const uint8_t FULL[4] = {'A', 'B', 'C', 'D'};
    TEST_ASSERT_TRUE(protocore_sunspec_string(FULL, 0, 2, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("ABCD", out);

    // A destination shorter than the field truncates and still terminates.
    char small[3];
    TEST_ASSERT_TRUE(protocore_sunspec_string(FULL, 0, 2, small, sizeof(small)));
    TEST_ASSERT_EQUAL_STRING("AB", small);

    TEST_ASSERT_FALSE(protocore_sunspec_string(FULL, 0, 2, out, 0));
    TEST_ASSERT_FALSE(protocore_sunspec_string(NULL, 0, 2, out, sizeof(out)));
    TEST_ASSERT_FALSE(protocore_sunspec_string(FULL, 0, 2, NULL, sizeof(out)));
}

// What the writer emits, the walker must read back as the same chain: identifier, two models with
// their bodies, end model. The octet count is arithmetic from the layout - 4 marker + per model
// (4 header + 2 * L) + 4 end model.
void test_writer_and_walker_round_trip(void)
{
    uint8_t buf[64];
    SunSpecWriter w;
    protocore_sunspec_writer_init(&w, buf, sizeof(buf));
    TEST_ASSERT_TRUE(protocore_sunspec_write_marker(&w));
    TEST_ASSERT_TRUE(protocore_sunspec_write_model_header(&w, SUNSPEC_COMMON_MODEL, 5));
    TEST_ASSERT_TRUE(protocore_sunspec_write_string(&w, "ACME", 2)); // 2 registers, NUL-padded
    TEST_ASSERT_TRUE(protocore_sunspec_write_i16(&w, -3));
    TEST_ASSERT_TRUE(protocore_sunspec_write_u32(&w, 0x000186A0u));
    TEST_ASSERT_TRUE(protocore_sunspec_write_end_model(&w));
    size_t n = protocore_sunspec_writer_finish(&w);
    TEST_ASSERT_EQUAL_size_t(4u + (4u + 10u) + 4u, n);

    // The identifier the writer laid down is the ASCII one.
    TEST_ASSERT_EQUAL_UINT8((uint8_t)'S', buf[0]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)'u', buf[1]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)'n', buf[2]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)'S', buf[3]);

    size_t off = 0;
    SunSpecModel m;
    TEST_ASSERT_TRUE(protocore_sunspec_begin(buf, n, &off));
    TEST_ASSERT_TRUE(protocore_sunspec_next_model(buf, n, &off, &m));
    TEST_ASSERT_EQUAL_UINT16(SUNSPEC_COMMON_MODEL, m.id);
    TEST_ASSERT_EQUAL_UINT16(5u, m.length);

    char name[8];
    TEST_ASSERT_TRUE(protocore_sunspec_string(m.body, 0, 2, name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("ACME", name);
    TEST_ASSERT_EQUAL_INT16(-3, protocore_sunspec_i16(m.body, 2));
    TEST_ASSERT_EQUAL_HEX32(0x000186A0u, protocore_sunspec_u32(m.body, 3));

    TEST_ASSERT_FALSE(protocore_sunspec_next_model(buf, n, &off, &m)); // the end model stops the walk
}

// The end model is [0xFFFF][0] - two registers, the second one zero.
void test_end_model_is_two_registers(void)
{
    uint8_t buf[8];
    SunSpecWriter w;
    protocore_sunspec_writer_init(&w, buf, sizeof(buf));
    TEST_ASSERT_TRUE(protocore_sunspec_write_end_model(&w));
    TEST_ASSERT_EQUAL_size_t(4u, protocore_sunspec_writer_finish(&w));
    TEST_ASSERT_EQUAL_HEX8(0xFFu, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[3]);
}

// A string longer than its field is cut to the field width, never spilling into the next point.
void test_write_string_is_clamped_to_the_field(void)
{
    uint8_t buf[8];
    SunSpecWriter w;
    protocore_sunspec_writer_init(&w, buf, sizeof(buf));
    TEST_ASSERT_TRUE(protocore_sunspec_write_string(&w, "ABCDEFGH", 2)); // field is 4 octets
    TEST_ASSERT_EQUAL_size_t(4u, protocore_sunspec_writer_finish(&w));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)'A', buf[0]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)'D', buf[3]);

    protocore_sunspec_writer_init(&w, buf, sizeof(buf));
    TEST_ASSERT_FALSE(protocore_sunspec_write_string(&w, NULL, 2));
}

// Once a write does not fit, the cursor latches the error and finish reports 0 rather than a
// half-written map that a peer would walk into.
void test_overflow_latches_and_finish_reports_zero(void)
{
    uint8_t buf[5];
    SunSpecWriter w;
    protocore_sunspec_writer_init(&w, buf, sizeof(buf));
    TEST_ASSERT_TRUE(protocore_sunspec_write_marker(&w)); // 4 of 5 octets
    TEST_ASSERT_FALSE(protocore_sunspec_write_u16(&w, 1));
    TEST_ASSERT_FALSE(protocore_sunspec_write_u16(&w, 1)); // still refused after the latch
    TEST_ASSERT_EQUAL_size_t(0u, protocore_sunspec_writer_finish(&w));

    // A string that does not fit latches the same way.
    protocore_sunspec_writer_init(&w, buf, sizeof(buf));
    TEST_ASSERT_FALSE(protocore_sunspec_write_string(&w, "ABCDEF", 3)); // 6 > 5
    TEST_ASSERT_EQUAL_size_t(0u, protocore_sunspec_writer_finish(&w));
}

// Signed points survive the writer/reader pair at the two's-complement extremes.
void test_signed_points_round_trip_at_the_extremes(void)
{
    uint8_t buf[16];
    SunSpecWriter w;
    protocore_sunspec_writer_init(&w, buf, sizeof(buf));
    TEST_ASSERT_TRUE(protocore_sunspec_write_i16(&w, -32768));
    TEST_ASSERT_TRUE(protocore_sunspec_write_i16(&w, 32767));
    TEST_ASSERT_TRUE(protocore_sunspec_write_i32(&w, -2147483647 - 1));
    TEST_ASSERT_TRUE(protocore_sunspec_write_i32(&w, 2147483647));
    TEST_ASSERT_EQUAL_size_t(12u, protocore_sunspec_writer_finish(&w));

    TEST_ASSERT_EQUAL_INT16(-32768, protocore_sunspec_i16(buf, 0));
    TEST_ASSERT_EQUAL_INT16(32767, protocore_sunspec_i16(buf, 1));
    TEST_ASSERT_EQUAL_INT32(-2147483647 - 1, protocore_sunspec_i32(buf, 2));
    TEST_ASSERT_EQUAL_INT32(2147483647, protocore_sunspec_i32(buf, 4));
    // -32768 is 0x8000 big-endian: the sign octet leads.
    TEST_ASSERT_EQUAL_HEX8(0x80u, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[1]);
}

// Null cursors and a zero-length model body are handled without walking off the buffer.
void test_next_model_guards(void)
{
    size_t off = 4;
    SunSpecModel m;
    TEST_ASSERT_FALSE(protocore_sunspec_next_model(NULL, 8, &off, &m));
    TEST_ASSERT_FALSE(protocore_sunspec_next_model(MAP, sizeof(MAP), NULL, &m));
    TEST_ASSERT_FALSE(protocore_sunspec_next_model(MAP, sizeof(MAP), &off, NULL));

    // A model may legally declare length 0: header only, empty body, cursor advances by 4.
    static const uint8_t EMPTY[] = {'S', 'u', 'n', 'S', 0x00, 0x0C, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00};
    off = 0;
    TEST_ASSERT_TRUE(protocore_sunspec_begin(EMPTY, sizeof(EMPTY), &off));
    TEST_ASSERT_TRUE(protocore_sunspec_next_model(EMPTY, sizeof(EMPTY), &off, &m));
    TEST_ASSERT_EQUAL_UINT16(12u, m.id);
    TEST_ASSERT_EQUAL_size_t(0u, m.body_len);
    TEST_ASSERT_EQUAL_size_t(8u, off);
    TEST_ASSERT_FALSE(protocore_sunspec_next_model(EMPTY, sizeof(EMPTY), &off, &m));
}
