// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the OPC UA Binary server core (services/opcua/opcua.h).
//
// The load-bearing case is test_part6_builtin_type_encodings: OPC UA Part 6 section 5.2.2 publishes
// the byte sequences for the built-in types this whole stack is written on top of - 1000000000 as
// 00 CA 9A 3B, -6.5 as 00 00 D0 C0, a null String as FF FF FF FF. Every NodeId, ResponseHeader and
// Variant above them is those primitives in order, so a byte order that drifts there is a wire
// format nobody can talk to. The numeric NodeIds and StatusCodes asserted below are the OPC
// Foundation's own registry assignments (NodeIds.csv, StatusCode.csv).

#include "services/opcua/opcua.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Unity's double assertions are compiled out in this build, so doubles are compared by their bits.
static proto_bool same_double(double a, double b)
{
    return memcmp(&a, &b, sizeof(a)) == 0;
}

// --- a little-endian composer, independent of the module, for assembling request messages ---

static uint8_t g_msg[512];
static size_t g_n;

static void put_u8(uint8_t v)
{
    g_msg[g_n++] = v;
}
static void put_u16(uint16_t v)
{
    put_u8((uint8_t)v);
    put_u8((uint8_t)(v >> 8));
}
static void put_u32(uint32_t v)
{
    for (int i = 0; i < 4; i++)
    {
        put_u8((uint8_t)(v >> (8 * i)));
    }
}
static void put_u64(uint64_t v)
{
    for (int i = 0; i < 8; i++)
    {
        put_u8((uint8_t)(v >> (8 * i)));
    }
}
// String / ByteString: an Int32 length (-1 = null) then the bytes (Part 6 sec 5.2.2.4 / 5.2.2.7).
static void put_str(const char *s)
{
    if (!s)
    {
        put_u32(0xFFFFFFFFu);
        return;
    }
    size_t l = strlen(s);
    put_u32((uint32_t)l);
    for (size_t i = 0; i < l; i++)
    {
        put_u8((uint8_t)s[i]);
    }
}
// A numeric NodeId in the Four Byte form: 0x01, namespace octet, identifier UInt16 (sec 5.2.2.9).
static void put_nodeid(uint16_t ns, uint16_t id)
{
    put_u8(0x01);
    put_u8((uint8_t)ns);
    put_u16(id);
}
// The RequestHeader every service request opens with (Part 4 sec 7.28).
static void put_request_header(uint32_t handle)
{
    put_u8(0x00); // AuthenticationToken: Two Byte NodeId i=0
    put_u8(0x00);
    put_u64(0); // Timestamp
    put_u32(handle);
    put_u32(0);    // ReturnDiagnostics
    put_str(NULL); // AuditEntryId
    put_u32(0);    // TimeoutHint
    put_u8(0x00);  // AdditionalHeader: null NodeId ...
    put_u8(0x00);
    put_u8(0x00); // ... + ExtensionObject encoding byte "no body"
}
// Start a UACP message; the MessageSize is patched by finish().
static void start(const char *type)
{
    g_n = 0;
    put_u8((uint8_t)type[0]);
    put_u8((uint8_t)type[1]);
    put_u8((uint8_t)type[2]);
    put_u8('F');
    put_u32(0);
}
static size_t finish(void)
{
    g_msg[4] = (uint8_t)g_n;
    g_msg[5] = (uint8_t)(g_n >> 8);
    g_msg[6] = (uint8_t)(g_n >> 16);
    g_msg[7] = (uint8_t)(g_n >> 24);
    return g_n;
}

// OPC UA Part 6 sec 5.2.2: the published byte sequences for the built-in types.
void test_part6_builtin_type_encodings(void)
{
    uint8_t buf[32];

    // sec 5.2.2.2: "All integer types shall be encoded as little-endian values where the least
    // significant byte appears first". The worked example is 1000000000 (3B9ACA00h) -> 00 CA 9A 3B.
    UaWriter w = {buf, sizeof(buf), 0, PROTO_TRUE};
    protocore_ua_w_u32(&w, 1000000000u);
    static const uint8_t INT_EXAMPLE[4] = {0x00, 0xCA, 0x9A, 0x3B};
    TEST_ASSERT_EQUAL_UINT(4u, w.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(INT_EXAMPLE, buf, 4);

    // sec 5.2.2.3: Float is IEEE 754 little-endian. The worked example is -6.5 (C0D00000h).
    w.n = 0;
    w.ok = PROTO_TRUE;
    protocore_ua_w_f32(&w, -6.5f);
    static const uint8_t FLOAT_EXAMPLE[4] = {0x00, 0x00, 0xD0, 0xC0};
    TEST_ASSERT_EQUAL_UINT(4u, w.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(FLOAT_EXAMPLE, buf, 4);

    // sec 5.2.2.4: a String is UTF-8 preceded by its length in bytes as an Int32. The spec's
    // example string is a CJK character followed by "Boy"; the CJK character is three UTF-8 octets
    // E6 B0 B4, so the length is 6 and the encoding is 06 00 00 00 E6 B0 B4 42 6F 79.
    static const char CJK_BOY[7] = {(char)0xE6, (char)0xB0, (char)0xB4, 'B', 'o', 'y', '\0'};
    w.n = 0;
    w.ok = PROTO_TRUE;
    protocore_ua_w_string(&w, CJK_BOY, 6);
    static const uint8_t STR_EXAMPLE[10] = {0x06, 0x00, 0x00, 0x00, 0xE6, 0xB0, 0xB4, 0x42, 0x6F, 0x79};
    TEST_ASSERT_EQUAL_UINT(10u, w.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(STR_EXAMPLE, buf, 10);

    // sec 5.2.2.4: "A value of -1 is used to indicate a null string", so it is the length alone.
    w.n = 0;
    w.ok = PROTO_TRUE;
    protocore_ua_w_string(&w, NULL, -1);
    static const uint8_t NULL_STR[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_UINT(4u, w.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(NULL_STR, buf, 4);

    // sec 5.2.2.1: "a value of 0 is false and any non-zero value is true", encoded as one byte.
    w.n = 0;
    w.ok = PROTO_TRUE;
    protocore_ua_w_bool(&w, PROTO_TRUE);
    protocore_ua_w_bool(&w, PROTO_FALSE);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[1]);
}

// Every writer has a reader that must return what was written, for the extremes of each width.
void test_reader_inverts_the_writer(void)
{
    uint8_t buf[64];
    UaWriter w = {buf, sizeof(buf), 0, PROTO_TRUE};
    protocore_ua_w_u8(&w, 0xFF);
    protocore_ua_w_u16(&w, 0xFEDC);
    protocore_ua_w_u32(&w, 0xFEDCBA98u);
    protocore_ua_w_u64(&w, 0xFEDCBA9876543210ull);
    protocore_ua_w_i32(&w, -2147483647 - 1);
    protocore_ua_w_f32(&w, 3.5f);
    protocore_ua_w_f64(&w, -1.0e300);
    protocore_ua_w_bool(&w, PROTO_TRUE);
    TEST_ASSERT_TRUE(w.ok);

    UaReader r = {buf, w.n, 0, PROTO_FALSE};
    TEST_ASSERT_EQUAL_HEX8(0xFF, protocore_ua_r_u8(&r));
    TEST_ASSERT_EQUAL_HEX16(0xFEDC, protocore_ua_r_u16(&r));
    TEST_ASSERT_EQUAL_HEX32(0xFEDCBA98u, protocore_ua_r_u32(&r));
    TEST_ASSERT_TRUE(0xFEDCBA9876543210ull == protocore_ua_r_u64(&r));
    TEST_ASSERT_EQUAL_INT32(-2147483647 - 1, protocore_ua_r_i32(&r));
    TEST_ASSERT_EQUAL_FLOAT(3.5f, protocore_ua_r_f32(&r));
    TEST_ASSERT_TRUE(same_double(-1.0e300, protocore_ua_r_f64(&r)));
    TEST_ASSERT_TRUE(protocore_ua_r_bool(&r));
    TEST_ASSERT_FALSE(r.err);
    TEST_ASSERT_EQUAL_UINT(w.n, r.off);
}

// The writer refuses to run past its capacity and latches, and the reader latches on underrun
// rather than reading past its buffer.
void test_bounds_latch(void)
{
    uint8_t small[3];
    UaWriter w = {small, sizeof(small), 0, PROTO_TRUE};
    protocore_ua_w_u16(&w, 0x1234);
    TEST_ASSERT_TRUE(w.ok);
    protocore_ua_w_u32(&w, 1); // one octet of room left, four wanted
    TEST_ASSERT_FALSE(w.ok);
    TEST_ASSERT_EQUAL_UINT(2u, w.n); // nothing partial was written
    protocore_ua_w_u8(&w, 1);        // a latched writer stays latched
    TEST_ASSERT_FALSE(w.ok);

    static const uint8_t two[2] = {0x01, 0x02};
    UaReader r = {two, sizeof(two), 0, PROTO_FALSE};
    TEST_ASSERT_EQUAL_HEX16(0x0201, protocore_ua_r_u16(&r));
    TEST_ASSERT_FALSE(r.err);
    (void)protocore_ua_r_u32(&r);
    TEST_ASSERT_TRUE(r.err);
    TEST_ASSERT_EQUAL_UINT(2u, r.off);
}

// Part 6 sec 5.2.2.4: a String decodes to its bytes, a -1 length is a null String, and a value too
// large for the destination is refused rather than truncated into it.
void test_string_decoding(void)
{
    uint8_t buf[32];
    UaWriter w = {buf, sizeof(buf), 0, PROTO_TRUE};
    protocore_ua_w_string(&w, "opc.tcp", 7);
    protocore_ua_w_string(&w, NULL, -1);

    char out[16];
    int32_t len = 0;
    UaReader r = {buf, w.n, 0, PROTO_FALSE};
    TEST_ASSERT_TRUE(protocore_ua_r_string(&r, out, sizeof(out), &len));
    TEST_ASSERT_EQUAL_INT32(7, len);
    TEST_ASSERT_EQUAL_STRING("opc.tcp", out);
    TEST_ASSERT_TRUE(protocore_ua_r_string(&r, out, sizeof(out), &len));
    TEST_ASSERT_EQUAL_INT32(-1, len);
    TEST_ASSERT_EQUAL_STRING("", out);

    char tiny[4];
    UaReader r2 = {buf, w.n, 0, PROTO_FALSE};
    TEST_ASSERT_FALSE(protocore_ua_r_string(&r2, tiny, sizeof(tiny), &len));
    TEST_ASSERT_TRUE(r2.err);
}

// Part 6 sec 5.2.2.9 Table 6: the NodeId encoding byte selects the form, and each form has a fixed
// field layout. The encoder picks the smallest form the value fits: Two Byte (0x00) for namespace 0
// with an identifier under 256, Four Byte (0x01) for a namespace under 256 with a UInt16
// identifier, Numeric (0x02) otherwise.
void test_nodeid_encoding_forms(void)
{
    uint8_t buf[16];

    UaWriter w = {buf, sizeof(buf), 0, PROTO_TRUE};
    protocore_ua_w_nodeid_numeric(&w, 0, 255);
    TEST_ASSERT_EQUAL_UINT(2u, w.n);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[1]);

    w.n = 0;
    protocore_ua_w_nodeid_numeric(&w, 0, 256); // no longer fits one octet
    TEST_ASSERT_EQUAL_UINT(4u, w.n);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]); // identifier 0100h, little-endian
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[3]);

    w.n = 0;
    protocore_ua_w_nodeid_numeric(&w, 2, 5); // a non-zero namespace forces the Four Byte form
    TEST_ASSERT_EQUAL_UINT(4u, w.n);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[1]);

    w.n = 0;
    protocore_ua_w_nodeid_numeric(&w, 300, 70000); // neither field fits the short forms
    TEST_ASSERT_EQUAL_UINT(7u, w.n);
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[0]);
    TEST_ASSERT_EQUAL_HEX16(300, (uint16_t)(buf[1] | (buf[2] << 8)));
}

// All three numeric forms decode back to the namespace and identifier they carry.
void test_nodeid_round_trip(void)
{
    struct
    {
        uint16_t ns;
        uint32_t id;
    } static const CASES[] = {{0, 0}, {0, 1}, {0, 255}, {0, 256}, {1, 65535}, {255, 65535}, {300, 70000}};
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        uint8_t buf[16];
        UaWriter w = {buf, sizeof(buf), 0, PROTO_TRUE};
        protocore_ua_w_nodeid_numeric(&w, CASES[i].ns, CASES[i].id);
        UaReader r = {buf, w.n, 0, PROTO_FALSE};
        UaNodeId got;
        TEST_ASSERT_TRUE(protocore_ua_r_nodeid(&r, &got));
        TEST_ASSERT_TRUE(got.numeric);
        TEST_ASSERT_EQUAL_UINT16(CASES[i].ns, got.ns);
        TEST_ASSERT_EQUAL_UINT32(CASES[i].id, got.id);
        TEST_ASSERT_EQUAL_UINT(w.n, r.off);
    }
}

// Part 6 sec 5.2.2.9: the String (0x03), Guid (0x04) and ByteString (0x05) forms carry a namespace
// and a variable identifier, and Table 6 assigns 0x80 to the NamespaceUri flag and 0x40 to the
// ServerIndex flag. The decoder must step over all of them and report the id is not numeric.
void test_nodeid_non_numeric_forms_are_skipped(void)
{
    // String form: 03, ns, Int32 length, bytes
    static const uint8_t STR_ID[] = {0x03, 0x01, 0x00, 0x03, 0x00, 0x00, 0x00, 'a', 'b', 'c', 0xAA};
    UaReader r = {STR_ID, sizeof(STR_ID), 0, PROTO_FALSE};
    UaNodeId n;
    TEST_ASSERT_TRUE(protocore_ua_r_nodeid(&r, &n));
    TEST_ASSERT_FALSE(n.numeric);
    TEST_ASSERT_EQUAL_UINT16(1, n.ns);
    TEST_ASSERT_EQUAL_HEX8(0xAA, STR_ID[r.off]); // stopped exactly after the identifier

    // Guid form: 04, ns, 16 octets
    static const uint8_t GUID_ID[] = {0x04, 0x02, 0x00, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 0xBB};
    UaReader r2 = {GUID_ID, sizeof(GUID_ID), 0, PROTO_FALSE};
    TEST_ASSERT_TRUE(protocore_ua_r_nodeid(&r2, &n));
    TEST_ASSERT_FALSE(n.numeric);
    TEST_ASSERT_EQUAL_HEX8(0xBB, GUID_ID[r2.off]);

    // Two Byte form with the NamespaceUri (0x80) and ServerIndex (0x40) flags set
    static const uint8_t EXPANDED[] = {0xC0, 0x07, 0x02, 0x00, 0x00, 0x00, 'n', 's', 0x09, 0x00, 0x00, 0x00, 0xCC};
    UaReader r3 = {EXPANDED, sizeof(EXPANDED), 0, PROTO_FALSE};
    TEST_ASSERT_TRUE(protocore_ua_r_nodeid(&r3, &n));
    TEST_ASSERT_TRUE(n.numeric);
    TEST_ASSERT_EQUAL_UINT32(7, n.id);
    TEST_ASSERT_EQUAL_HEX8(0xCC, EXPANDED[r3.off]);

    // an unknown identifier kind is not decodable
    static const uint8_t BAD[] = {0x06, 0x00};
    UaReader r4 = {BAD, sizeof(BAD), 0, PROTO_FALSE};
    TEST_ASSERT_FALSE(protocore_ua_r_nodeid(&r4, &n));
}

// Part 6 sec 5.2.2.5: "a DateTime value shall be encoded as a 64-bit signed integer which
// represents the number of 100 nanosecond intervals since January 1, 1601 (UTC)".
//
// The 1601 -> 1970 offset, from the Gregorian calendar alone:
//   1601-01-01 .. 1970-01-01 = 369 years = 369 * 365            = 134685 days
//   leap days: years divisible by 4 in 1604..1968               = 92
//              less 1700, 1800, 1900 (centuries not /400)       = -3
//                                                                 --------
//                                                                 134774 days
//   134774 * 86400                                              = 11644473600 seconds
// so 1970-01-01T00:00:01Z is (11644473600 + 1) * 10^7 ticks.
void test_datetime_epoch(void)
{
    TEST_ASSERT_TRUE(116444736000000000LL == protocore_opcua_filetime_from_unix(1) - 10000000LL);
    TEST_ASSERT_TRUE(116444736010000000LL == protocore_opcua_filetime_from_unix(1));

    // one second later is exactly 10^7 ticks later
    TEST_ASSERT_TRUE(10000000LL == protocore_opcua_filetime_from_unix(2) - protocore_opcua_filetime_from_unix(1));

    // a caller with no wall clock yields 0 rather than a false 1601 timestamp
    TEST_ASSERT_TRUE(0 == protocore_opcua_filetime_from_unix(0));
    TEST_ASSERT_TRUE(0 == protocore_opcua_filetime_from_unix(-1));
}

// Part 6 sec 7.1.2.2: the header is a 3-octet MessageType, a reserved octet ('F' for a final
// chunk), and a MessageSize UInt32 that "includes the 8 bytes for the Message header".
void test_uacp_header(void)
{
    static const uint8_t HDR[8] = {'H', 'E', 'L', 'F', 0x38, 0x00, 0x00, 0x00};
    UaMsgHeader h;
    TEST_ASSERT_TRUE(protocore_opcua_parse_header(HDR, sizeof(HDR), &h));
    TEST_ASSERT_EQUAL_HEX8('H', h.type[0]);
    TEST_ASSERT_EQUAL_HEX8('E', h.type[1]);
    TEST_ASSERT_EQUAL_HEX8('L', h.type[2]);
    TEST_ASSERT_EQUAL_HEX8('F', h.chunk);
    TEST_ASSERT_EQUAL_UINT32(0x38u, h.size);

    // the size field is little-endian like every other integer
    static const uint8_t BIG[8] = {'M', 'S', 'G', 'C', 0x01, 0x02, 0x03, 0x04};
    TEST_ASSERT_TRUE(protocore_opcua_parse_header(BIG, sizeof(BIG), &h));
    TEST_ASSERT_EQUAL_HEX8('C', h.chunk);
    TEST_ASSERT_EQUAL_UINT32(0x04030201u, h.size);

    for (size_t n = 0; n < 8; n++)
    {
        TEST_ASSERT_FALSE(protocore_opcua_parse_header(HDR, n, &h));
    }
}

// Part 6 sec 7.1.2.3: the Hello body is ProtocolVersion, ReceiveBufferSize, SendBufferSize,
// MaxMessageSize, MaxChunkCount (all UInt32) then the EndpointUrl String.
void test_hello_parse(void)
{
    start("HEL");
    put_u32(0);        // ProtocolVersion
    put_u32(65536);    // ReceiveBufferSize
    put_u32(65536);    // SendBufferSize
    put_u32(16777216); // MaxMessageSize
    put_u32(5000);     // MaxChunkCount
    put_str("opc.tcp://192.168.1.85:4840");
    size_t n = finish();

    OpcUaHello hello;
    TEST_ASSERT_TRUE(protocore_opcua_parse_hello(g_msg, n, &hello));
    TEST_ASSERT_EQUAL_UINT32(0u, hello.protocol_version);
    TEST_ASSERT_EQUAL_UINT32(65536u, hello.recv_buf_size);
    TEST_ASSERT_EQUAL_UINT32(65536u, hello.send_buf_size);
    TEST_ASSERT_EQUAL_UINT32(16777216u, hello.max_msg_size);
    TEST_ASSERT_EQUAL_UINT32(5000u, hello.max_chunk_count);

    // a MessageSize that disagrees with the octets delivered is not a complete message
    TEST_ASSERT_FALSE(protocore_opcua_parse_hello(g_msg, n - 1, &hello));

    // a different MessageType is not a Hello
    g_msg[0] = 'A';
    g_msg[1] = 'C';
    g_msg[2] = 'K';
    TEST_ASSERT_FALSE(protocore_opcua_parse_hello(g_msg, n, &hello));

    // a Hello without room for its five sizes is malformed
    start("HEL");
    put_u32(0);
    put_u32(0);
    n = finish();
    TEST_ASSERT_FALSE(protocore_opcua_parse_hello(g_msg, n, &hello));
}

// Part 6 sec 7.1.2.4: the Acknowledge carries the same five UInt32 fields and no EndpointUrl, so it
// is exactly 8 + 20 = 28 octets. The server's ReceiveBufferSize answers the client's SendBufferSize
// and vice versa, each capped at what the server can actually hold.
void test_ack_negotiation(void)
{
    uint8_t out[64];
    OpcUaHello hello = {0, 65536, 65536, 16777216, 5000};
    size_t n = protocore_opcua_build_ack(&hello, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(28u, n);
    TEST_ASSERT_EQUAL_HEX8('A', out[0]);
    TEST_ASSERT_EQUAL_HEX8('C', out[1]);
    TEST_ASSERT_EQUAL_HEX8('K', out[2]);
    TEST_ASSERT_EQUAL_HEX8('F', out[3]);

    UaMsgHeader h;
    TEST_ASSERT_TRUE(protocore_opcua_parse_header(out, n, &h));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)n, h.size); // MessageSize includes the header

    UaReader r = {out + 8, n - 8, 0, PROTO_FALSE};
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_ua_r_u32(&r));                  // ProtocolVersion
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_OPCUA_BUF, protocore_ua_r_u32(&r)); // ReceiveBufferSize, capped
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_OPCUA_BUF, protocore_ua_r_u32(&r)); // SendBufferSize, capped
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_OPCUA_BUF, protocore_ua_r_u32(&r)); // MaxMessageSize
    TEST_ASSERT_EQUAL_UINT32(1u, protocore_ua_r_u32(&r));                  // MaxChunkCount
    TEST_ASSERT_FALSE(r.err);

    // a client asking for less than the server can hold gets what it asked for
    OpcUaHello small = {0, 4096, 2048, 4096, 1};
    TEST_ASSERT_EQUAL_UINT(28u, protocore_opcua_build_ack(&small, out, sizeof(out)));
    UaReader r2 = {out + 8, 20, 0, PROTO_FALSE};
    (void)protocore_ua_r_u32(&r2);
    TEST_ASSERT_EQUAL_UINT32(2048u, protocore_ua_r_u32(&r2)); // = the client's SendBufferSize
    TEST_ASSERT_EQUAL_UINT32(4096u, protocore_ua_r_u32(&r2)); // = the client's ReceiveBufferSize

    // a buffer too small for the fixed 28 octets writes nothing
    TEST_ASSERT_EQUAL_UINT(0u, protocore_opcua_build_ack(&hello, out, 27));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_opcua_build_ack(NULL, out, sizeof(out)));
}

// Part 6 sec 7.1.2.5: the Error message is an Error UInt32 then a Reason String. The four transport
// status codes are the OPC Foundation's own registry values (StatusCode.csv).
void test_error_message(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x807E0000u, PROTOCORE_OPCUA_BAD_TCP_MESSAGE_TYPE_INVALID);
    TEST_ASSERT_EQUAL_HEX32(0x80800000u, PROTOCORE_OPCUA_BAD_TCP_MESSAGE_TOO_LARGE);
    TEST_ASSERT_EQUAL_HEX32(0x80810000u, PROTOCORE_OPCUA_BAD_TCP_NOT_ENOUGH_RESOURCES);
    TEST_ASSERT_EQUAL_HEX32(0x80820000u, PROTOCORE_OPCUA_BAD_TCP_INTERNAL_ERROR);

    uint8_t out[128];
    size_t n = protocore_opcua_build_error(PROTOCORE_OPCUA_BAD_TCP_MESSAGE_TOO_LARGE, "too big", out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(8u + 4u + 4u + 7u, n);
    TEST_ASSERT_EQUAL_HEX8('E', out[0]);
    TEST_ASSERT_EQUAL_HEX8('R', out[1]);
    TEST_ASSERT_EQUAL_HEX8('R', out[2]);
    TEST_ASSERT_EQUAL_HEX8('F', out[3]);

    UaMsgHeader h;
    TEST_ASSERT_TRUE(protocore_opcua_parse_header(out, n, &h));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)n, h.size);

    UaReader r = {out + 8, n - 8, 0, PROTO_FALSE};
    TEST_ASSERT_EQUAL_HEX32(PROTOCORE_OPCUA_BAD_TCP_MESSAGE_TOO_LARGE, protocore_ua_r_u32(&r));
    char reason[32];
    int32_t rl = 0;
    TEST_ASSERT_TRUE(protocore_ua_r_string(&r, reason, sizeof(reason), &rl));
    TEST_ASSERT_EQUAL_STRING("too big", reason);

    // a null reason is the null String form, so the message is the header plus two UInt32
    n = protocore_opcua_build_error(PROTOCORE_OPCUA_BAD_TCP_INTERNAL_ERROR, NULL, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(16u, n);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[12]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[15]);

    TEST_ASSERT_EQUAL_UINT(0u, protocore_opcua_build_error(0, "x", out, 4));
}

// The service NodeIds this server dispatches on are the OPC Foundation's binary encoding ids
// (NodeIds.csv, namespace 0).
void test_service_nodeids_match_the_registry(void)
{
    TEST_ASSERT_EQUAL_INT(397, OPCUA_ID_SERVICE_FAULT);
    TEST_ASSERT_EQUAL_INT(428, OPCUA_ID_GET_ENDPOINTS_REQ);
    TEST_ASSERT_EQUAL_INT(431, OPCUA_ID_GET_ENDPOINTS_RESP);
    TEST_ASSERT_EQUAL_INT(446, OPCUA_ID_OPEN_REQ);
    TEST_ASSERT_EQUAL_INT(449, OPCUA_ID_OPEN_RESP);
    TEST_ASSERT_EQUAL_INT(461, OPCUA_ID_CREATE_SESSION_REQ);
    TEST_ASSERT_EQUAL_INT(464, OPCUA_ID_CREATE_SESSION_RESP);
    TEST_ASSERT_EQUAL_INT(467, OPCUA_ID_ACTIVATE_SESSION_REQ);
    TEST_ASSERT_EQUAL_INT(470, OPCUA_ID_ACTIVATE_SESSION_RESP);
    TEST_ASSERT_EQUAL_INT(473, OPCUA_ID_CLOSE_SESSION_REQ);
    TEST_ASSERT_EQUAL_INT(476, OPCUA_ID_CLOSE_SESSION_RESP);
    TEST_ASSERT_EQUAL_INT(527, OPCUA_ID_BROWSE_REQ);
    TEST_ASSERT_EQUAL_INT(530, OPCUA_ID_BROWSE_RESP);
    TEST_ASSERT_EQUAL_INT(631, OPCUA_ID_READ_REQ);
    TEST_ASSERT_EQUAL_INT(634, OPCUA_ID_READ_RESP);
    TEST_ASSERT_EQUAL_INT(673, OPCUA_ID_WRITE_REQ);
    TEST_ASSERT_EQUAL_INT(676, OPCUA_ID_WRITE_RESP);

    // and the StatusCodes the services return
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, OPCUA_STATUS_GOOD);
    TEST_ASSERT_EQUAL_HEX32(0x80340000u, OPCUA_STATUS_BAD_NODE_ID_UNKNOWN);
    TEST_ASSERT_EQUAL_HEX32(0x803B0000u, OPCUA_STATUS_BAD_NOT_WRITABLE);
    TEST_ASSERT_EQUAL_HEX32(0x800B0000u, OPCUA_STATUS_BAD_SERVICE_UNSUPPORTED);

    // Part 7: the SecurityPolicy None URI
    TEST_ASSERT_EQUAL_STRING("http://opcfoundation.org/UA/SecurityPolicy#None", OPCUA_POLICY_NONE_URI);
}

// Compose an OPN OpenSecureChannel request (Part 6 sec 6.7.2 asymmetric security header, then the
// sequence header, then the OpenSecureChannelRequest body of Part 4 sec 5.5.2).
static size_t build_open_request(uint32_t channel_id, uint32_t seq, uint32_t request_id, uint32_t handle,
                                 uint32_t req_type, uint32_t sec_mode, uint32_t lifetime)
{
    start("OPN");
    put_u32(channel_id);
    put_str(OPCUA_POLICY_NONE_URI); // SecurityPolicyUri
    put_str(NULL);                  // SenderCertificate
    put_str(NULL);                  // ReceiverCertificateThumbprint
    put_u32(seq);
    put_u32(request_id);
    put_nodeid(0, OPCUA_ID_OPEN_REQ);
    put_request_header(handle);
    put_u32(0);        // ClientProtocolVersion
    put_u32(req_type); // RequestType: 0 = Issue, 1 = Renew
    put_u32(sec_mode); // SecurityMode: 1 = None
    put_str(NULL);     // ClientNonce
    put_u32(lifetime);
    return finish();
}

// Part 4 sec 5.5.2: the OpenSecureChannel request fields the server needs to answer, and the
// response echoing the RequestId and RequestHandle with the token it issued.
void test_open_secure_channel(void)
{
    size_t n = build_open_request(0, 51, 1, 0xABCD, 0, 1, 3600000);
    OpcUaOpenChannel req;
    TEST_ASSERT_TRUE(protocore_opcua_parse_open(g_msg, n, &req));
    TEST_ASSERT_EQUAL_UINT32(0u, req.secure_channel_id);
    TEST_ASSERT_EQUAL_UINT32(51u, req.sequence_number);
    TEST_ASSERT_EQUAL_UINT32(1u, req.request_id);
    TEST_ASSERT_EQUAL_UINT32(0xABCDu, req.request_handle);
    TEST_ASSERT_EQUAL_UINT32(0u, req.security_token_request_type);
    TEST_ASSERT_EQUAL_UINT32(1u, req.message_security_mode);
    TEST_ASSERT_EQUAL_UINT32(3600000u, req.requested_lifetime);

    uint8_t out[512];
    size_t rn =
        protocore_opcua_build_open_response(&req, 0x1234, 0x5678, 1, 116444736010000000LL, 600000, out, sizeof(out));
    TEST_ASSERT_TRUE(rn > 8);
    TEST_ASSERT_EQUAL_HEX8('O', out[0]);
    TEST_ASSERT_EQUAL_HEX8('P', out[1]);
    TEST_ASSERT_EQUAL_HEX8('N', out[2]);
    UaMsgHeader h;
    TEST_ASSERT_TRUE(protocore_opcua_parse_header(out, rn, &h));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)rn, h.size); // the size field is patched to the real length

    UaReader r = {out + 8, rn - 8, 0, PROTO_FALSE};
    TEST_ASSERT_EQUAL_UINT32(0x1234u, protocore_ua_r_u32(&r)); // SecureChannelId
    char uri[128];
    int32_t ul = 0;
    TEST_ASSERT_TRUE(protocore_ua_r_string(&r, uri, sizeof(uri), &ul));
    TEST_ASSERT_EQUAL_STRING(OPCUA_POLICY_NONE_URI, uri);
    TEST_ASSERT_EQUAL_INT32(-1, protocore_ua_r_i32(&r));  // SenderCertificate: null
    TEST_ASSERT_EQUAL_INT32(-1, protocore_ua_r_i32(&r));  // ReceiverCertificateThumbprint: null
    TEST_ASSERT_EQUAL_UINT32(1u, protocore_ua_r_u32(&r)); // SequenceNumber
    TEST_ASSERT_EQUAL_UINT32(1u, protocore_ua_r_u32(&r)); // RequestId echoed
    UaNodeId tid;
    TEST_ASSERT_TRUE(protocore_ua_r_nodeid(&r, &tid));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)OPCUA_ID_OPEN_RESP, tid.id);
    TEST_ASSERT_TRUE(116444736010000000ULL == protocore_ua_r_u64(&r)); // ResponseHeader Timestamp
    TEST_ASSERT_EQUAL_UINT32(0xABCDu, protocore_ua_r_u32(&r));         // RequestHandle echoed
    TEST_ASSERT_EQUAL_UINT32(OPCUA_STATUS_GOOD, protocore_ua_r_u32(&r));
    TEST_ASSERT_FALSE(r.err);

    // a buffer that cannot hold the whole response writes nothing
    TEST_ASSERT_EQUAL_UINT(0u, protocore_opcua_build_open_response(&req, 1, 1, 1, 0, 1, out, 32));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_opcua_build_open_response(NULL, 1, 1, 1, 0, 1, out, sizeof(out)));
}

// An OPN whose body TypeId is not OpenSecureChannelRequest (i=446), or whose MessageType is not
// OPN, or whose MessageSize disagrees with the octets, is refused.
void test_open_secure_channel_rejects_wrong_frames(void)
{
    size_t n = build_open_request(0, 1, 1, 1, 0, 1, 60000);
    OpcUaOpenChannel req;
    TEST_ASSERT_TRUE(protocore_opcua_parse_open(g_msg, n, &req));

    TEST_ASSERT_FALSE(protocore_opcua_parse_open(g_msg, n - 1, &req));

    g_msg[0] = 'M';
    g_msg[1] = 'S';
    g_msg[2] = 'G';
    TEST_ASSERT_FALSE(protocore_opcua_parse_open(g_msg, n, &req));

    // rebuild with a CloseSecureChannel type id instead
    start("OPN");
    put_u32(0);
    put_str(OPCUA_POLICY_NONE_URI);
    put_str(NULL);
    put_str(NULL);
    put_u32(1);
    put_u32(1);
    put_nodeid(0, 452); // CloseSecureChannelRequest, not the open request
    put_request_header(1);
    put_u32(0);
    put_u32(0);
    put_u32(1);
    put_str(NULL);
    put_u32(60000);
    n = finish();
    TEST_ASSERT_FALSE(protocore_opcua_parse_open(g_msg, n, &req));
}

// Compose a MSG service request: the symmetric security + sequence headers, the body TypeId, and
// the RequestHeader every service request opens with (Part 6 sec 6.7.2).
static size_t build_msg(uint32_t channel, uint32_t token, uint32_t seq, uint32_t request_id, uint16_t type_id,
                        uint32_t handle)
{
    start("MSG");
    put_u32(channel);
    put_u32(token);
    put_u32(seq);
    put_u32(request_id);
    put_nodeid(0, type_id);
    put_request_header(handle);
    return finish();
}

void test_msg_envelope(void)
{
    size_t n = build_msg(0x1234, 0x5678, 7, 9, OPCUA_ID_CREATE_SESSION_REQ, 0x0F0F);
    OpcUaMsg m;
    TEST_ASSERT_TRUE(protocore_opcua_parse_msg(g_msg, n, &m));
    TEST_ASSERT_EQUAL_UINT32(0x1234u, m.secure_channel_id);
    TEST_ASSERT_EQUAL_UINT32(0x5678u, m.token_id);
    TEST_ASSERT_EQUAL_UINT32(7u, m.sequence_number);
    TEST_ASSERT_EQUAL_UINT32(9u, m.request_id);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)OPCUA_ID_CREATE_SESSION_REQ, m.type_id);
    TEST_ASSERT_EQUAL_UINT32(0x0F0Fu, m.request_handle);

    TEST_ASSERT_FALSE(protocore_opcua_parse_msg(g_msg, n - 1, &m));
    g_msg[0] = 'O';
    g_msg[1] = 'P';
    g_msg[2] = 'N';
    TEST_ASSERT_FALSE(protocore_opcua_parse_msg(g_msg, n, &m));
}

// Every MSG response carries the same envelope back: the SecureChannelId, TokenId and RequestId of
// the request, its own SequenceNumber, then the response TypeId and a ResponseHeader whose
// RequestHandle is the request's.
static void check_msg_response(const uint8_t *out, size_t n, const OpcUaMsg *req, uint32_t seq, uint32_t want_type,
                               uint32_t want_result)
{
    TEST_ASSERT_TRUE(n > 8);
    TEST_ASSERT_EQUAL_HEX8('M', out[0]);
    TEST_ASSERT_EQUAL_HEX8('S', out[1]);
    TEST_ASSERT_EQUAL_HEX8('G', out[2]);
    TEST_ASSERT_EQUAL_HEX8('F', out[3]);
    UaMsgHeader h;
    TEST_ASSERT_TRUE(protocore_opcua_parse_header(out, n, &h));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)n, h.size);

    UaReader r = {out + 8, n - 8, 0, PROTO_FALSE};
    TEST_ASSERT_EQUAL_UINT32(req->secure_channel_id, protocore_ua_r_u32(&r));
    TEST_ASSERT_EQUAL_UINT32(req->token_id, protocore_ua_r_u32(&r));
    TEST_ASSERT_EQUAL_UINT32(seq, protocore_ua_r_u32(&r));
    TEST_ASSERT_EQUAL_UINT32(req->request_id, protocore_ua_r_u32(&r));
    UaNodeId tid;
    TEST_ASSERT_TRUE(protocore_ua_r_nodeid(&r, &tid));
    TEST_ASSERT_EQUAL_UINT32(want_type, tid.id);
    (void)protocore_ua_r_u64(&r); // Timestamp
    TEST_ASSERT_EQUAL_UINT32(req->request_handle, protocore_ua_r_u32(&r));
    TEST_ASSERT_EQUAL_UINT32(want_result, protocore_ua_r_u32(&r));
    TEST_ASSERT_FALSE(r.err);
}

// Part 4 sec 5.6.3 / 5.6.4 / 5.6.5: the Session responses, each tagged with its registry TypeId.
void test_session_responses(void)
{
    uint8_t out[1024];
    OpcUaMsg req;

    size_t n = build_msg(1, 2, 3, 4, OPCUA_ID_CREATE_SESSION_REQ, 55);
    TEST_ASSERT_TRUE(protocore_opcua_parse_msg(g_msg, n, &req));
    OpcUaServerInfo info = {"opc.tcp://10.0.0.1:4840", "urn:protocore", "ProtoCore"};
    size_t rn = protocore_opcua_build_create_session_response(&req, 0x11, 0x22, 60000.0, &info, 1, 0, out, sizeof(out));
    check_msg_response(out, rn, &req, 1, OPCUA_ID_CREATE_SESSION_RESP, OPCUA_STATUS_GOOD);

    n = build_msg(1, 2, 4, 5, OPCUA_ID_ACTIVATE_SESSION_REQ, 56);
    TEST_ASSERT_TRUE(protocore_opcua_parse_msg(g_msg, n, &req));
    rn = protocore_opcua_build_activate_session_response(&req, 2, 0, out, sizeof(out));
    check_msg_response(out, rn, &req, 2, OPCUA_ID_ACTIVATE_SESSION_RESP, OPCUA_STATUS_GOOD);

    n = build_msg(1, 2, 5, 6, OPCUA_ID_CLOSE_SESSION_REQ, 57);
    TEST_ASSERT_TRUE(protocore_opcua_parse_msg(g_msg, n, &req));
    rn = protocore_opcua_build_close_session_response(&req, 3, 0, out, sizeof(out));
    check_msg_response(out, rn, &req, 3, OPCUA_ID_CLOSE_SESSION_RESP, OPCUA_STATUS_GOOD);

    n = build_msg(1, 2, 6, 7, OPCUA_ID_GET_ENDPOINTS_REQ, 58);
    TEST_ASSERT_TRUE(protocore_opcua_parse_msg(g_msg, n, &req));
    rn = protocore_opcua_build_get_endpoints_response(&req, &info, 4, 0, out, sizeof(out));
    check_msg_response(out, rn, &req, 4, OPCUA_ID_GET_ENDPOINTS_RESP, OPCUA_STATUS_GOOD);

    // an unknown service draws a ServiceFault carrying BadServiceUnsupported
    n = build_msg(1, 2, 7, 8, 999, 59);
    TEST_ASSERT_TRUE(protocore_opcua_parse_msg(g_msg, n, &req));
    rn = protocore_opcua_build_service_fault(&req, OPCUA_STATUS_BAD_SERVICE_UNSUPPORTED, 5, 0, out, sizeof(out));
    check_msg_response(out, rn, &req, 5, OPCUA_ID_SERVICE_FAULT, OPCUA_STATUS_BAD_SERVICE_UNSUPPORTED);

    // every builder refuses a buffer it cannot fit in
    TEST_ASSERT_EQUAL_UINT(0u, protocore_opcua_build_service_fault(&req, 0, 1, 0, out, 16));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_opcua_build_activate_session_response(&req, 1, 0, out, 16));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_opcua_build_close_session_response(NULL, 1, 0, out, sizeof(out)));
}

// Part 6 sec 5.2.2.16: a scalar Variant is an encoding byte holding the built-in type id, then the
// value. Each supported type must survive the encode/decode round trip.
void test_variant_round_trip(void)
{
    uint8_t buf[64];
    OpcUaVariant v;
    OpcUaVariant got;

    memset(&v, 0, sizeof(v));
    v.type = OPCUA_VAR_BOOL;
    v.b = PROTO_TRUE;
    UaWriter w = {buf, sizeof(buf), 0, PROTO_TRUE};
    protocore_ua_w_variant(&w, &v);
    TEST_ASSERT_EQUAL_HEX8(1, buf[0]); // the Boolean built-in type id
    UaReader r = {buf, w.n, 0, PROTO_FALSE};
    TEST_ASSERT_TRUE(protocore_ua_r_variant(&r, &got));
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_BOOL, got.type);
    TEST_ASSERT_TRUE(got.b);

    memset(&v, 0, sizeof(v));
    v.type = OPCUA_VAR_INT32;
    v.i32 = -12345;
    w.n = 0;
    w.ok = PROTO_TRUE;
    protocore_ua_w_variant(&w, &v);
    TEST_ASSERT_EQUAL_HEX8(6, buf[0]);
    UaReader r2 = {buf, w.n, 0, PROTO_FALSE};
    TEST_ASSERT_TRUE(protocore_ua_r_variant(&r2, &got));
    TEST_ASSERT_EQUAL_INT32(-12345, got.i32);

    memset(&v, 0, sizeof(v));
    v.type = OPCUA_VAR_DOUBLE;
    v.f64 = 3.14159265358979;
    w.n = 0;
    w.ok = PROTO_TRUE;
    protocore_ua_w_variant(&w, &v);
    TEST_ASSERT_EQUAL_HEX8(11, buf[0]);
    UaReader r3 = {buf, w.n, 0, PROTO_FALSE};
    TEST_ASSERT_TRUE(protocore_ua_r_variant(&r3, &got));
    TEST_ASSERT_TRUE(same_double(3.14159265358979, got.f64));

    memset(&v, 0, sizeof(v));
    v.type = OPCUA_VAR_STRING;
    v.str = "hello";
    v.str_len = 5;
    w.n = 0;
    w.ok = PROTO_TRUE;
    protocore_ua_w_variant(&w, &v);
    TEST_ASSERT_EQUAL_HEX8(12, buf[0]);
    UaReader r4 = {buf, w.n, 0, PROTO_FALSE};
    TEST_ASSERT_TRUE(protocore_ua_r_variant(&r4, &got));
    TEST_ASSERT_EQUAL_INT32(5, got.str_len);
    TEST_ASSERT_EQUAL_INT(0, memcmp(got.str, "hello", 5));

    // a null Variant is the encoding byte 0 alone
    w.n = 0;
    w.ok = PROTO_TRUE;
    protocore_ua_w_variant(&w, NULL);
    TEST_ASSERT_EQUAL_UINT(1u, w.n);
    TEST_ASSERT_EQUAL_HEX8(0, buf[0]);

    // sec 5.2.2.16: bit 7 of the encoding byte marks an array, which this scalar decoder refuses
    static const uint8_t ARRAY_VARIANT[5] = {0x86, 0x01, 0x00, 0x00, 0x00};
    UaReader r5 = {ARRAY_VARIANT, sizeof(ARRAY_VARIANT), 0, PROTO_FALSE};
    TEST_ASSERT_FALSE(protocore_ua_r_variant(&r5, &got));
}

// Part 6 sec 5.2.2.17: a DataValue opens with a bit mask naming the fields present - bit 0 Value,
// bit 1 StatusCode, bit 2 SourceTimestamp, bit 3 ServerTimestamp, bit 4 SourcePicoseconds, bit 5
// ServerPicoseconds. A Good status is omitted, so only the value bit is set.
void test_datavalue_mask(void)
{
    uint8_t buf[64];
    OpcUaVariant v;
    memset(&v, 0, sizeof(v));
    v.type = OPCUA_VAR_UINT32;
    v.u32 = 0xDEADBEEFu;

    UaWriter w = {buf, sizeof(buf), 0, PROTO_TRUE};
    protocore_ua_w_datavalue(&w, &v, OPCUA_STATUS_GOOD);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);
    TEST_ASSERT_EQUAL_UINT(6u, w.n); // mask + encoding byte + UInt32

    OpcUaVariant got;
    uint32_t status = 0xFFFFFFFFu;
    UaReader r = {buf, w.n, 0, PROTO_FALSE};
    TEST_ASSERT_TRUE(protocore_ua_r_datavalue(&r, &got, &status));
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_UINT32, got.type);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, got.u32);
    TEST_ASSERT_EQUAL_HEX32(OPCUA_STATUS_GOOD, status);

    // a Bad status sets bit 1 as well
    w.n = 0;
    w.ok = PROTO_TRUE;
    protocore_ua_w_datavalue(&w, &v, OPCUA_STATUS_BAD_NODE_ID_UNKNOWN);
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[0]);
    UaReader r2 = {buf, w.n, 0, PROTO_FALSE};
    TEST_ASSERT_TRUE(protocore_ua_r_datavalue(&r2, &got, &status));
    TEST_ASSERT_EQUAL_HEX32(OPCUA_STATUS_BAD_NODE_ID_UNKNOWN, status);

    // an error-only DataValue carries no Variant at all
    w.n = 0;
    w.ok = PROTO_TRUE;
    protocore_ua_w_datavalue(&w, NULL, OPCUA_STATUS_BAD_NODE_ID_UNKNOWN);
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[0]);
    TEST_ASSERT_EQUAL_UINT(5u, w.n);

    // the timestamp fields are consumed when their bits are set, so the reader lands past them
    static const uint8_t WITH_STAMPS[] = {
        0x0F,                                  // Value + StatusCode + Source + ServerTimestamp
        0x06, 0x2A, 0x00, 0x00, 0x00,          // Int32 42
        0x00, 0x00, 0x34, 0x80,                // StatusCode
        0,    0,    0,    0,    0,    0, 0, 0, // SourceTimestamp
        0,    0,    0,    0,    0,    0, 0, 0, // ServerTimestamp
    };
    UaReader r3 = {WITH_STAMPS, sizeof(WITH_STAMPS), 0, PROTO_FALSE};
    TEST_ASSERT_TRUE(protocore_ua_r_datavalue(&r3, &got, &status));
    TEST_ASSERT_EQUAL_INT32(42, got.i32);
    TEST_ASSERT_EQUAL_HEX32(0x80340000u, status);
    TEST_ASSERT_EQUAL_UINT(sizeof(WITH_STAMPS), r3.off);
}

// Part 4 sec 5.10.2: a ReadRequest carries MaxAge, TimestampsToReturn and the NodesToRead array of
// ReadValueId (NodeId, AttributeId, IndexRange, DataEncoding). The Value attribute is id 13.
static size_t build_read_request(int32_t count)
{
    start("MSG");
    put_u32(1);
    put_u32(2);
    put_u32(3);
    put_u32(4);
    put_nodeid(0, OPCUA_ID_READ_REQ);
    put_request_header(77);
    put_u64(0); // MaxAge (Double)
    put_u32(0); // TimestampsToReturn
    put_u32((uint32_t)count);
    for (int32_t i = 0; i < count; i++)
    {
        put_nodeid(1, (uint16_t)(100 + i)); // ReadValueId.NodeId
        put_u32(OPCUA_ATTR_VALUE);          // AttributeId
        put_str(NULL);                      // IndexRange
        put_u16(0);                         // DataEncoding.NamespaceIndex
        put_str(NULL);                      // DataEncoding.Name
    }
    return finish();
}

void test_read_request_and_response(void)
{
    TEST_ASSERT_EQUAL_INT(13, OPCUA_ATTR_VALUE); // Part 6 Table: the Value attribute id

    size_t n = build_read_request(2);
    OpcUaReadRequest req;
    TEST_ASSERT_TRUE(protocore_opcua_parse_read(g_msg, n, &req));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)OPCUA_ID_READ_REQ, req.msg.type_id);
    TEST_ASSERT_EQUAL_UINT32(2u, req.total);
    TEST_ASSERT_EQUAL_UINT32(2u, req.count);
    TEST_ASSERT_EQUAL_UINT16(1, req.items[0].ns);
    TEST_ASSERT_EQUAL_UINT32(100u, req.items[0].id);
    TEST_ASSERT_TRUE(req.items[0].numeric);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)OPCUA_ATTR_VALUE, req.items[0].attribute);
    TEST_ASSERT_EQUAL_UINT32(101u, req.items[1].id);

    OpcUaVariant values[2];
    memset(values, 0, sizeof(values));
    values[0].type = OPCUA_VAR_INT32;
    values[0].i32 = 7;
    values[1].type = OPCUA_VAR_NULL;
    uint32_t statuses[2] = {OPCUA_STATUS_GOOD, OPCUA_STATUS_BAD_NODE_ID_UNKNOWN};

    uint8_t out[512];
    size_t rn = protocore_opcua_build_read_response(&req, values, statuses, 9, 0, out, sizeof(out));
    check_msg_response(out, rn, &req.msg, 9, OPCUA_ID_READ_RESP, OPCUA_STATUS_GOOD);

    // the response carries one DataValue per captured node, in request order
    UaReader r = {out + 8, rn - 8, 0, PROTO_FALSE};
    for (int i = 0; i < 4; i++)
    {
        (void)protocore_ua_r_u32(&r); // channel, token, seq, request id
    }
    UaNodeId tid;
    TEST_ASSERT_TRUE(protocore_ua_r_nodeid(&r, &tid));
    (void)protocore_ua_r_u64(&r); // ResponseHeader Timestamp
    (void)protocore_ua_r_u32(&r); // RequestHandle
    (void)protocore_ua_r_u32(&r); // ServiceResult
    (void)protocore_ua_r_u8(&r);  // ServiceDiagnostics
    (void)protocore_ua_r_i32(&r); // StringTable
    UaNodeId ah;
    TEST_ASSERT_TRUE(protocore_ua_r_nodeid(&r, &ah));   // AdditionalHeader NodeId
    (void)protocore_ua_r_u8(&r);                        // ... ExtensionObject encoding byte
    TEST_ASSERT_EQUAL_INT32(2, protocore_ua_r_i32(&r)); // Results array length

    OpcUaVariant got;
    uint32_t st = 0;
    TEST_ASSERT_TRUE(protocore_ua_r_datavalue(&r, &got, &st));
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_INT32, got.type);
    TEST_ASSERT_EQUAL_INT32(7, got.i32);
    TEST_ASSERT_EQUAL_HEX32(OPCUA_STATUS_GOOD, st);
    TEST_ASSERT_TRUE(protocore_ua_r_datavalue(&r, &got, &st));
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_NULL, got.type);
    TEST_ASSERT_EQUAL_HEX32(OPCUA_STATUS_BAD_NODE_ID_UNKNOWN, st);
    TEST_ASSERT_FALSE(r.err);
}

// A request naming more nodes than the server's fixed table holds keeps the total it was asked for
// while capturing only what fits, so nothing is written past the array.
void test_read_request_is_clamped(void)
{
    size_t n = build_read_request(PROTOCORE_OPCUA_READ_MAX + 3);
    OpcUaReadRequest req;
    TEST_ASSERT_TRUE(protocore_opcua_parse_read(g_msg, n, &req));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(PROTOCORE_OPCUA_READ_MAX + 3), req.total);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)PROTOCORE_OPCUA_READ_MAX, req.count);
}

// Part 4 sec 5.8.2: a BrowseRequest carries a View, a RequestedMaxReferencesPerNode and the
// NodesToBrowse array of BrowseDescription. The response holds one BrowseResult per browsed node.
static size_t build_browse_request(int32_t count)
{
    start("MSG");
    put_u32(1);
    put_u32(2);
    put_u32(3);
    put_u32(4);
    put_nodeid(0, OPCUA_ID_BROWSE_REQ);
    put_request_header(88);
    put_u8(0x00); // ViewDescription.ViewId: Two Byte NodeId i=0
    put_u8(0x00);
    put_u64(0);  // View.Timestamp
    put_u32(0);  // View.ViewVersion
    put_u32(10); // RequestedMaxReferencesPerNode
    put_u32((uint32_t)count);
    for (int32_t i = 0; i < count; i++)
    {
        put_nodeid(0, (uint16_t)(85 + i)); // BrowseDescription.NodeId
        put_u32(0);                        // BrowseDirection
        put_u8(0x00);                      // ReferenceTypeId: Two Byte NodeId i=0
        put_u8(0x00);
        put_u8(0x01); // IncludeSubtypes
        put_u32(0);   // NodeClassMask
        put_u32(63);  // ResultMask
    }
    return finish();
}

static int32_t browse_handler(uint16_t ns, uint32_t id, OpcUaReference *out, uint32_t max)
{
    (void)ns;
    if (id != 85 || max < 1)
    {
        return -1; // an unknown node
    }
    out[0].ref_type_id = OPCUA_REFTYPE_ORGANIZES;
    out[0].is_forward = PROTO_TRUE;
    out[0].target_ns = 1;
    out[0].target_id = 1000;
    out[0].browse_name_ns = 1;
    out[0].browse_name = "Temperature";
    out[0].display_name = "Temperature";
    out[0].node_class = OPCUA_NODECLASS_VARIABLE;
    out[0].type_def_id = OPCUA_TYPEDEF_BASE_DATA_VARIABLE;
    return 1;
}

void test_browse_request_and_response(void)
{
    // Part 3 sec 8.30 NodeClass and Part 5 the standard ReferenceType / VariableType NodeIds
    TEST_ASSERT_EQUAL_INT(1, OPCUA_NODECLASS_OBJECT);
    TEST_ASSERT_EQUAL_INT(2, OPCUA_NODECLASS_VARIABLE);
    TEST_ASSERT_EQUAL_INT(35, OPCUA_REFTYPE_ORGANIZES);
    TEST_ASSERT_EQUAL_INT(47, OPCUA_REFTYPE_HAS_COMPONENT);
    TEST_ASSERT_EQUAL_INT(58, OPCUA_TYPEDEF_BASE_OBJECT);
    TEST_ASSERT_EQUAL_INT(63, OPCUA_TYPEDEF_BASE_DATA_VARIABLE);

    size_t n = build_browse_request(2);
    OpcUaBrowseRequest req;
    TEST_ASSERT_TRUE(protocore_opcua_parse_browse(g_msg, n, &req));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)OPCUA_ID_BROWSE_REQ, req.msg.type_id);
    TEST_ASSERT_EQUAL_UINT32(2u, req.total);
    TEST_ASSERT_EQUAL_UINT32(85u, req.items[0].id);
    TEST_ASSERT_EQUAL_UINT32(86u, req.items[1].id);

    uint8_t out[1024];
    size_t rn = protocore_opcua_build_browse_response(&req, browse_handler, 11, 0, out, sizeof(out));
    check_msg_response(out, rn, &req.msg, 11, OPCUA_ID_BROWSE_RESP, OPCUA_STATUS_GOOD);

    // a null handler still produces a well-formed response
    rn = protocore_opcua_build_browse_response(&req, NULL, 12, 0, out, sizeof(out));
    check_msg_response(out, rn, &req.msg, 12, OPCUA_ID_BROWSE_RESP, OPCUA_STATUS_GOOD);

    TEST_ASSERT_EQUAL_UINT(0u, protocore_opcua_build_browse_response(&req, browse_handler, 1, 0, out, 24));
}

// Part 4 sec 5.10.4: a WriteRequest carries NodesToWrite of WriteValue (NodeId, AttributeId,
// IndexRange, DataValue), and the response is one StatusCode per entry.
void test_write_request_and_response(void)
{
    start("MSG");
    put_u32(1);
    put_u32(2);
    put_u32(3);
    put_u32(4);
    put_nodeid(0, OPCUA_ID_WRITE_REQ);
    put_request_header(99);
    put_u32(2); // NodesToWrite length
    for (int i = 0; i < 2; i++)
    {
        put_nodeid(1, (uint16_t)(200 + i));
        put_u32(OPCUA_ATTR_VALUE);
        put_str(NULL);                 // IndexRange
        put_u8(0x01);                  // DataValue mask: Value present
        put_u8(OPCUA_VAR_INT32);       // Variant encoding byte
        put_u32((uint32_t)(1000 + i)); // Int32 value
    }
    size_t n = finish();

    OpcUaWriteRequest req;
    TEST_ASSERT_TRUE(protocore_opcua_parse_write(g_msg, n, &req));
    TEST_ASSERT_EQUAL_UINT32(2u, req.total);
    TEST_ASSERT_EQUAL_UINT32(2u, req.count);
    TEST_ASSERT_EQUAL_UINT32(200u, req.items[0].id);
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_INT32, req.items[0].value.type);
    TEST_ASSERT_EQUAL_INT32(1000, req.items[0].value.i32);
    TEST_ASSERT_EQUAL_INT32(1001, req.items[1].value.i32);

    uint32_t results[2] = {OPCUA_STATUS_GOOD, OPCUA_STATUS_BAD_NOT_WRITABLE};
    uint8_t out[512];
    size_t rn = protocore_opcua_build_write_response(&req, results, 13, 0, out, sizeof(out));
    check_msg_response(out, rn, &req.msg, 13, OPCUA_ID_WRITE_RESP, OPCUA_STATUS_GOOD);

    // the two StatusCodes are the last eight octets before the empty DiagnosticInfos array
    TEST_ASSERT_EQUAL_HEX32(OPCUA_STATUS_BAD_NOT_WRITABLE, (uint32_t)out[rn - 8] | ((uint32_t)out[rn - 7] << 8) |
                                                               ((uint32_t)out[rn - 6] << 16) |
                                                               ((uint32_t)out[rn - 5] << 24));

    // a null results array means every write succeeded
    rn = protocore_opcua_build_write_response(&req, NULL, 14, 0, out, sizeof(out));
    check_msg_response(out, rn, &req.msg, 14, OPCUA_ID_WRITE_RESP, OPCUA_STATUS_GOOD);
}

// The resolver setters accept a handler and a null, so an application can install and remove one.
void test_resolver_registration(void)
{
    protocore_opcua_set_browse_handler(browse_handler);
    protocore_opcua_set_browse_handler(NULL);
    protocore_opcua_set_read_handler(NULL);
    protocore_opcua_set_write_handler(NULL);
    protocore_opcua_set_endpoint_url("opc.tcp://127.0.0.1:4840");

    // the endpoint url the server advertises reaches the EndpointDescription encoder
    uint8_t buf[512];
    UaWriter w = {buf, sizeof(buf), 0, PROTO_TRUE};
    OpcUaServerInfo info = {"opc.tcp://127.0.0.1:4840", "urn:x", "X"};
    protocore_ua_w_endpoint_description(&w, &info);
    TEST_ASSERT_TRUE(w.ok);
    UaReader r = {buf, w.n, 0, PROTO_FALSE};
    char url[64];
    int32_t ul = 0;
    TEST_ASSERT_TRUE(protocore_ua_r_string(&r, url, sizeof(url), &ul));
    TEST_ASSERT_EQUAL_STRING("opc.tcp://127.0.0.1:4840", url);
}

// Part 6 sec 5.2.2.13 / 5.2.2.14: a QualifiedName is a UInt16 NamespaceIndex then a String Name,
// and a LocalizedText opens with a mask - bit 0 Locale present, bit 1 Text present.
void test_qualifiedname_and_localizedtext(void)
{
    uint8_t buf[64];
    UaWriter w = {buf, sizeof(buf), 0, PROTO_TRUE};
    protocore_ua_w_qualifiedname(&w, 3, "Name");
    UaReader r = {buf, w.n, 0, PROTO_FALSE};
    TEST_ASSERT_EQUAL_UINT16(3, protocore_ua_r_u16(&r));
    char name[16];
    int32_t nl = 0;
    TEST_ASSERT_TRUE(protocore_ua_r_string(&r, name, sizeof(name), &nl));
    TEST_ASSERT_EQUAL_STRING("Name", name);

    w.n = 0;
    w.ok = PROTO_TRUE;
    protocore_ua_w_localizedtext(&w, NULL, "Temperature");
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[0]); // Text only
    UaReader r2 = {buf + 1, w.n - 1, 0, PROTO_FALSE};
    char text[32];
    TEST_ASSERT_TRUE(protocore_ua_r_string(&r2, text, sizeof(text), &nl));
    TEST_ASSERT_EQUAL_STRING("Temperature", text);

    w.n = 0;
    w.ok = PROTO_TRUE;
    protocore_ua_w_localizedtext(&w, "en", "Temperature");
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[0]); // Locale and Text
}
