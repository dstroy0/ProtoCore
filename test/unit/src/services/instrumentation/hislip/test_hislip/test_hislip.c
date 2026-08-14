// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the HiSLIP message codec (services/instrumentation/hislip/hislip.h).
//
// The load-bearing case is test_ivi61_table2_header_layout. IVI-6.1 (HiSLIP 2.0, 2020-04-23) sec
// 2.3 Table 2 publishes the header as five fields at fixed octet offsets - Prologue 2 at 0, Message
// Type 1 at 2, Control Code 1 at 3, Message Parameter 4 at 4, Payload Length 8 at 8 - and states
// "All HiSLIP fields are marshaled onto the network in network order (big endian), most significant
// byte first", with the prologue "ASCII 'HS' encoded as a 16 bit value. 'H' is in the most
// significant network order position". A codec that puts any field at the wrong offset or in the
// wrong order desynchronizes the stream, which is precisely what the prologue exists to detect.
//
// The message type numbers are Table 4, "Message Type Value Definitions", transcribed. The
// MessageID rule is sec 6.2: "Clients shall maintain a MessageID count that is initially set to
// 0xffff ff00 ... increment the MessageID by two in an unsigned 32-bit sense (permitting
// wrap-around)". The port is sec 2.2, "the IANA assigned port number of 4880".

#include "services/instrumentation/hislip/hislip.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// IVI-6.1 sec 2.3 Table 2, field by field. The parameter and length below are chosen so every octet
// of both is distinct, which is what makes a swapped byte order visible.
void test_ivi61_table2_header_layout(void)
{
    uint8_t buf[PROTOCORE_HISLIP_HEADER_LEN];
    TEST_ASSERT_EQUAL_UINT(16u, PROTOCORE_HISLIP_HEADER_LEN);

    size_t n = protocore_hislip_build_header(buf, sizeof(buf), HISLIP_MSG_DATA_END, 0x5A, 0x01020304u,
                                             0x0102030405060708ull);
    TEST_ASSERT_EQUAL_UINT(16u, n);

    TEST_ASSERT_EQUAL_HEX8('H', buf[0]); // 'H' in the most significant position
    TEST_ASSERT_EQUAL_HEX8('S', buf[1]);
    TEST_ASSERT_EQUAL_HEX8(7, buf[2]);    // DataEnd, Table 4
    TEST_ASSERT_EQUAL_HEX8(0x5A, buf[3]); // ControlCode

    static const uint8_t PARAM[4] = {0x01, 0x02, 0x03, 0x04};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PARAM, buf + 4, 4);

    static const uint8_t LEN[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(LEN, buf + 8, 8);
}

// The same header read back: every field returns the value it was built from, at full width.
void test_header_round_trip(void)
{
    uint8_t buf[PROTOCORE_HISLIP_HEADER_LEN];
    HislipHeader h;
    protocore_hislip_build_header(buf, sizeof(buf), HISLIP_MSG_ASYNC_STATUS_RESPONSE, 0xFF, 0xFFFFFFFFu,
                                  0xFFFFFFFFFFFFFFFFull);
    TEST_ASSERT_TRUE(protocore_hislip_parse_header(buf, sizeof(buf), &h));
    TEST_ASSERT_EQUAL_INT(HISLIP_MSG_ASYNC_STATUS_RESPONSE, h.type);
    TEST_ASSERT_EQUAL_HEX8(0xFF, h.control);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, h.parameter);
    TEST_ASSERT_EQUAL_HEX64(0xFFFFFFFFFFFFFFFFull, h.payload_len);
}

// IVI-6.1 sec 2.3: the prologue exists "to facilitate HiSLIP devices detecting when they receive an
// ill-formed message or are out of sync", so anything but "HS" is refused, as is a short buffer.
void test_prologue_is_checked(void)
{
    uint8_t buf[PROTOCORE_HISLIP_HEADER_LEN];
    HislipHeader h;
    protocore_hislip_build_header(buf, sizeof(buf), HISLIP_MSG_DATA, 0, 0, 0);
    TEST_ASSERT_TRUE(protocore_hislip_parse_header(buf, sizeof(buf), &h));

    buf[0] = 'h'; // the prologue is the exact uppercase pair
    TEST_ASSERT_FALSE(protocore_hislip_parse_header(buf, sizeof(buf), &h));
    buf[0] = 'H';
    buf[1] = 's';
    TEST_ASSERT_FALSE(protocore_hislip_parse_header(buf, sizeof(buf), &h));
    buf[1] = 'S';
    TEST_ASSERT_TRUE(protocore_hislip_parse_header(buf, sizeof(buf), &h));

    for (size_t shorter = 0; shorter < PROTOCORE_HISLIP_HEADER_LEN; shorter++)
    {
        TEST_ASSERT_FALSE(protocore_hislip_parse_header(buf, shorter, &h));
    }
    TEST_ASSERT_FALSE(protocore_hislip_parse_header(NULL, 16, &h));
    TEST_ASSERT_FALSE(protocore_hislip_parse_header(buf, 16, NULL));
}

// IVI-6.1 Table 4, "Message Type Value Definitions": the decimal value of every message type, 0
// through 38. Codes 26 and above were added in HiSLIP 2.0.
void test_ivi61_table4_message_type_values(void)
{
    TEST_ASSERT_EQUAL_INT(0, HISLIP_MSG_INITIALIZE);
    TEST_ASSERT_EQUAL_INT(1, HISLIP_MSG_INITIALIZE_RESPONSE);
    TEST_ASSERT_EQUAL_INT(2, HISLIP_MSG_FATAL_ERROR);
    TEST_ASSERT_EQUAL_INT(3, HISLIP_MSG_ERROR);
    TEST_ASSERT_EQUAL_INT(4, HISLIP_MSG_ASYNC_LOCK);
    TEST_ASSERT_EQUAL_INT(5, HISLIP_MSG_ASYNC_LOCK_RESPONSE);
    TEST_ASSERT_EQUAL_INT(6, HISLIP_MSG_DATA);
    TEST_ASSERT_EQUAL_INT(7, HISLIP_MSG_DATA_END);
    TEST_ASSERT_EQUAL_INT(8, HISLIP_MSG_DEVICE_CLEAR_COMPLETE);
    TEST_ASSERT_EQUAL_INT(9, HISLIP_MSG_DEVICE_CLEAR_ACKNOWLEDGE);
    TEST_ASSERT_EQUAL_INT(10, HISLIP_MSG_ASYNC_REMOTE_LOCAL_CONTROL);
    TEST_ASSERT_EQUAL_INT(11, HISLIP_MSG_ASYNC_REMOTE_LOCAL_RESPONSE);
    TEST_ASSERT_EQUAL_INT(12, HISLIP_MSG_TRIGGER);
    TEST_ASSERT_EQUAL_INT(13, HISLIP_MSG_INTERRUPTED);
    TEST_ASSERT_EQUAL_INT(14, HISLIP_MSG_ASYNC_INTERRUPTED);
    TEST_ASSERT_EQUAL_INT(15, HISLIP_MSG_ASYNC_MAX_MSG_SIZE);
    TEST_ASSERT_EQUAL_INT(16, HISLIP_MSG_ASYNC_MAX_MSG_SIZE_RESPONSE);
    TEST_ASSERT_EQUAL_INT(17, HISLIP_MSG_ASYNC_INITIALIZE);
    TEST_ASSERT_EQUAL_INT(18, HISLIP_MSG_ASYNC_INITIALIZE_RESPONSE);
    TEST_ASSERT_EQUAL_INT(19, HISLIP_MSG_ASYNC_DEVICE_CLEAR);
    TEST_ASSERT_EQUAL_INT(20, HISLIP_MSG_ASYNC_SERVICE_REQUEST);
    TEST_ASSERT_EQUAL_INT(21, HISLIP_MSG_ASYNC_STATUS_QUERY);
    TEST_ASSERT_EQUAL_INT(22, HISLIP_MSG_ASYNC_STATUS_RESPONSE);
    TEST_ASSERT_EQUAL_INT(23, HISLIP_MSG_ASYNC_DEVICE_CLEAR_ACKNOWLEDGE);
    TEST_ASSERT_EQUAL_INT(24, HISLIP_MSG_ASYNC_LOCK_INFO);
    TEST_ASSERT_EQUAL_INT(25, HISLIP_MSG_ASYNC_LOCK_INFO_RESPONSE);
    TEST_ASSERT_EQUAL_INT(26, HISLIP_MSG_GET_DESCRIPTORS);
    TEST_ASSERT_EQUAL_INT(27, HISLIP_MSG_GET_DESCRIPTORS_RESPONSE);
    TEST_ASSERT_EQUAL_INT(28, HISLIP_MSG_START_TLS);
    TEST_ASSERT_EQUAL_INT(29, HISLIP_MSG_ASYNC_START_TLS);
    TEST_ASSERT_EQUAL_INT(30, HISLIP_MSG_ASYNC_START_TLS_RESPONSE);
    TEST_ASSERT_EQUAL_INT(31, HISLIP_MSG_END_TLS);
    TEST_ASSERT_EQUAL_INT(32, HISLIP_MSG_ASYNC_END_TLS);
    TEST_ASSERT_EQUAL_INT(33, HISLIP_MSG_ASYNC_END_TLS_RESPONSE);
    TEST_ASSERT_EQUAL_INT(34, HISLIP_MSG_GET_SASL_MECHANISM_LIST);
    TEST_ASSERT_EQUAL_INT(35, HISLIP_MSG_GET_SASL_MECHANISM_LIST_RESPONSE);
    TEST_ASSERT_EQUAL_INT(36, HISLIP_MSG_AUTHENTICATION_START);
    TEST_ASSERT_EQUAL_INT(37, HISLIP_MSG_AUTHENTICATION_EXCHANGE);
    TEST_ASSERT_EQUAL_INT(38, HISLIP_MSG_AUTHENTICATION_RESULT);
}

// IVI-6.1 sec 2.2: "the IANA assigned port number of 4880".
void test_ivi61_port_assignment(void)
{
    TEST_ASSERT_EQUAL_INT(4880, PROTOCORE_HISLIP_PORT);
}

// IVI-6.1 Table 3, Initialize row: "UpperWord : Client protocol version, LowerWord : Client-
// vendorID", payload "sub-address in ASCII, may be of zero length". So version 2.0 (0x0200) with
// vendor "PC" (0x5043) is parameter 0x02005043.
void test_initialize_packs_version_and_vendor(void)
{
    uint8_t buf[64];
    HislipHeader h;
    HislipInitialize init;

    size_t n = protocore_hislip_build_initialize(buf, sizeof(buf), PROTOCORE_HISLIP_VERSION_2_0, 0x5043, "hislip0");
    TEST_ASSERT_EQUAL_UINT(16u + 7u, n);
    TEST_ASSERT_TRUE(protocore_hislip_parse_header(buf, n, &h));
    TEST_ASSERT_EQUAL_INT(HISLIP_MSG_INITIALIZE, h.type);
    TEST_ASSERT_EQUAL_HEX32(0x02005043u, h.parameter);
    TEST_ASSERT_EQUAL_UINT64(7u, h.payload_len);
    TEST_ASSERT_EQUAL_MEMORY("hislip0", buf + 16, 7);

    TEST_ASSERT_TRUE(protocore_hislip_parse_initialize(buf, n, &init));
    TEST_ASSERT_EQUAL_HEX16(PROTOCORE_HISLIP_VERSION_2_0, init.protocol_version);
    TEST_ASSERT_EQUAL_HEX16(0x5043, init.vendor_id);
    TEST_ASSERT_EQUAL_UINT(7u, init.sub_address_len);
    TEST_ASSERT_EQUAL_MEMORY("hislip0", init.sub_address, 7);

    // "may be of zero length": no payload at all is a legal Initialize
    n = protocore_hislip_build_initialize(buf, sizeof(buf), PROTOCORE_HISLIP_VERSION_1_0, 0, NULL);
    TEST_ASSERT_EQUAL_UINT(16u, n);
    TEST_ASSERT_TRUE(protocore_hislip_parse_initialize(buf, n, &init));
    TEST_ASSERT_EQUAL_UINT(0u, init.sub_address_len);
    TEST_ASSERT_EQUAL_HEX16(PROTOCORE_HISLIP_VERSION_1_0, init.protocol_version);
}

// The protocol version words are `<major><minor>` in the high half of the parameter.
void test_version_words(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0100, PROTOCORE_HISLIP_VERSION_1_0);
    TEST_ASSERT_EQUAL_HEX16(0x0101, PROTOCORE_HISLIP_VERSION_1_1);
    TEST_ASSERT_EQUAL_HEX16(0x0200, PROTOCORE_HISLIP_VERSION_2_0);
}

// IVI-6.1 Table 3, InitializeResponse row: ControlCode bit 0 is the overlap preference, bit 1 the
// encryption-mandatory flag, and the parameter is "UpperWord : Negotiated protocol version,
// LowerWord : SessionID".
void test_initialize_response_control_bits_and_session(void)
{
    uint8_t buf[32];
    HislipInitializeResponse r;

    TEST_ASSERT_EQUAL_UINT(16u, protocore_hislip_build_initialize_response(
                                    buf, sizeof(buf), PROTOCORE_HISLIP_INITRESP_OVERLAP,
                                    PROTOCORE_HISLIP_VERSION_2_0, 0x1234));
    TEST_ASSERT_TRUE(protocore_hislip_parse_initialize_response(buf, 16, &r));
    TEST_ASSERT_EQUAL_HEX16(PROTOCORE_HISLIP_VERSION_2_0, r.protocol_version);
    TEST_ASSERT_EQUAL_HEX16(0x1234, r.session_id);
    TEST_ASSERT_TRUE(r.overlap);
    TEST_ASSERT_FALSE(r.encryption_mandatory);

    // bit 0 clear is "Prefer Synchronized"; bit 1 set is "encryption mandatory"
    protocore_hislip_build_initialize_response(buf, sizeof(buf), PROTOCORE_HISLIP_INITRESP_ENC_MANDATORY,
                                               PROTOCORE_HISLIP_VERSION_2_0, 1);
    TEST_ASSERT_TRUE(protocore_hislip_parse_initialize_response(buf, 16, &r));
    TEST_ASSERT_FALSE(r.overlap);
    TEST_ASSERT_TRUE(r.encryption_mandatory);

    TEST_ASSERT_EQUAL_HEX8(0x01, PROTOCORE_HISLIP_INITRESP_OVERLAP);
    TEST_ASSERT_EQUAL_HEX8(0x02, PROTOCORE_HISLIP_INITRESP_ENC_MANDATORY);
    TEST_ASSERT_EQUAL_HEX8(0x04, PROTOCORE_HISLIP_INITRESP_ENC_INITIAL);
}

// A parser keyed to one message type refuses another: an InitializeResponse is not an Initialize.
void test_typed_parsers_reject_the_wrong_type(void)
{
    uint8_t buf[32];
    HislipInitialize init;
    HislipInitializeResponse r;

    protocore_hislip_build_initialize_response(buf, sizeof(buf), 0, PROTOCORE_HISLIP_VERSION_1_0, 1);
    TEST_ASSERT_FALSE(protocore_hislip_parse_initialize(buf, 16, &init));

    protocore_hislip_build_initialize(buf, sizeof(buf), PROTOCORE_HISLIP_VERSION_1_0, 0, NULL);
    TEST_ASSERT_FALSE(protocore_hislip_parse_initialize_response(buf, 16, &r));
}

// A declared payload longer than the octets present is refused rather than read past.
void test_initialize_refuses_a_short_payload(void)
{
    uint8_t buf[64];
    HislipInitialize init;
    size_t n = protocore_hislip_build_initialize(buf, sizeof(buf), PROTOCORE_HISLIP_VERSION_1_0, 1, "hislip0");
    TEST_ASSERT_TRUE(protocore_hislip_parse_initialize(buf, n, &init));
    for (size_t shorter = PROTOCORE_HISLIP_HEADER_LEN; shorter < n; shorter++)
    {
        TEST_ASSERT_FALSE(protocore_hislip_parse_initialize(buf, shorter, &init));
    }
}

// IVI-6.1 sec 2.2 / Table 3: an AsyncInitialize carries the SessionID in the parameter and no
// payload; the response carries the server's vendor id.
void test_async_initialize_pair(void)
{
    uint8_t buf[32];
    HislipHeader h;

    TEST_ASSERT_EQUAL_UINT(16u, protocore_hislip_build_async_initialize(buf, sizeof(buf), 0xBEEF));
    TEST_ASSERT_TRUE(protocore_hislip_parse_header(buf, 16, &h));
    TEST_ASSERT_EQUAL_INT(HISLIP_MSG_ASYNC_INITIALIZE, h.type);
    TEST_ASSERT_EQUAL_HEX32(0x0000BEEFu, h.parameter);
    TEST_ASSERT_EQUAL_UINT64(0u, h.payload_len);

    TEST_ASSERT_EQUAL_UINT(16u, protocore_hislip_build_async_initialize_response(buf, sizeof(buf), 0, 0x4B53));
    TEST_ASSERT_TRUE(protocore_hislip_parse_header(buf, 16, &h));
    TEST_ASSERT_EQUAL_INT(HISLIP_MSG_ASYNC_INITIALIZE_RESPONSE, h.type);
    TEST_ASSERT_EQUAL_HEX32(0x00004B53u, h.parameter);
}

// IVI-6.1 Table 3, Data / DataEnd rows: the MessageParameter is the MessageID and ControlCode bit 0
// reports whether a Response Message Terminator was delivered. DataEND is type 7, Data type 6.
void test_data_and_data_end(void)
{
    static const uint8_t SCPI[] = {'*', 'I', 'D', 'N', '?', '\n'};
    uint8_t buf[64];
    HislipHeader h;

    size_t n = protocore_hislip_build_data(buf, sizeof(buf), PROTO_TRUE, 0, 0xFFFFFF00u, SCPI, sizeof(SCPI));
    TEST_ASSERT_EQUAL_UINT(16u + sizeof(SCPI), n);
    TEST_ASSERT_TRUE(protocore_hislip_parse_header(buf, n, &h));
    TEST_ASSERT_EQUAL_INT(HISLIP_MSG_DATA_END, h.type);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFF00u, h.parameter);
    TEST_ASSERT_EQUAL_UINT64(sizeof(SCPI), h.payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SCPI, buf + 16, sizeof(SCPI));

    n = protocore_hislip_build_data(buf, sizeof(buf), PROTO_FALSE, PROTOCORE_HISLIP_DATA_RMT_DELIVERED, 2, SCPI,
                                    sizeof(SCPI));
    TEST_ASSERT_TRUE(protocore_hislip_parse_header(buf, n, &h));
    TEST_ASSERT_EQUAL_INT(HISLIP_MSG_DATA, h.type);
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_HISLIP_DATA_RMT_DELIVERED, h.control);
    TEST_ASSERT_EQUAL_HEX8(0x01, PROTOCORE_HISLIP_DATA_RMT_DELIVERED);

    // an empty payload is legal and the length field says so
    n = protocore_hislip_build_data(buf, sizeof(buf), PROTO_TRUE, 0, 0, NULL, 0);
    TEST_ASSERT_EQUAL_UINT(16u, n);
    TEST_ASSERT_TRUE(protocore_hislip_parse_header(buf, n, &h));
    TEST_ASSERT_EQUAL_UINT64(0u, h.payload_len);
}

// IVI-6.1 sec 6.2: the count "is initially set to 0xffff ff00" and each Data / DataEND / Trigger
// increments it "by two in an unsigned 32-bit sense (permitting wrap-around)". From the initial
// value that is 0xffffff00, 0xffffff02, ... 0xfffffffe, then 0x00000000.
void test_message_id_increments_by_two_and_wraps(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFF00u, PROTOCORE_HISLIP_MESSAGE_ID_INIT);

    uint32_t id = PROTOCORE_HISLIP_MESSAGE_ID_INIT;
    for (int i = 0; i < 128; i++) // 0xffffff00 + 128*2 = 0x100000000, i.e. back to 0
    {
        TEST_ASSERT_EQUAL_HEX32(0xFFFFFF00u + (uint32_t)(i * 2), id);
        id = protocore_hislip_next_message_id(id);
    }
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, id);
    TEST_ASSERT_EQUAL_HEX32(0x00000002u, protocore_hislip_next_message_id(id));

    // sec 6.2 also gives the value used after initialization and device clear: 0xffffff00 - 2
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFEFEu, PROTOCORE_HISLIP_MESSAGE_ID_INIT - 2u);
}

// Builders refuse rather than truncating: one octet short of the exact need reports 0. A truncated
// header is exactly the out-of-sync condition the prologue is meant to catch.
void test_builders_refuse_a_short_buffer(void)
{
    static const uint8_t P[] = {1, 2, 3, 4};
    uint8_t buf[64];

    TEST_ASSERT_EQUAL_UINT(16u, protocore_hislip_build_header(buf, 16, HISLIP_MSG_DATA, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_hislip_build_header(buf, 15, HISLIP_MSG_DATA, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_hislip_build_header(NULL, 16, HISLIP_MSG_DATA, 0, 0, 0));

    TEST_ASSERT_EQUAL_UINT(20u, protocore_hislip_build_data(buf, 20, PROTO_TRUE, 0, 0, P, sizeof(P)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_hislip_build_data(buf, 19, PROTO_TRUE, 0, 0, P, sizeof(P)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_hislip_build_data(buf, 64, PROTO_TRUE, 0, 0, NULL, 4));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_hislip_build_data(NULL, 64, PROTO_TRUE, 0, 0, P, sizeof(P)));

    TEST_ASSERT_EQUAL_UINT(0u, protocore_hislip_build_initialize_response(buf, 15, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_hislip_build_async_initialize(buf, 15, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_hislip_build_async_initialize_response(buf, 15, 0, 0));
}

// Sec 2.3 note: "The message type is copied through even if beyond 38" is the codec's forward-
// compatibility contract - Table 4 reserves 39-127 and 128-255 for vendor use, so a parser must
// hand an unknown type up rather than dropping the message.
void test_unknown_message_type_is_carried_through(void)
{
    uint8_t buf[PROTOCORE_HISLIP_HEADER_LEN];
    HislipHeader h;
    protocore_hislip_build_header(buf, sizeof(buf), (HislipMsg)200, 0, 0, 0); // vendor-specific range
    TEST_ASSERT_TRUE(protocore_hislip_parse_header(buf, sizeof(buf), &h));
    TEST_ASSERT_EQUAL_INT(200, (int)h.type);
}
