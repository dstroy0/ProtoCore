// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the QUIC transport-parameter codec
// (network_drivers/presentation/http/http3/quic_tp.h).
//
// RFC 9000 sec 18.2 publishes each parameter's identifier and its value-when-absent, and those two
// tables are what a peer's stack assumes about ours. test_rfc9000_18_2_defaults and
// test_hand_built_wire_string are the load-bearing cases: the first asserts every default the
// section states verbatim, the second parses an ID/Length/Value string written out by hand from the
// sec 18 figure so the parser is checked against the spec's byte layout, not against our encoder.

#include "network_drivers/presentation/http/http3/quic_tp.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// RFC 9000 sec 18.2, one clause per line:
//   "Transport parameters have a default value of 0 if the transport parameter is absent, unless
//    otherwise stated."
//   max_udp_payload_size: "The default for this parameter is the maximum permitted UDP payload
//    of 65527."
//   ack_delay_exponent:   "If this value is absent, a default value of 3 is assumed".
//   max_ack_delay:        "If this value is absent, a default of 25 milliseconds is assumed."
//   active_connection_id_limit: "If this transport parameter is absent, a default of 2 is assumed."
void test_rfc9000_18_2_defaults(void)
{
    QuicTransportParams tp;
    memset(&tp, 0xAA, sizeof(tp));
    protocore_quic_tp_defaults(&tp);

    TEST_ASSERT_EQUAL_UINT64(0u, tp.max_idle_timeout);
    TEST_ASSERT_EQUAL_UINT64(65527u, tp.max_udp_payload_size);
    TEST_ASSERT_EQUAL_UINT64(0u, tp.initial_max_data);
    TEST_ASSERT_EQUAL_UINT64(0u, tp.initial_max_sd_bidi_local);
    TEST_ASSERT_EQUAL_UINT64(0u, tp.initial_max_sd_bidi_remote);
    TEST_ASSERT_EQUAL_UINT64(0u, tp.initial_max_sd_uni);
    TEST_ASSERT_EQUAL_UINT64(0u, tp.initial_max_streams_bidi);
    TEST_ASSERT_EQUAL_UINT64(0u, tp.initial_max_streams_uni);
    TEST_ASSERT_EQUAL_UINT64(3u, tp.ack_delay_exponent);
    TEST_ASSERT_EQUAL_UINT64(25u, tp.max_ack_delay);
    TEST_ASSERT_EQUAL_UINT64(2u, tp.active_connection_id_limit);
    TEST_ASSERT_FALSE(tp.disable_active_migration);
    TEST_ASSERT_FALSE(tp.has_original_dcid);
    TEST_ASSERT_FALSE(tp.has_initial_scid);
    TEST_ASSERT_FALSE(tp.has_retry_scid);

    // An empty parameter string leaves every default in place: nothing present, nothing overridden.
    QuicTransportParams parsed;
    TEST_ASSERT_TRUE(protocore_quic_tp_parse(NULL, 0, &parsed));
    TEST_ASSERT_EQUAL_UINT64(65527u, parsed.max_udp_payload_size);
    TEST_ASSERT_EQUAL_UINT64(3u, parsed.ack_delay_exponent);
    TEST_ASSERT_EQUAL_UINT64(25u, parsed.max_ack_delay);
    TEST_ASSERT_EQUAL_UINT64(2u, parsed.active_connection_id_limit);
}

// RFC 9000 sec 18 Figure 21: "Transport Parameter { Transport Parameter ID (i), Transport Parameter
// Length (i), Transport Parameter Value (..) }", every (i) field a sec 16 varint.
//
// Written out by hand from that figure and the sec 18.2 identifier list:
//   0x01 0x02 0x67 0x10   max_idle_timeout      = varint(0x2710)  = 10000 ms
//   0x03 0x02 0x45 0xc0   max_udp_payload_size  = varint(0x05c0)  = 1472
//   0x04 0x04 0x80 0x01 0x00 0x00
//                         initial_max_data      = varint(0x010000) = 65536
//   0x0a 0x01 0x00        ack_delay_exponent    = varint(0)       = 0
//   0x0b 0x01 0x0a        max_ack_delay         = varint(10)      = 10 ms
//   0x0c 0x00             disable_active_migration, "a zero-length value"
//   0x0e 0x01 0x04        active_connection_id_limit = varint(4)
//   0x0f 0x04 de ad be ef initial_source_connection_id (4 octets)
//
// The two-byte varints follow Table 4: 0x6710 is prefix 01 over 0x2710, 0x45c0 is prefix 01 over
// 0x05c0. The four-byte one is prefix 10 over 0x00010000.
void test_hand_built_wire_string(void)
{
    static const uint8_t WIRE[] = {
        0x01, 0x02, 0x67, 0x10,                         // max_idle_timeout = 10000
        0x03, 0x02, 0x45, 0xc0,                         // max_udp_payload_size = 1472
        0x04, 0x04, 0x80, 0x01, 0x00, 0x00,             // initial_max_data = 65536
        0x0a, 0x01, 0x00,                               // ack_delay_exponent = 0
        0x0b, 0x01, 0x0a,                               // max_ack_delay = 10
        0x0c, 0x00,                                     // disable_active_migration
        0x0e, 0x01, 0x04,                               // active_connection_id_limit = 4
        0x0f, 0x04, 0xde, 0xad, 0xbe, 0xef,             // initial_source_connection_id
    };
    static const uint8_t SCID[4] = {0xde, 0xad, 0xbe, 0xef};

    QuicTransportParams tp;
    TEST_ASSERT_TRUE(protocore_quic_tp_parse(WIRE, sizeof(WIRE), &tp));
    TEST_ASSERT_EQUAL_UINT64(10000u, tp.max_idle_timeout);
    TEST_ASSERT_EQUAL_UINT64(1472u, tp.max_udp_payload_size);
    TEST_ASSERT_EQUAL_UINT64(65536u, tp.initial_max_data);
    TEST_ASSERT_EQUAL_UINT64(0u, tp.ack_delay_exponent);
    TEST_ASSERT_EQUAL_UINT64(10u, tp.max_ack_delay);
    TEST_ASSERT_TRUE(tp.disable_active_migration);
    TEST_ASSERT_EQUAL_UINT64(4u, tp.active_connection_id_limit);
    TEST_ASSERT_TRUE(tp.has_initial_scid);
    TEST_ASSERT_EQUAL_UINT8(4u, tp.initial_scid_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SCID, tp.initial_scid, 4);

    // Nothing named in the string keeps its sec 18.2 default.
    TEST_ASSERT_EQUAL_UINT64(0u, tp.initial_max_streams_bidi);
    TEST_ASSERT_FALSE(tp.has_original_dcid);
    TEST_ASSERT_FALSE(tp.has_retry_scid);
}

// Encode then parse must return every value handed in: the identity a peer's stack depends on.
void test_encode_parse_round_trip(void)
{
    QuicTransportParams out;
    protocore_quic_tp_defaults(&out);
    out.has_original_dcid = PROTO_TRUE;
    out.original_dcid_len = 8;
    for (uint8_t i = 0; i < 8; i++)
    {
        out.original_dcid[i] = (uint8_t)(0x10 + i);
    }
    out.has_initial_scid = PROTO_TRUE;
    // sec 17.2: "In QUIC version 1, this value MUST NOT exceed 20 bytes."
    out.initial_scid_len = QUIC_MAX_CID_LEN;
    for (uint8_t i = 0; i < QUIC_MAX_CID_LEN; i++)
    {
        out.initial_scid[i] = (uint8_t)(0xA0 + i);
    }
    out.has_retry_scid = PROTO_TRUE;
    out.retry_scid_len = 1;
    out.retry_scid[0] = 0x5a;

    out.max_idle_timeout = 30000;
    out.max_udp_payload_size = 1200; // sec 18.2: "Values below 1200 are invalid", so 1200 is legal
    out.initial_max_data = 1048576;
    out.initial_max_sd_bidi_local = 262144;
    out.initial_max_sd_bidi_remote = 131072;
    out.initial_max_sd_uni = 65536;
    out.initial_max_streams_bidi = 100;
    out.initial_max_streams_uni = 3;
    out.active_connection_id_limit = 8;
    out.disable_active_migration = PROTO_TRUE;

    uint8_t wire[512];
    size_t n = protocore_quic_tp_encode(&out, wire, sizeof(wire));
    TEST_ASSERT_TRUE(n > 0);

    QuicTransportParams in;
    TEST_ASSERT_TRUE(protocore_quic_tp_parse(wire, n, &in));

    TEST_ASSERT_TRUE(in.has_original_dcid);
    TEST_ASSERT_EQUAL_UINT8(8u, in.original_dcid_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(out.original_dcid, in.original_dcid, 8);
    TEST_ASSERT_TRUE(in.has_initial_scid);
    TEST_ASSERT_EQUAL_UINT8(QUIC_MAX_CID_LEN, in.initial_scid_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(out.initial_scid, in.initial_scid, QUIC_MAX_CID_LEN);
    TEST_ASSERT_TRUE(in.has_retry_scid);
    TEST_ASSERT_EQUAL_UINT8(1u, in.retry_scid_len);
    TEST_ASSERT_EQUAL_HEX8(0x5a, in.retry_scid[0]);

    TEST_ASSERT_EQUAL_UINT64(30000u, in.max_idle_timeout);
    TEST_ASSERT_EQUAL_UINT64(1200u, in.max_udp_payload_size);
    TEST_ASSERT_EQUAL_UINT64(1048576u, in.initial_max_data);
    TEST_ASSERT_EQUAL_UINT64(262144u, in.initial_max_sd_bidi_local);
    TEST_ASSERT_EQUAL_UINT64(131072u, in.initial_max_sd_bidi_remote);
    TEST_ASSERT_EQUAL_UINT64(65536u, in.initial_max_sd_uni);
    TEST_ASSERT_EQUAL_UINT64(100u, in.initial_max_streams_bidi);
    TEST_ASSERT_EQUAL_UINT64(3u, in.initial_max_streams_uni);
    TEST_ASSERT_EQUAL_UINT64(8u, in.active_connection_id_limit);
    TEST_ASSERT_TRUE(in.disable_active_migration);
}

// sec 18.2: disable_active_migration "is included if the endpoint does not support active
// connection migration". Absent from the encoding when the flag is clear, so the peer's parse of
// our own output leaves the flag false.
void test_migration_flag_is_absent_when_clear(void)
{
    QuicTransportParams out;
    protocore_quic_tp_defaults(&out);
    out.disable_active_migration = PROTO_FALSE;

    uint8_t wire[256];
    size_t n = protocore_quic_tp_encode(&out, wire, sizeof(wire));
    TEST_ASSERT_TRUE(n > 0);

    QuicTransportParams in;
    TEST_ASSERT_TRUE(protocore_quic_tp_parse(wire, n, &in));
    TEST_ASSERT_FALSE(in.disable_active_migration);
}

// sec 18.1: "Transport parameters with an identifier of the form 31 * N + 27 ... are reserved to
// exercise the requirement that unknown transport parameters be ignored." N = 0, 1, 2 give 27, 58
// and 89; each carries arbitrary bytes and must not disturb the parse of a real parameter beside it.
void test_reserved_ids_are_ignored(void)
{
    static const uint8_t WIRE[] = {
        0x1b, 0x03, 0xde, 0xad, 0xbe,       // id 27  = 31*0 + 27, three arbitrary octets
        0x0b, 0x01, 0x0a,                   // max_ack_delay = 10
        0x40, 0x3a, 0x00,                   // id 58  = 31*1 + 27, zero length (2-byte varint id)
        0x40, 0x59, 0x01, 0xff,             // id 89  = 31*2 + 27, one octet
        0x0e, 0x01, 0x02,                   // active_connection_id_limit = 2
    };
    QuicTransportParams tp;
    TEST_ASSERT_TRUE(protocore_quic_tp_parse(WIRE, sizeof(WIRE), &tp));
    TEST_ASSERT_EQUAL_UINT64(10u, tp.max_ack_delay);
    TEST_ASSERT_EQUAL_UINT64(2u, tp.active_connection_id_limit);
}

// The sec 18.2 legality limits, each asserted at the first illegal value and at the last legal one.
void test_out_of_range_values_are_rejected(void)
{
    QuicTransportParams tp;

    // "Values above 20 are invalid" (ack_delay_exponent).
    static const uint8_t EXP20[] = {0x0a, 0x01, 0x14};
    static const uint8_t EXP21[] = {0x0a, 0x01, 0x15};
    TEST_ASSERT_TRUE(protocore_quic_tp_parse(EXP20, sizeof(EXP20), &tp));
    TEST_ASSERT_EQUAL_UINT64(20u, tp.ack_delay_exponent);
    TEST_ASSERT_FALSE(protocore_quic_tp_parse(EXP21, sizeof(EXP21), &tp));

    // "Values of 2^14 or greater are invalid" (max_ack_delay). 2^14-1 = 16383 = varint 0x7fff,
    // 2^14 = 16384 = varint 0x80004000.
    static const uint8_t DELAY_MAX[] = {0x0b, 0x02, 0x7f, 0xff};
    static const uint8_t DELAY_OVER[] = {0x0b, 0x04, 0x80, 0x00, 0x40, 0x00};
    TEST_ASSERT_TRUE(protocore_quic_tp_parse(DELAY_MAX, sizeof(DELAY_MAX), &tp));
    TEST_ASSERT_EQUAL_UINT64(16383u, tp.max_ack_delay);
    TEST_ASSERT_FALSE(protocore_quic_tp_parse(DELAY_OVER, sizeof(DELAY_OVER), &tp));

    // "Values below 1200 are invalid" (max_udp_payload_size). 1200 = 0x4b0 -> varint 0x44b0,
    // 1199 = 0x4af -> varint 0x44af.
    static const uint8_t MPS_1200[] = {0x03, 0x02, 0x44, 0xb0};
    static const uint8_t MPS_1199[] = {0x03, 0x02, 0x44, 0xaf};
    TEST_ASSERT_TRUE(protocore_quic_tp_parse(MPS_1200, sizeof(MPS_1200), &tp));
    TEST_ASSERT_EQUAL_UINT64(1200u, tp.max_udp_payload_size);
    TEST_ASSERT_FALSE(protocore_quic_tp_parse(MPS_1199, sizeof(MPS_1199), &tp));

    // "The value of the active_connection_id_limit parameter MUST be at least 2."
    static const uint8_t CID_2[] = {0x0e, 0x01, 0x02};
    static const uint8_t CID_1[] = {0x0e, 0x01, 0x01};
    static const uint8_t CID_0[] = {0x0e, 0x01, 0x00};
    TEST_ASSERT_TRUE(protocore_quic_tp_parse(CID_2, sizeof(CID_2), &tp));
    TEST_ASSERT_FALSE(protocore_quic_tp_parse(CID_1, sizeof(CID_1), &tp));
    TEST_ASSERT_FALSE(protocore_quic_tp_parse(CID_0, sizeof(CID_0), &tp));

    // "This parameter is a zero-length value" (disable_active_migration).
    static const uint8_t MIG_OK[] = {0x0c, 0x00};
    static const uint8_t MIG_BAD[] = {0x0c, 0x01, 0x00};
    TEST_ASSERT_TRUE(protocore_quic_tp_parse(MIG_OK, sizeof(MIG_OK), &tp));
    TEST_ASSERT_FALSE(protocore_quic_tp_parse(MIG_BAD, sizeof(MIG_BAD), &tp));
}

// sec 17.2: a version 1 connection ID "MUST NOT exceed 20 bytes". 20 parses, 21 does not.
void test_oversized_connection_id_is_rejected(void)
{
    uint8_t wire[3 + QUIC_MAX_CID_LEN + 1];
    QuicTransportParams tp;

    wire[0] = QUIC_TP_INITIAL_SCID;
    wire[1] = QUIC_MAX_CID_LEN; // varint(20), one byte
    memset(wire + 2, 0x11, QUIC_MAX_CID_LEN);
    TEST_ASSERT_TRUE(protocore_quic_tp_parse(wire, 2 + QUIC_MAX_CID_LEN, &tp));
    TEST_ASSERT_EQUAL_UINT8(QUIC_MAX_CID_LEN, tp.initial_scid_len);

    wire[1] = QUIC_MAX_CID_LEN + 1;
    memset(wire + 2, 0x11, QUIC_MAX_CID_LEN + 1);
    TEST_ASSERT_FALSE(protocore_quic_tp_parse(wire, 3 + QUIC_MAX_CID_LEN, &tp));

    // A zero-length connection ID is legal: sec 18.2 speaks of "an endpoint [that] issues a
    // zero-length connection ID".
    static const uint8_t ZERO[] = {QUIC_TP_INITIAL_SCID, 0x00};
    TEST_ASSERT_TRUE(protocore_quic_tp_parse(ZERO, sizeof(ZERO), &tp));
    TEST_ASSERT_TRUE(tp.has_initial_scid);
    TEST_ASSERT_EQUAL_UINT8(0u, tp.initial_scid_len);
}

// sec 7.4: "An endpoint MUST NOT send a parameter more than once in a given transport parameters
// extension. An endpoint SHOULD treat receipt of duplicate transport parameters as a connection
// error of type TRANSPORT_PARAMETER_ERROR."
void test_duplicate_parameter_is_rejected(void)
{
    static const uint8_t DUP[] = {
        0x0b, 0x01, 0x0a, // max_ack_delay = 10
        0x0b, 0x01, 0x14, // max_ack_delay again
    };
    static const uint8_t DUP_CID[] = {
        0x0f, 0x01, 0xaa, // initial_source_connection_id
        0x0f, 0x01, 0xbb, // and again
    };
    QuicTransportParams tp;
    TEST_ASSERT_FALSE(protocore_quic_tp_parse(DUP, sizeof(DUP), &tp));
    TEST_ASSERT_FALSE(protocore_quic_tp_parse(DUP_CID, sizeof(DUP_CID), &tp));
}

// A Length field that runs past the end of the string, and a value whose varint does not fill the
// declared length, are both malformed rather than partly accepted.
void test_malformed_encoding_is_rejected(void)
{
    QuicTransportParams tp;

    // Length says 4, only 2 octets follow.
    static const uint8_t OVERRUN[] = {0x04, 0x04, 0x00, 0x00};
    TEST_ASSERT_FALSE(protocore_quic_tp_parse(OVERRUN, sizeof(OVERRUN), &tp));

    // An ID with no Length after it.
    static const uint8_t NO_LEN[] = {0x04};
    TEST_ASSERT_FALSE(protocore_quic_tp_parse(NO_LEN, sizeof(NO_LEN), &tp));

    // A varint-valued parameter whose value varint consumes fewer octets than Length declares.
    static const uint8_t SHORT_VALUE[] = {0x04, 0x02, 0x01, 0x01};
    TEST_ASSERT_FALSE(protocore_quic_tp_parse(SHORT_VALUE, sizeof(SHORT_VALUE), &tp));

    // A varint-valued parameter with a zero-length value has no varint at all.
    static const uint8_t EMPTY_VALUE[] = {0x04, 0x00};
    TEST_ASSERT_FALSE(protocore_quic_tp_parse(EMPTY_VALUE, sizeof(EMPTY_VALUE), &tp));
}

// A destination too small for the whole parameter set writes nothing rather than a truncated one:
// half a transport-parameter string is a different set of limits.
void test_encode_refuses_a_short_buffer(void)
{
    QuicTransportParams tp;
    protocore_quic_tp_defaults(&tp);
    tp.has_original_dcid = PROTO_TRUE;
    tp.original_dcid_len = QUIC_MAX_CID_LEN;
    memset(tp.original_dcid, 0x22, QUIC_MAX_CID_LEN);

    uint8_t big[256];
    size_t n = protocore_quic_tp_encode(&tp, big, sizeof(big));
    TEST_ASSERT_TRUE(n > 0);

    uint8_t small[8];
    TEST_ASSERT_EQUAL_UINT(0u, protocore_quic_tp_encode(&tp, small, sizeof(small)));

    // One octet short of what it needs is still a refusal; exactly enough is not.
    uint8_t exact[256];
    TEST_ASSERT_EQUAL_UINT(0u, protocore_quic_tp_encode(&tp, exact, n - 1));
    TEST_ASSERT_EQUAL_UINT(n, protocore_quic_tp_encode(&tp, exact, n));
}
