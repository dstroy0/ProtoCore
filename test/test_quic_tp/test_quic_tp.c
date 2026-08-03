// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the QUIC transport-parameters codec (network_drivers/presentation/http/http3/pc_quic_tp;
// RFC 9000 sec 18). Covers the spec defaults, an encode/parse round-trip (connection IDs + every
// varint parameter + the migration flag), parsing a hand-built byte string, skipping unknown/GREASE
// parameters, and rejection of malformed or out-of-range input.

#include "network_drivers/presentation/http/http3/quic_tp.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_defaults()
{
    QuicTransportParams tp;
    pc_quic_tp_defaults(&tp);
    TEST_ASSERT_EQUAL_UINT64(65527, tp.max_udp_payload_size);
    TEST_ASSERT_EQUAL_UINT64(3, tp.ack_delay_exponent);
    TEST_ASSERT_EQUAL_UINT64(25, tp.max_ack_delay);
    TEST_ASSERT_EQUAL_UINT64(2, tp.active_connection_id_limit);
    TEST_ASSERT_EQUAL_UINT64(0, tp.initial_max_data);
    TEST_ASSERT_FALSE(tp.has_original_dcid);
    TEST_ASSERT_FALSE(tp.disable_active_migration);
}

void test_roundtrip()
{
    QuicTransportParams tp;
    pc_quic_tp_defaults(&tp);
    tp.has_original_dcid = PROTO_TRUE;
    tp.original_dcid_len = 8;
    memcpy(tp.original_dcid, "\x00\x01\x02\x03\x04\x05\x06\x07", 8);
    tp.has_initial_scid = PROTO_TRUE;
    tp.initial_scid_len = 4;
    memcpy(tp.initial_scid, "\xaa\xbb\xcc\xdd", 4);
    tp.initial_max_data = 1048576;
    tp.initial_max_sd_bidi_local = 262144;
    tp.initial_max_sd_bidi_remote = 262144;
    tp.initial_max_sd_uni = 65536;
    tp.initial_max_streams_bidi = 100;
    tp.initial_max_streams_uni = 3;
    tp.max_idle_timeout = 30000;
    tp.disable_active_migration = PROTO_TRUE;

    uint8_t buf[256];
    size_t n = pc_quic_tp_encode(&tp, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);

    QuicTransportParams out;
    TEST_ASSERT_TRUE(pc_quic_tp_parse(buf, n, &out));
    TEST_ASSERT_TRUE(out.has_original_dcid);
    TEST_ASSERT_EQUAL_UINT8(8, out.original_dcid_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tp.original_dcid, out.original_dcid, 8);
    TEST_ASSERT_TRUE(out.has_initial_scid);
    TEST_ASSERT_EQUAL_UINT8(4, out.initial_scid_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tp.initial_scid, out.initial_scid, 4);
    TEST_ASSERT_EQUAL_UINT64(1048576, out.initial_max_data);
    TEST_ASSERT_EQUAL_UINT64(262144, out.initial_max_sd_bidi_local);
    TEST_ASSERT_EQUAL_UINT64(262144, out.initial_max_sd_bidi_remote);
    TEST_ASSERT_EQUAL_UINT64(65536, out.initial_max_sd_uni);
    TEST_ASSERT_EQUAL_UINT64(100, out.initial_max_streams_bidi);
    TEST_ASSERT_EQUAL_UINT64(3, out.initial_max_streams_uni);
    TEST_ASSERT_EQUAL_UINT64(30000, out.max_idle_timeout);
    TEST_ASSERT_TRUE(out.disable_active_migration);
}

// Parse a hand-built byte string independent of our own encoder:
//   0f 04 aabbccdd        initial_source_connection_id = aabbccdd
//   04 04 80100000        initial_max_data = 0x100000 (1048576)
//   01 02 6710            max_idle_timeout = 0x2710 (10000)
void test_parse_bytes()
{
    static const uint8_t enc[] = {0x0f, 0x04, 0xaa, 0xbb, 0xcc, 0xdd, 0x04, 0x04,
                                  0x80, 0x10, 0x00, 0x00, 0x01, 0x02, 0x67, 0x10};
    QuicTransportParams tp;
    TEST_ASSERT_TRUE(pc_quic_tp_parse(enc, sizeof(enc), &tp));
    TEST_ASSERT_TRUE(tp.has_initial_scid);
    TEST_ASSERT_EQUAL_UINT8(4, tp.initial_scid_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("\xaa\xbb\xcc\xdd", tp.initial_scid, 4);
    TEST_ASSERT_EQUAL_UINT64(1048576, tp.initial_max_data);
    TEST_ASSERT_EQUAL_UINT64(10000, tp.max_idle_timeout);
}

// An unknown (GREASE) parameter is skipped; a following known one still parses.
void test_skip_unknown()
{
    // id 0x1a (unknown), len 3, value 01 02 03; then 04 01 20 (initial_max_data = 0x20 = 32).
    static const uint8_t enc[] = {0x1a, 0x03, 0x01, 0x02, 0x03, 0x04, 0x01, 0x20};
    QuicTransportParams tp;
    TEST_ASSERT_TRUE(pc_quic_tp_parse(enc, sizeof(enc), &tp));
    TEST_ASSERT_EQUAL_UINT64(32, tp.initial_max_data);
}

void test_reject_duplicate()
{
    // initial_max_data twice.
    static const uint8_t enc[] = {0x04, 0x01, 0x20, 0x04, 0x01, 0x21};
    QuicTransportParams tp;
    TEST_ASSERT_FALSE(pc_quic_tp_parse(enc, sizeof(enc), &tp));
}

void test_reject_oversized_cid()
{
    // original_destination_connection_id with a 21-byte value (max is 20).
    uint8_t enc[2 + 21];
    enc[0] = QUIC_TP_ORIGINAL_DCID;
    enc[1] = 21;
    memset(enc + 2, 0xff, 21);
    QuicTransportParams tp;
    TEST_ASSERT_FALSE(pc_quic_tp_parse(enc, sizeof(enc), &tp));
}

void test_reject_bad_values()
{
    QuicTransportParams tp;
    // active_connection_id_limit = 1 (must be >= 2).
    static const uint8_t a[] = {0x0e, 0x01, 0x01};
    TEST_ASSERT_FALSE(pc_quic_tp_parse(a, sizeof(a), &tp));
    // max_udp_payload_size = 1000 (< 1200).
    static const uint8_t b[] = {0x03, 0x02, 0x43, 0xe8};
    TEST_ASSERT_FALSE(pc_quic_tp_parse(b, sizeof(b), &tp));
    // ack_delay_exponent = 21 (> 20).
    static const uint8_t c[] = {0x0a, 0x01, 0x15};
    TEST_ASSERT_FALSE(pc_quic_tp_parse(c, sizeof(c), &tp));
    // disable_active_migration with a non-zero length.
    static const uint8_t d[] = {0x0c, 0x01, 0x00};
    TEST_ASSERT_FALSE(pc_quic_tp_parse(d, sizeof(d), &tp));
    // truncated: id present, length says 4 but only 1 byte follows.
    static const uint8_t e[] = {0x04, 0x04, 0x00};
    TEST_ASSERT_FALSE(pc_quic_tp_parse(e, sizeof(e), &tp));
    // retry_source_connection_id (0x10) with a 21-byte value (max is 20).
    uint8_t f[2 + 21];
    f[0] = QUIC_TP_RETRY_SCID;
    f[1] = 21;
    memset(f + 2, 0x11, 21);
    TEST_ASSERT_FALSE(pc_quic_tp_parse(f, sizeof(f), &tp));
    // max_ack_delay = 16384 (>= 2^14); a 4-byte varint 80 00 40 00.
    static const uint8_t g[] = {0x0b, 0x04, 0x80, 0x00, 0x40, 0x00};
    TEST_ASSERT_FALSE(pc_quic_tp_parse(g, sizeof(g), &tp));
    // a numeric parameter (initial_max_sd_bidi_remote) with a zero-length value -> no varint to read.
    static const uint8_t h[] = {0x06, 0x00};
    TEST_ASSERT_FALSE(pc_quic_tp_parse(h, sizeof(h), &tp));
}

void test_quic_tp_more_paths()
{
    QuicTransportParams tp;
    QuicTransportParams out;
    uint8_t buf[256];

    // Encode overflow: a CID param's ID varint, length varint, and value each fail at a tight cap.
    pc_quic_tp_defaults(&tp);
    tp.has_original_dcid = PROTO_TRUE;
    tp.original_dcid_len = 8;
    memset(tp.original_dcid, 0xAB, 8);
    TEST_ASSERT_EQUAL_size_t(0, pc_quic_tp_encode(&tp, buf, 0)); // ID varint has no room
    TEST_ASSERT_EQUAL_size_t(0, pc_quic_tp_encode(&tp, buf, 1)); // length varint has no room
    TEST_ASSERT_EQUAL_size_t(0, pc_quic_tp_encode(&tp, buf, 6)); // value (8 octets) overruns

    // Encode: a varint value beyond the 62-bit range fails the inner varint encode.
    pc_quic_tp_defaults(&tp);
    tp.initial_max_data = 0xFFFFFFFFFFFFFFFFull;
    TEST_ASSERT_EQUAL_size_t(0, pc_quic_tp_encode(&tp, buf, sizeof(buf)));

    // Encode + parse a retry_source_connection_id round-trip (encode arm + valid-parse arm).
    pc_quic_tp_defaults(&tp);
    tp.has_retry_scid = PROTO_TRUE;
    tp.retry_scid_len = 4;
    memcpy(tp.retry_scid, "\xaa\xbb\xcc\xdd", 4);
    size_t n = pc_quic_tp_encode(&tp, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(pc_quic_tp_parse(buf, n, &out));
    TEST_ASSERT_TRUE(out.has_retry_scid);
    TEST_ASSERT_EQUAL_UINT8(4, out.retry_scid_len);

    // Parse: a truncated length varint (0xC0 announces an 8-octet varint with no octets after it).
    static const uint8_t badlen[2] = {0x04, 0xC0};
    TEST_ASSERT_FALSE(pc_quic_tp_parse(badlen, sizeof(badlen), &out));

    // Parse: an oversized initial_source_connection_id (0x0f).
    uint8_t bigscid[2 + 21];
    bigscid[0] = QUIC_TP_INITIAL_SCID;
    bigscid[1] = 21;
    memset(bigscid + 2, 0x22, 21);
    TEST_ASSERT_FALSE(pc_quic_tp_parse(bigscid, sizeof(bigscid), &out));

    // Parse: each varint-valued parameter whose value does not consume exactly its declared length.
    const uint8_t ids[] = {0x01, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
    for (unsigned i = 0; i < sizeof(ids); i++)
    {
        const uint8_t bad[4] = {ids[i], 0x02, 0x00, 0x00}; // 2-octet value, but the varint consumes 1
        TEST_ASSERT_FALSE(pc_quic_tp_parse(bad, sizeof(bad), &out));
    }

    // Parse: a valid ack_delay_exponent and max_ack_delay (the success arm of each range check).
    static const uint8_t ade[3] = {0x0a, 0x01, 0x03}; // ack_delay_exponent = 3
    TEST_ASSERT_TRUE(pc_quic_tp_parse(ade, sizeof(ade), &out));
    static const uint8_t mad[3] = {0x0b, 0x01, 0x19}; // max_ack_delay = 25
    TEST_ASSERT_TRUE(pc_quic_tp_parse(mad, sizeof(mad), &out));
}

// Encode: the initial_source_connection_id and retry_source_connection_id put_param() calls each have
// two arms exercised nowhere above: the call itself failing (cap overflow), and being skipped because
// "ok" is already false from an earlier failed param (the && short-circuit).
void test_encode_cid_ok_chain_gaps()
{
    QuicTransportParams tp;
    uint8_t buf[256];

    // All three connection-ID params present; cap = 0 fails original_dcid immediately, so both the
    // initial_scid and retry_scid puts are skipped by the already-false "ok &&" (not even attempted).
    pc_quic_tp_defaults(&tp);
    tp.has_original_dcid = PROTO_TRUE;
    tp.original_dcid_len = 8;
    memset(tp.original_dcid, 0xAB, 8);
    tp.has_initial_scid = PROTO_TRUE;
    tp.initial_scid_len = 4;
    memset(tp.initial_scid, 0xCD, 4);
    tp.has_retry_scid = PROTO_TRUE;
    tp.retry_scid_len = 4;
    memset(tp.retry_scid, 0xEF, 4);
    TEST_ASSERT_EQUAL_size_t(0, pc_quic_tp_encode(&tp, buf, 0));

    // initial_scid alone: cap enough for its ID + length varints but not its 4-octet value.
    pc_quic_tp_defaults(&tp);
    tp.has_initial_scid = PROTO_TRUE;
    tp.initial_scid_len = 4;
    memset(tp.initial_scid, 0xEE, 4);
    TEST_ASSERT_EQUAL_size_t(0, pc_quic_tp_encode(&tp, buf, 5));

    // retry_scid alone: same cap-starved shape.
    pc_quic_tp_defaults(&tp);
    tp.has_retry_scid = PROTO_TRUE;
    tp.retry_scid_len = 4;
    memset(tp.retry_scid, 0xFF, 4);
    TEST_ASSERT_EQUAL_size_t(0, pc_quic_tp_encode(&tp, buf, 5));
}

// Encode: the put_varint_param() failure arm (value beyond the 62-bit varint range) for every
// initial_max_* / stream-limit / timeout / payload-size field that test_quic_tp_more_paths only
// exercised for initial_max_data.
void test_encode_varint_param_overflow_gaps()
{
    QuicTransportParams tp;
    uint8_t buf[256];

    pc_quic_tp_defaults(&tp);
    tp.initial_max_sd_bidi_local = 0xFFFFFFFFFFFFFFFFull;
    TEST_ASSERT_EQUAL_size_t(0, pc_quic_tp_encode(&tp, buf, sizeof(buf)));

    pc_quic_tp_defaults(&tp);
    tp.initial_max_sd_bidi_remote = 0xFFFFFFFFFFFFFFFFull;
    TEST_ASSERT_EQUAL_size_t(0, pc_quic_tp_encode(&tp, buf, sizeof(buf)));

    pc_quic_tp_defaults(&tp);
    tp.initial_max_sd_uni = 0xFFFFFFFFFFFFFFFFull;
    TEST_ASSERT_EQUAL_size_t(0, pc_quic_tp_encode(&tp, buf, sizeof(buf)));

    pc_quic_tp_defaults(&tp);
    tp.initial_max_streams_bidi = 0xFFFFFFFFFFFFFFFFull;
    TEST_ASSERT_EQUAL_size_t(0, pc_quic_tp_encode(&tp, buf, sizeof(buf)));

    pc_quic_tp_defaults(&tp);
    tp.initial_max_streams_uni = 0xFFFFFFFFFFFFFFFFull;
    TEST_ASSERT_EQUAL_size_t(0, pc_quic_tp_encode(&tp, buf, sizeof(buf)));

    pc_quic_tp_defaults(&tp);
    tp.max_idle_timeout = 0xFFFFFFFFFFFFFFFFull;
    TEST_ASSERT_EQUAL_size_t(0, pc_quic_tp_encode(&tp, buf, sizeof(buf)));

    pc_quic_tp_defaults(&tp);
    tp.max_udp_payload_size = 0xFFFFFFFFFFFFFFFFull;
    TEST_ASSERT_EQUAL_size_t(0, pc_quic_tp_encode(&tp, buf, sizeof(buf)));
}

// Encode: the disable_active_migration put_param() call's two remaining arms - skipped because ok is
// already false (active_connection_id_limit failing first), and the call itself failing (cap runs out
// exactly at the flag, after the 9 always-emitted params fill it).
void test_encode_disable_migration_gaps()
{
    QuicTransportParams tp;
    uint8_t buf[64];

    pc_quic_tp_defaults(&tp);
    tp.active_connection_id_limit = 0xFFFFFFFFFFFFFFFFull;
    tp.disable_active_migration = PROTO_TRUE;
    TEST_ASSERT_EQUAL_size_t(0, pc_quic_tp_encode(&tp, buf, sizeof(buf)));

    // The 9 always-emitted params take exactly 30 bytes with an all-default tp (7 zero-valued 1-byte
    // varints at 3 bytes each, max_udp_payload_size's 4-byte varint at 6 bytes, active_connection_id_
    // limit's 1-byte varint at 3 bytes): 7*3 + 6 + 3 = 30. cap=30 leaves no room for the flag's ID byte.
    pc_quic_tp_defaults(&tp);
    tp.disable_active_migration = PROTO_TRUE;
    TEST_ASSERT_EQUAL_size_t(0, pc_quic_tp_encode(&tp, buf, 30));
}

// Parse: the ID-varint-decode failure itself (every other malformed-input test fails at a later step),
// and a transport parameter ID >= 32, which falls outside the dup-guard bitmask entirely.
void test_parse_id_decode_and_large_id()
{
    QuicTransportParams tp;

    // Announces an 8-octet varint (top 2 bits = 11) with zero bytes available.
    static const uint8_t bad_id[] = {0xC0};
    TEST_ASSERT_FALSE(pc_quic_tp_parse(bad_id, sizeof(bad_id), &tp));

    // id = 32 (>= 32, so the dup-guard bitmask is skipped), zero-length value; unknown -> skipped.
    static const uint8_t big_id[] = {0x20, 0x00};
    TEST_ASSERT_TRUE(pc_quic_tp_parse(big_id, sizeof(big_id), &tp));
}

// Parse: the "value_varint() itself fails" short-circuit arm of each remaining range-checked
// parameter's "value_varint(...) && <range check>" (test_quic_tp_more_paths only covered this arm for
// the range-check-free varint fields).
void test_parse_range_check_value_decode_gaps()
{
    QuicTransportParams tp;
    const uint8_t ids[] = {0x03, 0x0a, 0x0b, 0x0e};
    for (unsigned i = 0; i < sizeof(ids); i++)
    {
        const uint8_t bad[4] = {ids[i], 0x02, 0x00, 0x00}; // 2-octet value, but the varint consumes 1
        TEST_ASSERT_FALSE(pc_quic_tp_parse(bad, sizeof(bad), &tp));
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults);
    RUN_TEST(test_roundtrip);
    RUN_TEST(test_parse_bytes);
    RUN_TEST(test_skip_unknown);
    RUN_TEST(test_reject_duplicate);
    RUN_TEST(test_reject_oversized_cid);
    RUN_TEST(test_reject_bad_values);
    RUN_TEST(test_quic_tp_more_paths);
    RUN_TEST(test_encode_cid_ok_chain_gaps);
    RUN_TEST(test_encode_varint_param_overflow_gaps);
    RUN_TEST(test_encode_disable_migration_gaps);
    RUN_TEST(test_parse_id_decode_and_large_id);
    RUN_TEST(test_parse_range_check_value_decode_gaps);
    return UNITY_END();
}
