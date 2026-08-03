// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the SNMP ASN.1 BER codec. Encodings are checked against
// independent known-answer vectors (the standard BER byte sequences), then
// round-tripped through the decoder.

#include "services/net/snmp/snmp_ber.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// ==================== INTEGER known-answer vectors ====================

static void check_int(long v, const uint8_t *exp, size_t explen)
{
    uint8_t buf[16];
    BerEnc e;
    pc_ber_enc_init(&e, buf, sizeof(buf));
    pc_ber_put_integer(&e, v);
    TEST_ASSERT_TRUE(e.ok);
    TEST_ASSERT_EQUAL_UINT(explen, e.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp, buf, explen);
}

void test_integer_vectors()
{
    const uint8_t v0[] = {0x02, 0x01, 0x00};
    check_int(0, v0, sizeof(v0));
    const uint8_t v1[] = {0x02, 0x01, 0x01};
    check_int(1, v1, sizeof(v1));
    const uint8_t v127[] = {0x02, 0x01, 0x7F};
    check_int(127, v127, sizeof(v127));
    const uint8_t v128[] = {0x02, 0x02, 0x00, 0x80};
    check_int(128, v128, sizeof(v128));
    const uint8_t v256[] = {0x02, 0x02, 0x01, 0x00};
    check_int(256, v256, sizeof(v256));
    const uint8_t vm1[] = {0x02, 0x01, 0xFF};
    check_int(-1, vm1, sizeof(vm1));
    const uint8_t vm128[] = {0x02, 0x01, 0x80};
    check_int(-128, vm128, sizeof(vm128));
    // -256 needs a second octet: after the first shift val is -1 but the emitted byte (0x00) has a
    // clear sign bit, so a bare 0x00 would decode as +0 - the encoder must keep the 0xFF sign octet.
    const uint8_t vm256[] = {0x02, 0x02, 0xFF, 0x00};
    check_int(-256, vm256, sizeof(vm256));
}

void test_oid_vector()
{
    // 1.3.6.1 -> 06 03 2B 06 01
    uint32_t a[] = {1, 3, 6, 1};
    uint8_t buf[16];
    BerEnc e;
    pc_ber_enc_init(&e, buf, sizeof(buf));
    pc_ber_put_oid(&e, a, 4);
    const uint8_t exp[] = {0x06, 0x03, 0x2B, 0x06, 0x01};
    TEST_ASSERT_TRUE(e.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(exp), e.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp, buf, sizeof(exp));

    // sysName.0 = 1.3.6.1.2.1.1.5.0 -> 06 08 2B 06 01 02 01 01 05 00
    uint32_t b[] = {1, 3, 6, 1, 2, 1, 1, 5, 0};
    pc_ber_enc_init(&e, buf, sizeof(buf));
    pc_ber_put_oid(&e, b, 9);
    const uint8_t exp2[] = {0x06, 0x08, 0x2B, 0x06, 0x01, 0x02, 0x01, 0x01, 0x05, 0x00};
    TEST_ASSERT_TRUE(e.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(exp2), e.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp2, buf, sizeof(exp2));
}

void test_octet_string_and_null()
{
    uint8_t buf[16];
    BerEnc e;
    pc_ber_enc_init(&e, buf, sizeof(buf));
    pc_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, (const uint8_t *)"public", 6);
    const uint8_t exp[] = {0x04, 0x06, 'p', 'u', 'b', 'l', 'i', 'c'};
    TEST_ASSERT_TRUE(e.ok);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp, buf, sizeof(exp));

    pc_ber_enc_init(&e, buf, sizeof(buf));
    pc_ber_put_null(&e);
    const uint8_t expn[] = {0x05, 0x00};
    TEST_ASSERT_EQUAL_UINT(2, e.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expn, buf, 2);
}

void test_counter32_keeps_unsigned()
{
    // 0x80000000 has the top bit set -> a leading 0x00 must be added.
    uint8_t buf[16];
    BerEnc e;
    pc_ber_enc_init(&e, buf, sizeof(buf));
    pc_ber_put_uint(&e, (uint8_t)SNMP_TAG_SNMP_COUNTER32, 0x80000000u);
    const uint8_t exp[] = {0x41, 0x05, 0x00, 0x80, 0x00, 0x00, 0x00};
    TEST_ASSERT_TRUE(e.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(exp), e.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp, buf, sizeof(exp));
}

// ==================== round-trip + SEQUENCE ====================

void test_sequence_roundtrip()
{
    uint8_t buf[64];
    BerEnc e;
    pc_ber_enc_init(&e, buf, sizeof(buf));
    size_t seq = pc_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    pc_ber_put_integer(&e, 1); // e.g. SNMP version (v2c=1)
    pc_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, (const uint8_t *)"public", 6);
    pc_ber_seq_end(&e, seq);
    TEST_ASSERT_TRUE(e.ok);

    // Decode: outer SEQUENCE, then INTEGER + OCTET STRING.
    BerDec d;
    pc_ber_dec_init(&d, buf, e.len);
    uint8_t tag;
    size_t len;
    TEST_ASSERT_TRUE(pc_ber_read_header(&d, &tag, &len));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_BER_SEQUENCE, tag);

    long ver = -99;
    TEST_ASSERT_TRUE(pc_ber_read_integer(&d, &ver));
    TEST_ASSERT_EQUAL_INT(1, ver);

    TEST_ASSERT_TRUE(pc_ber_read_header(&d, &tag, &len));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_BER_OCTET_STRING, tag);
    TEST_ASSERT_EQUAL_UINT(6, len);
    TEST_ASSERT_EQUAL_MEMORY("public", d.buf + d.pos, 6);
}

void test_oid_roundtrip()
{
    uint32_t in[] = {1, 3, 6, 1, 2, 1, 1, 5, 0};
    uint8_t buf[32];
    BerEnc e;
    pc_ber_enc_init(&e, buf, sizeof(buf));
    pc_ber_put_oid(&e, in, 9);
    TEST_ASSERT_TRUE(e.ok);

    BerDec d;
    pc_ber_dec_init(&d, buf, e.len);
    uint32_t out[SNMP_MAX_OID_LEN];
    size_t n = 0;
    TEST_ASSERT_TRUE(pc_ber_read_oid(&d, out, SNMP_MAX_OID_LEN, &n));
    TEST_ASSERT_EQUAL_UINT(9, n);
    for (size_t i = 0; i < 9; i++)
    {
        TEST_ASSERT_EQUAL_UINT32(in[i], out[i]);
    }
}

void test_large_arc_roundtrip()
{
    // An arc > 127 exercises multi-byte base-128 encoding (e.g. enterprise 8072).
    uint32_t in[] = {1, 3, 6, 1, 4, 1, 8072, 3, 2, 10};
    uint8_t buf[32];
    BerEnc e;
    pc_ber_enc_init(&e, buf, sizeof(buf));
    pc_ber_put_oid(&e, in, 10);
    TEST_ASSERT_TRUE(e.ok);

    BerDec d;
    pc_ber_dec_init(&d, buf, e.len);
    uint32_t out[SNMP_MAX_OID_LEN];
    size_t n = 0;
    TEST_ASSERT_TRUE(pc_ber_read_oid(&d, out, SNMP_MAX_OID_LEN, &n));
    TEST_ASSERT_EQUAL_UINT(10, n);
    TEST_ASSERT_EQUAL_UINT32(8072u, out[6]);
}

// X.690 8.19.4: the FIRST subidentifier (40*arc0 + arc1) is itself base-128 and can
// span multiple octets when arc1 is large. OID 2.100.3 -> first subid = 40*2+100 = 180
// (>= 128, two octets). The decoder must split it back to {2, 100, 3}, not misread it.
void test_oid_large_first_subidentifier_roundtrip()
{
    uint32_t in[] = {2, 100, 3};
    uint8_t buf[16];
    BerEnc e;
    pc_ber_enc_init(&e, buf, sizeof(buf));
    pc_ber_put_oid(&e, in, 3);
    TEST_ASSERT_TRUE(e.ok);

    BerDec d;
    pc_ber_dec_init(&d, buf, e.len);
    uint32_t out[SNMP_MAX_OID_LEN];
    size_t n = 0;
    TEST_ASSERT_TRUE(pc_ber_read_oid(&d, out, SNMP_MAX_OID_LEN, &n));
    TEST_ASSERT_EQUAL_UINT(3, n);
    TEST_ASSERT_EQUAL_UINT32(2u, out[0]);
    TEST_ASSERT_EQUAL_UINT32(100u, out[1]);
    TEST_ASSERT_EQUAL_UINT32(3u, out[2]);
}

// ==================== bounds / error handling ====================

void test_encoder_overflow_sets_not_ok()
{
    uint8_t buf[3];
    BerEnc e;
    pc_ber_enc_init(&e, buf, sizeof(buf));
    pc_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, (const uint8_t *)"too long", 8);
    TEST_ASSERT_FALSE(e.ok);
}

void test_decoder_truncated_length_fails()
{
    // Claims 10 bytes of content but only 2 are present.
    const uint8_t bad[] = {0x04, 0x0A, 0x01, 0x02};
    BerDec d;
    pc_ber_dec_init(&d, bad, sizeof(bad));
    uint8_t tag;
    size_t len;
    TEST_ASSERT_FALSE(pc_ber_read_header(&d, &tag, &len));
    TEST_ASSERT_FALSE(d.ok);
}

// Long-form length whose count byte (0x84 = "4 length octets follow") runs past
// the buffer: the count-byte bounds check must reject it, not over-read.
void test_decoder_longform_length_count_past_buffer_fails()
{
    const uint8_t bad[] = {0x04, 0x84, 0x00, 0x00}; // says 4 len octets, only 2 present
    BerDec d;
    pc_ber_dec_init(&d, bad, sizeof(bad));
    uint8_t tag;
    size_t len;
    TEST_ASSERT_FALSE(pc_ber_read_header(&d, &tag, &len));
    TEST_ASSERT_FALSE(d.ok);
}

// Long-form length with an over-wide count (> 4 octets) is rejected (a huge
// length can't be represented / is a malformed/attack input).
void test_decoder_longform_length_too_wide_fails()
{
    const uint8_t bad[] = {0x04, 0x85, 0x01, 0x00, 0x00, 0x00, 0x00}; // 5 length octets
    BerDec d;
    pc_ber_dec_init(&d, bad, sizeof(bad));
    uint8_t tag;
    size_t len;
    TEST_ASSERT_FALSE(pc_ber_read_header(&d, &tag, &len));
    TEST_ASSERT_FALSE(d.ok);
}

// Long-form length that parses but then claims more content than is present.
void test_decoder_longform_length_content_past_buffer_fails()
{
    // 0x82 0x01 0x00 = long form, length 256; only a few content bytes follow.
    const uint8_t bad[] = {0x04, 0x82, 0x01, 0x00, 0xAA, 0xBB};
    BerDec d;
    pc_ber_dec_init(&d, bad, sizeof(bad));
    uint8_t tag;
    size_t len;
    TEST_ASSERT_FALSE(pc_ber_read_header(&d, &tag, &len));
    TEST_ASSERT_FALSE(d.ok);
}

// A maximal 4-octet long-form length (0x84 FF FF FF FF = 0xFFFFFFFF) must be rejected, not
// admitted. On a 32-bit target `d->pos + 0xFFFFFFFF` wraps below `d->len` and would slip the
// bound; the wrap-safe `length_val > d->len - d->pos` check rejects it. (Host is 64-bit so it
// cannot wrap here - this guards the wrap-safe formulation against regression.)
void test_decoder_longform_length_max_uint32_fails()
{
    const uint8_t bad[] = {0x04, 0x84, 0xFF, 0xFF, 0xFF, 0xFF, 0xAA};
    BerDec d;
    pc_ber_dec_init(&d, bad, sizeof(bad));
    uint8_t tag;
    size_t len;
    TEST_ASSERT_FALSE(pc_ber_read_header(&d, &tag, &len));
    TEST_ASSERT_FALSE(d.ok);
}

// An indefinite-length encoding (0x80) is not valid in DER/this decoder.
void test_decoder_indefinite_length_fails()
{
    const uint8_t bad[] = {0x30, 0x80, 0x00, 0x00};
    BerDec d;
    pc_ber_dec_init(&d, bad, sizeof(bad));
    uint8_t tag;
    size_t len;
    TEST_ASSERT_FALSE(pc_ber_read_header(&d, &tag, &len));
    TEST_ASSERT_FALSE(d.ok);
}

// An INTEGER whose length exceeds the supported width (> 8 octets) is rejected.
void test_decoder_oversized_integer_fails()
{
    const uint8_t bad[] = {0x02, 0x09, 0, 0, 0, 0, 0, 0, 0, 0, 1}; // 9-octet INTEGER
    BerDec d;
    pc_ber_dec_init(&d, bad, sizeof(bad));
    long v;
    TEST_ASSERT_FALSE(pc_ber_read_integer(&d, &v));
    TEST_ASSERT_FALSE(d.ok);
}

void test_enc_len_long_form()
{
    // A value >= 128 octets forces the long-form definite length (0x81 <len>).
    uint8_t buf[300];
    BerEnc e;
    pc_ber_enc_init(&e, buf, sizeof(buf));
    uint8_t val[200];
    for (int i = 0; i < 200; i++)
    {
        val[i] = (uint8_t)i;
    }
    TEST_ASSERT_TRUE(pc_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, val, sizeof(val)));
    TEST_ASSERT_EQUAL_size_t(203, e.len); // tag(1) + 0x81 0xC8 (2) + 200
    TEST_ASSERT_EQUAL_HEX8(0x81, buf[1]); // long form, one length octet
    TEST_ASSERT_EQUAL_HEX8(0xC8, buf[2]); // 200
    BerDec d;
    pc_ber_dec_init(&d, buf, e.len);
    uint8_t tag;
    size_t len;
    TEST_ASSERT_TRUE(pc_ber_read_header(&d, &tag, &len));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_BER_OCTET_STRING, tag);
    TEST_ASSERT_EQUAL_size_t(200, len);
}

void test_put_oid_guards()
{
    uint8_t buf[256];
    BerEnc e;
    pc_ber_enc_init(&e, buf, sizeof(buf));
    uint32_t one[1] = {1};
    TEST_ASSERT_FALSE(pc_ber_put_oid(&e, one, 1)); // fewer than 2 arcs
    TEST_ASSERT_FALSE(e.ok);
    // More subidentifier octets than the internal scratch (SNMP_MAX_OID_LEN*5) holds.
    pc_ber_enc_init(&e, buf, sizeof(buf));
    uint32_t big[40];
    for (int i = 0; i < 40; i++)
    {
        big[i] = 0xFFFFFFFFu; // each encodes to five base-128 octets
    }
    TEST_ASSERT_FALSE(pc_ber_put_oid(&e, big, 40));
    TEST_ASSERT_FALSE(e.ok);
}

void test_seq_end_overflow()
{
    // A content region larger than the 16-bit back-patched length field fails closed.
    uint8_t buf[16];
    BerEnc e;
    pc_ber_enc_init(&e, buf, sizeof(buf));
    size_t tok = pc_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    e.len = tok + 3 + 0x10000; // pretend > 64 KiB of content was written
    pc_ber_seq_end(&e, tok);
    TEST_ASSERT_FALSE(e.ok);
}

void test_read_oid_rejects()
{
    // pc_ber_read_oid on a non-OID TLV.
    const uint8_t intv[] = {(uint8_t)SNMP_TAG_BER_INTEGER, 0x01, 0x05};
    BerDec d;
    pc_ber_dec_init(&d, intv, sizeof(intv));
    uint32_t arcs[8];
    size_t n = 0;
    TEST_ASSERT_FALSE(pc_ber_read_oid(&d, arcs, 8, &n));
    TEST_ASSERT_FALSE(d.ok);
    // An OID with more subidentifiers than the caller's array holds.
    uint8_t buf[64];
    BerEnc e;
    pc_ber_enc_init(&e, buf, sizeof(buf));
    uint32_t oid[4] = {1, 3, 6, 1};
    TEST_ASSERT_TRUE(pc_ber_put_oid(&e, oid, 4));
    BerDec d2;
    pc_ber_dec_init(&d2, buf, e.len);
    uint32_t out2[2];
    size_t n2 = 0;
    TEST_ASSERT_FALSE(pc_ber_read_oid(&d2, out2, 2, &n2)); // 4 arcs into max 2
    TEST_ASSERT_FALSE(d2.ok);
}

void test_ber_skip()
{
    const uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    BerDec d;
    pc_ber_dec_init(&d, data, sizeof(data));
    TEST_ASSERT_TRUE(pc_ber_skip(&d, 3));
    TEST_ASSERT_EQUAL_size_t(3, d.pos);
    TEST_ASSERT_FALSE(pc_ber_skip(&d, 100)); // beyond the remaining buffer
    TEST_ASSERT_FALSE(d.ok);
    BerDec d2;
    pc_ber_dec_init(&d2, NULL, 0); // ok == false
    TEST_ASSERT_FALSE(pc_ber_skip(&d2, 1));
}

// The encoder fails closed when handed no buffer at all, or a zero-capacity one, rather
// than reporting ok and writing through a null/zero-length destination.
void test_enc_init_rejects_unusable_buffer()
{
    uint8_t buf[8];
    BerEnc e;
    pc_ber_enc_init(&e, NULL, sizeof(buf)); // no buffer
    TEST_ASSERT_FALSE(e.ok);
    TEST_ASSERT_FALSE(pc_ber_put_null(&e)); // and stays closed for every write
    pc_ber_enc_init(&e, buf, 0);            // buffer, but no capacity
    TEST_ASSERT_FALSE(e.ok);
    TEST_ASSERT_FALSE(pc_ber_put_integer(&e, 1));
    TEST_ASSERT_EQUAL_size_t(0, e.len);
}

// A decoder that has already failed stays failed: read_header on a !ok decoder returns
// false without touching the (null) buffer.
void test_read_header_on_failed_decoder()
{
    BerDec d;
    pc_ber_dec_init(&d, NULL, 4); // ok == false
    TEST_ASSERT_FALSE(d.ok);
    uint8_t tag;
    size_t len;
    TEST_ASSERT_FALSE(pc_ber_read_header(&d, &tag, &len));
    TEST_ASSERT_EQUAL_size_t(0, d.pos); // cursor untouched
}

// A zero-length INTEGER is malformed (X.690 8.3.1 requires at least one content octet)
// and must be rejected rather than decoded as 0; so is a well-formed TLV of another type.
void test_read_integer_rejects_bad_tlv()
{
    const uint8_t zero_len[] = {0x02, 0x00};
    BerDec d;
    pc_ber_dec_init(&d, zero_len, sizeof(zero_len));
    long v = 12345;
    TEST_ASSERT_FALSE(pc_ber_read_integer(&d, &v));
    TEST_ASSERT_FALSE(d.ok);
    TEST_ASSERT_EQUAL_INT(12345, v); // output left alone

    const uint8_t octet_string[] = {0x04, 0x01, 0x05}; // parses as a TLV, but is not an INTEGER
    pc_ber_dec_init(&d, octet_string, sizeof(octet_string));
    TEST_ASSERT_FALSE(pc_ber_read_integer(&d, &v));
    TEST_ASSERT_FALSE(d.ok);
    TEST_ASSERT_EQUAL_INT(12345, v);
}

// A negative INTEGER sign-extends from the first content octet's high bit, so a
// one-octet 0xFF decodes as -1 (not 255) and a two-octet 0xFF 0x00 as -256.
void test_read_integer_sign_extends_negative()
{
    const uint8_t m1[] = {0x02, 0x01, 0xFF};
    BerDec d;
    pc_ber_dec_init(&d, m1, sizeof(m1));
    long v = 0;
    TEST_ASSERT_TRUE(pc_ber_read_integer(&d, &v));
    TEST_ASSERT_EQUAL_INT(-1, v);

    const uint8_t m256[] = {0x02, 0x02, 0xFF, 0x00};
    pc_ber_dec_init(&d, m256, sizeof(m256));
    TEST_ASSERT_TRUE(pc_ber_read_integer(&d, &v));
    TEST_ASSERT_EQUAL_INT(-256, v);
}

// pc_ber_read_oid rejects a truncated TLV header, and refuses a caller array too
// small to hold even the two arcs the first subidentifier always yields.
void test_read_oid_truncated_header_and_tiny_max()
{
    const uint8_t truncated[] = {0x06}; // OID tag with no length octet
    BerDec d;
    pc_ber_dec_init(&d, truncated, sizeof(truncated));
    uint32_t arcs[8];
    size_t n = 99;
    TEST_ASSERT_FALSE(pc_ber_read_oid(&d, arcs, 8, &n));
    TEST_ASSERT_FALSE(d.ok);

    const uint8_t oid_1_3_6_1[] = {0x06, 0x03, 0x2B, 0x06, 0x01};
    pc_ber_dec_init(&d, oid_1_3_6_1, sizeof(oid_1_3_6_1));
    TEST_ASSERT_FALSE(pc_ber_read_oid(&d, arcs, 1, &n)); // max < 2 cannot hold arc0 + arc1
    TEST_ASSERT_FALSE(d.ok);
}

// pc_ber_skip fails closed when the cursor is already past the end of the buffer,
// even for a zero-length skip - it never trusts pos <= len.
void test_ber_skip_cursor_past_end()
{
    const uint8_t data[4] = {1, 2, 3, 4};
    BerDec d;
    pc_ber_dec_init(&d, data, sizeof(data));
    d.pos = sizeof(data) + 1; // cursor beyond the buffer
    TEST_ASSERT_FALSE(pc_ber_skip(&d, 0));
    TEST_ASSERT_FALSE(d.ok);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_enc_init_rejects_unusable_buffer);
    RUN_TEST(test_read_header_on_failed_decoder);
    RUN_TEST(test_read_integer_rejects_bad_tlv);
    RUN_TEST(test_read_integer_sign_extends_negative);
    RUN_TEST(test_read_oid_truncated_header_and_tiny_max);
    RUN_TEST(test_ber_skip_cursor_past_end);
    RUN_TEST(test_integer_vectors);
    RUN_TEST(test_oid_vector);
    RUN_TEST(test_octet_string_and_null);
    RUN_TEST(test_counter32_keeps_unsigned);
    RUN_TEST(test_sequence_roundtrip);
    RUN_TEST(test_oid_roundtrip);
    RUN_TEST(test_large_arc_roundtrip);
    RUN_TEST(test_oid_large_first_subidentifier_roundtrip);
    RUN_TEST(test_encoder_overflow_sets_not_ok);
    RUN_TEST(test_decoder_truncated_length_fails);
    RUN_TEST(test_decoder_longform_length_count_past_buffer_fails);
    RUN_TEST(test_decoder_longform_length_too_wide_fails);
    RUN_TEST(test_decoder_longform_length_content_past_buffer_fails);
    RUN_TEST(test_decoder_longform_length_max_uint32_fails);
    RUN_TEST(test_decoder_indefinite_length_fails);
    RUN_TEST(test_decoder_oversized_integer_fails);
    RUN_TEST(test_enc_len_long_form);
    RUN_TEST(test_put_oid_guards);
    RUN_TEST(test_seq_end_overflow);
    RUN_TEST(test_read_oid_rejects);
    RUN_TEST(test_ber_skip);
    return UNITY_END();
}
