// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the SNMP ASN.1 BER codec (services/net/snmp/snmp_ber.h).
//
// test_x690_object_identifier_first_subidentifier is the load-bearing case. ITU-T X.690 clause 8.19
// folds the first two subidentifiers of an OBJECT IDENTIFIER into one value, 40*arc0 + arc1, and
// then encodes that value in base 128 like any other - so it can span several octets. An encoder
// that emits arc0 and arc1 separately, or that assumes the folded value fits one octet, produces
// OIDs that look right for 1.3.6.1.* and are wrong for everything else, and every managed object
// name this library sends is an OID.
//
// The remaining expected octets are derived here from the definitions X.690 and RFC 3417 state,
// with the derivation written beside each vector.

#include "services/net/snmp/snmp_ber.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static BerEnc g_enc;
static BerDec g_dec;

static void enc_open(uint8_t *buf, size_t cap)
{
    SnmpBer.enc = &g_enc;
    SnmpBer.buf.out = buf;
    SnmpBer.buf.cap = cap;
    SnmpBer.enc_init(SnmpBer.internal);
}

static void dec_open(const uint8_t *buf, size_t len)
{
    SnmpBer.dec = &g_dec;
    SnmpBer.buf.in = buf;
    SnmpBer.buf.cap = len;
    SnmpBer.dec_init(SnmpBer.internal);
}

static void put_integer(long v)
{
    SnmpBer.tlv.ival = v;
    SnmpBer.put_integer(SnmpBer.internal);
}

static void put_uint(uint8_t tag, uint32_t v)
{
    SnmpBer.tlv.tag = tag;
    SnmpBer.tlv.uval = v;
    SnmpBer.put_uint(SnmpBer.internal);
}

static void put_octet_string(uint8_t tag, const void *p, size_t n)
{
    SnmpBer.tlv.tag = tag;
    SnmpBer.tlv.bytes = (const uint8_t *)p;
    SnmpBer.tlv.len = n;
    SnmpBer.put_octet_string(SnmpBer.internal);
}

static void put_oid(const uint32_t *arcs, size_t n)
{
    SnmpBer.tlv.arcs = arcs;
    SnmpBer.tlv.arc_count = n;
    SnmpBer.put_oid(SnmpBer.internal);
}

static size_t seq_begin(uint8_t tag)
{
    SnmpBer.tlv.tag = tag;
    SnmpBer.seq_begin(SnmpBer.internal);
    return SnmpBer.tlv.token;
}

static void seq_end(size_t token)
{
    SnmpBer.tlv.token = token;
    SnmpBer.seq_end(SnmpBer.internal);
}

static proto_bool read_header(void)
{
    SnmpBer.read_header(SnmpBer.internal);
    return SnmpBer.ok;
}

static proto_bool read_integer(void)
{
    SnmpBer.read_integer(SnmpBer.internal);
    return SnmpBer.ok;
}

static proto_bool read_oid(uint32_t *out, size_t cap)
{
    SnmpBer.read_args.arc_out = out;
    SnmpBer.read_args.arc_cap = cap;
    SnmpBer.read_oid(SnmpBer.internal);
    return SnmpBer.ok;
}

static proto_bool skip(size_t n)
{
    SnmpBer.read_args.skip = n;
    SnmpBer.skip(SnmpBer.internal);
    return SnmpBer.ok;
}

// Encode one INTEGER on its own and compare the whole TLV.
static void check_integer(long v, const uint8_t *want, size_t want_len, const char *msg)
{
    uint8_t buf[16];
    enc_open(buf, sizeof(buf));
    put_integer(v);
    TEST_ASSERT_TRUE_MESSAGE(g_enc.ok, msg);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(want_len, g_enc.len, msg);
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want, buf, want_len, msg);
}

// X.690 8.3: an INTEGER is two's complement, big-endian, in the fewest octets such that the first
// nine bits are not all ones and not all zeros. Each vector below is that rule applied by hand.
void test_x690_integer_minimal_octets(void)
{
    // 0 needs one content octet: 8.3.1 forbids a zero-length value.
    static const uint8_t V0[] = {0x02, 0x01, 0x00};
    check_integer(0, V0, sizeof(V0), "0");
    // 127 = 0x7F, sign bit clear, one octet.
    static const uint8_t V127[] = {0x02, 0x01, 0x7F};
    check_integer(127, V127, sizeof(V127), "127");
    // 128 = 0x80: one octet would read as -128, so a 0x00 sign octet goes in front.
    static const uint8_t V128[] = {0x02, 0x02, 0x00, 0x80};
    check_integer(128, V128, sizeof(V128), "128");
    // 256 = 0x0100, two octets, first nine bits are not all zeros.
    static const uint8_t V256[] = {0x02, 0x02, 0x01, 0x00};
    check_integer(256, V256, sizeof(V256), "256");
    // -1 is all ones; one octet 0xFF, since 8.3.2 only forbids all-ones across the first nine bits.
    static const uint8_t VM1[] = {0x02, 0x01, 0xFF};
    check_integer(-1, VM1, sizeof(VM1), "-1");
    // -128 = 0x80 in two's complement, one octet.
    static const uint8_t VM128[] = {0x02, 0x01, 0x80};
    check_integer(-128, VM128, sizeof(VM128), "-128");
    // -256 = 0xFF00: dropping the 0xFF would leave 0x00, which reads as +0.
    static const uint8_t VM256[] = {0x02, 0x02, 0xFF, 0x00};
    check_integer(-256, VM256, sizeof(VM256), "-256");
}

// X.690 8.19.4: the first subidentifier is 40 * arc0 + arc1, and 8.19.2 encodes every subidentifier
// in base 128 with the continuation bit set on all but the last octet.
//
//   2.100.3 -> first subidentifier 40*2 + 100 = 180
//              180 = 1 * 128 + 52, two groups: 0x80|1 = 0x81, then 52 = 0x34
//              third subidentifier 3           = 0x03
//              value is 3 octets               -> 06 03 81 34 03
void test_x690_object_identifier_first_subidentifier(void)
{
    static const uint32_t ARCS[] = {2, 100, 3};
    static const uint8_t WANT[] = {0x06, 0x03, 0x81, 0x34, 0x03};
    uint8_t buf[16];
    enc_open(buf, sizeof(buf));
    put_oid(ARCS, 3);
    TEST_ASSERT_TRUE(g_enc.ok);
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), g_enc.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, sizeof(WANT));

    // The decoder must split the multi-octet first subidentifier back into arc0 and arc1.
    uint32_t arcs[SNMP_MAX_OID_LEN];
    dec_open(buf, g_enc.len);
    TEST_ASSERT_TRUE(read_oid(arcs, SNMP_MAX_OID_LEN));
    TEST_ASSERT_EQUAL_size_t(3, SnmpBer.n);
    TEST_ASSERT_EQUAL_UINT32(2u, arcs[0]);
    TEST_ASSERT_EQUAL_UINT32(100u, arcs[1]);
    TEST_ASSERT_EQUAL_UINT32(3u, arcs[2]);
}

// sysName is { system 5 } and system is { mib-2 1 } (RFC 3418 sec 2), so sysName.0 names
// 1.3.6.1.2.1.1.5.0.
//
//   first subidentifier 40*1 + 3 = 43 = 0x2B, then 6, 1, 2, 1, 1, 5, 0 one octet each
//   value is 8 octets -> 06 08 2B 06 01 02 01 01 05 00
void test_rfc3418_sysname_instance_oid(void)
{
    static const uint32_t ARCS[] = {1, 3, 6, 1, 2, 1, 1, 5, 0};
    static const uint8_t WANT[] = {0x06, 0x08, 0x2B, 0x06, 0x01, 0x02, 0x01, 0x01, 0x05, 0x00};
    uint8_t buf[32];
    enc_open(buf, sizeof(buf));
    put_oid(ARCS, sizeof(ARCS) / sizeof(ARCS[0]));
    TEST_ASSERT_TRUE(g_enc.ok);
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), g_enc.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, sizeof(WANT));
}

// An arc above 127 spans several base-128 groups. Enterprise 8072 (net-snmp's IANA assignment)
// under 1.3.6.1.4.1 exercises it:
//   8072 = 63 * 128 + 8 -> 0x80|63 = 0xBF, then 8 = 0x08
void test_x690_multi_octet_subidentifier(void)
{
    static const uint32_t ARCS[] = {1, 3, 6, 1, 4, 1, 8072, 3, 2, 10};
    static const uint8_t WANT[] = {0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0xBF, 0x08, 0x03, 0x02, 0x0A};
    uint8_t buf[32];
    enc_open(buf, sizeof(buf));
    put_oid(ARCS, sizeof(ARCS) / sizeof(ARCS[0]));
    TEST_ASSERT_TRUE(g_enc.ok);
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), g_enc.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, sizeof(WANT));

    uint32_t arcs[SNMP_MAX_OID_LEN];
    dec_open(buf, g_enc.len);
    TEST_ASSERT_TRUE(read_oid(arcs, SNMP_MAX_OID_LEN));
    TEST_ASSERT_EQUAL_size_t(sizeof(ARCS) / sizeof(ARCS[0]), SnmpBer.n);
    for (size_t i = 0; i < sizeof(ARCS) / sizeof(ARCS[0]); i++)
    {
        TEST_ASSERT_EQUAL_UINT32(ARCS[i], arcs[i]);
    }
}

// RFC 2578 sec 7.1.6 through 7.1.8: Counter32, Gauge32 and TimeTicks are [APPLICATION 1], [2] and
// [3] IMPLICIT INTEGER (0..4294967295), so the identifier octet is 0x40|n and the value is the
// X.690 8.3 INTEGER encoding of a non-negative number - a 0x00 octet in front when the top bit is
// set, or the value would read as negative.
void test_rfc2578_application_types_stay_non_negative(void)
{
    uint8_t buf[16];

    // Counter32 0x80000000: top bit set -> 41 05 00 80 00 00 00
    static const uint8_t C32[] = {0x41, 0x05, 0x00, 0x80, 0x00, 0x00, 0x00};
    enc_open(buf, sizeof(buf));
    put_uint((uint8_t)SNMP_TAG_SNMP_COUNTER32, 0x80000000u);
    TEST_ASSERT_TRUE(g_enc.ok);
    TEST_ASSERT_EQUAL_size_t(sizeof(C32), g_enc.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(C32, buf, sizeof(C32));

    // Gauge32 at its stated maximum 4294967295 = 0xFFFFFFFF -> 42 05 00 FF FF FF FF
    static const uint8_t G32[] = {0x42, 0x05, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
    enc_open(buf, sizeof(buf));
    put_uint((uint8_t)SNMP_TAG_SNMP_GAUGE32, 4294967295u);
    TEST_ASSERT_TRUE(g_enc.ok);
    TEST_ASSERT_EQUAL_size_t(sizeof(G32), g_enc.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(G32, buf, sizeof(G32));

    // TimeTicks 0: one content octet, no sign octet -> 43 01 00
    static const uint8_t T0[] = {0x43, 0x01, 0x00};
    enc_open(buf, sizeof(buf));
    put_uint((uint8_t)SNMP_TAG_SNMP_TIMETICKS, 0u);
    TEST_ASSERT_TRUE(g_enc.ok);
    TEST_ASSERT_EQUAL_size_t(sizeof(T0), g_enc.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(T0, buf, sizeof(T0));

    // TimeTicks 127: still one octet, sign bit clear -> 43 01 7F
    static const uint8_t T127[] = {0x43, 0x01, 0x7F};
    enc_open(buf, sizeof(buf));
    put_uint((uint8_t)SNMP_TAG_SNMP_TIMETICKS, 127u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(T127, buf, sizeof(T127));
}

// X.690 8.7 OCTET STRING and 8.8 NULL. The community "public" is 6 octets, so the short-form
// length is 0x06; NULL always has a zero-length value.
void test_x690_octet_string_and_null(void)
{
    uint8_t buf[16];
    static const uint8_t WANT[] = {0x04, 0x06, 'p', 'u', 'b', 'l', 'i', 'c'};
    enc_open(buf, sizeof(buf));
    put_octet_string((uint8_t)SNMP_TAG_BER_OCTET_STRING, "public", 6);
    TEST_ASSERT_TRUE(g_enc.ok);
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), g_enc.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, sizeof(WANT));

    static const uint8_t NUL[] = {0x05, 0x00};
    enc_open(buf, sizeof(buf));
    SnmpBer.put_null(SnmpBer.internal);
    TEST_ASSERT_EQUAL_size_t(sizeof(NUL), g_enc.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(NUL, buf, sizeof(NUL));
}

// X.690 8.1.3.5: a length of 128 or more takes the long form - an octet 0x80|k naming k length
// octets, then the length big-endian. 200 octets -> 0x81 0xC8.
void test_x690_long_form_length(void)
{
    uint8_t val[200];
    uint8_t buf[300];
    memset(val, 0xA5, sizeof(val));
    enc_open(buf, sizeof(buf));
    put_octet_string((uint8_t)SNMP_TAG_BER_OCTET_STRING, val, sizeof(val));
    TEST_ASSERT_TRUE(g_enc.ok);
    TEST_ASSERT_EQUAL_size_t(1 + 2 + 200, g_enc.len);
    TEST_ASSERT_EQUAL_HEX8(0x81, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0xC8, buf[2]);

    dec_open(buf, g_enc.len);
    TEST_ASSERT_TRUE(read_header());
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_BER_OCTET_STRING, SnmpBer.tag);
    TEST_ASSERT_EQUAL_size_t(200, SnmpBer.vlen);
}

// RFC 3417 sec 8 item 1 permits more length octets than the minimum, so a constructed type opens
// with a reserved two-octet definite-long length that the close back-patches. The whole message
// below is therefore fixed octet for octet:
//
//   INTEGER 1                        02 01 01                    3 octets
//   OCTET STRING "public"            04 06 70 75 62 6C 69 63     8 octets
//   content                                                     11 octets
//   SEQUENCE header                  30 82 00 0B                 4 octets
void test_rfc3417_definite_long_sequence(void)
{
    static const uint8_t WANT[] = {0x30, 0x82, 0x00, 0x0B, 0x02, 0x01, 0x01, 0x04, 0x06, 'p', 'u', 'b', 'l', 'i', 'c'};
    uint8_t buf[64];
    enc_open(buf, sizeof(buf));
    const size_t seq = seq_begin((uint8_t)SNMP_TAG_BER_SEQUENCE);
    put_integer(1);
    put_octet_string((uint8_t)SNMP_TAG_BER_OCTET_STRING, "public", 6);
    seq_end(seq);
    TEST_ASSERT_TRUE(g_enc.ok);
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), g_enc.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, sizeof(WANT));

    // And it reads back as one SEQUENCE holding those two values.
    dec_open(buf, g_enc.len);
    TEST_ASSERT_TRUE(read_header());
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_BER_SEQUENCE, SnmpBer.tag);
    TEST_ASSERT_EQUAL_size_t(11, SnmpBer.vlen);
    TEST_ASSERT_TRUE(read_integer());
    TEST_ASSERT_EQUAL_INT(1, SnmpBer.ival);
    TEST_ASSERT_TRUE(read_header());
    TEST_ASSERT_EQUAL_HEX8((uint8_t)SNMP_TAG_BER_OCTET_STRING, SnmpBer.tag);
    TEST_ASSERT_EQUAL_size_t(6, SnmpBer.vlen);
    TEST_ASSERT_EQUAL_MEMORY("public", g_dec.buf + g_dec.pos, 6);
}

// Octets already encoded elsewhere append with no header of their own, so a PDU built in one
// buffer can be framed by a message built in another.
void test_put_raw_appends_verbatim(void)
{
    static const uint8_t PRE[] = {0x02, 0x01, 0x07};
    uint8_t buf[16];
    enc_open(buf, sizeof(buf));
    SnmpBer.tlv.bytes = PRE;
    SnmpBer.tlv.len = sizeof(PRE);
    SnmpBer.put_raw(SnmpBer.internal);
    TEST_ASSERT_TRUE(g_enc.ok);
    TEST_ASSERT_EQUAL_size_t(sizeof(PRE), g_enc.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PRE, buf, sizeof(PRE));
}

// RFC 3417 sec 8 item 1: "use of the indefinite form encoding is prohibited". The indefinite form
// is the length octet 0x80, which is also the long form with a count of zero.
void test_rfc3417_indefinite_length_is_refused(void)
{
    static const uint8_t BAD[] = {0x30, 0x80, 0x02, 0x01, 0x01, 0x00, 0x00};
    dec_open(BAD, sizeof(BAD));
    TEST_ASSERT_FALSE(read_header());
    TEST_ASSERT_FALSE(g_dec.ok);
}

// A length that names more octets than the buffer holds is refused rather than read through, in
// every shape the long form can take it.
void test_decoder_refuses_a_length_past_the_buffer(void)
{
    // short form: claims 10 content octets, 2 present
    static const uint8_t SHORT[] = {0x04, 0x0A, 0x01, 0x02};
    dec_open(SHORT, sizeof(SHORT));
    TEST_ASSERT_FALSE(read_header());

    // long form: 0x84 says four length octets follow, only two are present
    static const uint8_t COUNT[] = {0x04, 0x84, 0x00, 0x00};
    dec_open(COUNT, sizeof(COUNT));
    TEST_ASSERT_FALSE(read_header());

    // long form wider than the four octets a size_t length is read into
    static const uint8_t WIDE[] = {0x04, 0x85, 0x01, 0x00, 0x00, 0x00, 0x00};
    dec_open(WIDE, sizeof(WIDE));
    TEST_ASSERT_FALSE(read_header());

    // long form that parses, then names 256 content octets with two present
    static const uint8_t CONTENT[] = {0x04, 0x82, 0x01, 0x00, 0xAA, 0xBB};
    dec_open(CONTENT, sizeof(CONTENT));
    TEST_ASSERT_FALSE(read_header());

    // the widest four-octet length: pos + len wraps on a 32-bit size_t, so the check is written
    // against the octets remaining instead
    static const uint8_t MAXU32[] = {0x04, 0x84, 0xFF, 0xFF, 0xFF, 0xFF, 0xAA};
    dec_open(MAXU32, sizeof(MAXU32));
    TEST_ASSERT_FALSE(read_header());
}

// X.690 8.3.1 gives an INTEGER at least one content octet, and this codec reads at most eight.
// A well-formed TLV of another type is not an INTEGER either.
void test_read_integer_refuses_malformed(void)
{
    static const uint8_t ZERO_LEN[] = {0x02, 0x00};
    dec_open(ZERO_LEN, sizeof(ZERO_LEN));
    SnmpBer.ival = 12345;
    TEST_ASSERT_FALSE(read_integer());
    TEST_ASSERT_EQUAL_INT(12345, SnmpBer.ival); // left alone

    static const uint8_t NINE[] = {0x02, 0x09, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    dec_open(NINE, sizeof(NINE));
    TEST_ASSERT_FALSE(read_integer());

    static const uint8_t NOT_INT[] = {0x04, 0x01, 0x05};
    dec_open(NOT_INT, sizeof(NOT_INT));
    TEST_ASSERT_FALSE(read_integer());
}

// X.690 8.3.3: the value is two's complement, so the first content octet's high bit is the sign
// and the rest of the word is filled from it.
void test_read_integer_sign_extends(void)
{
    static const uint8_t M1[] = {0x02, 0x01, 0xFF};
    dec_open(M1, sizeof(M1));
    TEST_ASSERT_TRUE(read_integer());
    TEST_ASSERT_EQUAL_INT(-1, SnmpBer.ival);

    static const uint8_t M256[] = {0x02, 0x02, 0xFF, 0x00};
    dec_open(M256, sizeof(M256));
    TEST_ASSERT_TRUE(read_integer());
    TEST_ASSERT_EQUAL_INT(-256, SnmpBer.ival);

    // 0x00 0x80 is +128, not -128: the sign octet is what separates them.
    static const uint8_t P128[] = {0x02, 0x02, 0x00, 0x80};
    dec_open(P128, sizeof(P128));
    TEST_ASSERT_TRUE(read_integer());
    TEST_ASSERT_EQUAL_INT(128, SnmpBer.ival);
}

// The encoder reports the buffer it was given is full rather than writing past it, and refuses a
// buffer it cannot use at all.
void test_encoder_fails_closed(void)
{
    uint8_t small[3];
    enc_open(small, sizeof(small));
    put_octet_string((uint8_t)SNMP_TAG_BER_OCTET_STRING, "too long", 8);
    TEST_ASSERT_FALSE(g_enc.ok);

    uint8_t buf[8];
    enc_open(NULL, sizeof(buf));
    TEST_ASSERT_FALSE(g_enc.ok);
    SnmpBer.put_null(SnmpBer.internal);
    TEST_ASSERT_FALSE(g_enc.ok);
    TEST_ASSERT_EQUAL_size_t(0, g_enc.len);

    enc_open(buf, 0);
    TEST_ASSERT_FALSE(g_enc.ok);
    put_integer(1);
    TEST_ASSERT_EQUAL_size_t(0, g_enc.len);
}

// X.690 8.19.4 needs both of the first two subidentifiers to form one octet group, so fewer than
// two arcs has no encoding. An OID longer than the module's own scratch is refused too.
void test_put_oid_bounds(void)
{
    uint8_t buf[256];
    static const uint32_t ONE[] = {1};
    enc_open(buf, sizeof(buf));
    put_oid(ONE, 1);
    TEST_ASSERT_FALSE(g_enc.ok);

    uint32_t big[SNMP_MAX_OID_LEN + 8];
    for (size_t i = 0; i < sizeof(big) / sizeof(big[0]); i++)
    {
        big[i] = 0xFFFFFFFFu; // five base-128 groups each, past SNMP_MAX_OID_LEN * 5
    }
    enc_open(buf, sizeof(buf));
    put_oid(big, sizeof(big) / sizeof(big[0]));
    TEST_ASSERT_FALSE(g_enc.ok);
}

// read_oid refuses a TLV that is not an OBJECT IDENTIFIER, an array too small for the two arcs the
// first subidentifier always yields, and one too small for the arcs that follow.
void test_read_oid_bounds(void)
{
    uint32_t arcs[8];
    static const uint8_t INT_TLV[] = {0x02, 0x01, 0x05};
    dec_open(INT_TLV, sizeof(INT_TLV));
    TEST_ASSERT_FALSE(read_oid(arcs, 8));

    static const uint8_t TRUNCATED[] = {0x06}; // tag with no length octet
    dec_open(TRUNCATED, sizeof(TRUNCATED));
    TEST_ASSERT_FALSE(read_oid(arcs, 8));

    static const uint8_t OID_1_3_6_1[] = {0x06, 0x03, 0x2B, 0x06, 0x01};
    dec_open(OID_1_3_6_1, sizeof(OID_1_3_6_1));
    TEST_ASSERT_FALSE(read_oid(arcs, 1)); // cannot hold arc0 and arc1
    dec_open(OID_1_3_6_1, sizeof(OID_1_3_6_1));
    TEST_ASSERT_FALSE(read_oid(arcs, 2)); // holds those two, not the two that follow
    dec_open(OID_1_3_6_1, sizeof(OID_1_3_6_1));
    TEST_ASSERT_TRUE(read_oid(arcs, 4));
    TEST_ASSERT_EQUAL_size_t(4, SnmpBer.n);
}

// A skip walks the cursor over value octets and refuses to step past the end.
void test_skip_bounds(void)
{
    static const uint8_t DATA[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    dec_open(DATA, sizeof(DATA));
    TEST_ASSERT_TRUE(skip(3));
    TEST_ASSERT_EQUAL_size_t(3, g_dec.pos);
    TEST_ASSERT_TRUE(skip(5));
    TEST_ASSERT_EQUAL_size_t(8, g_dec.pos);
    TEST_ASSERT_FALSE(skip(1)); // nothing left
    TEST_ASSERT_FALSE(g_dec.ok);

    dec_open(NULL, 0);
    TEST_ASSERT_FALSE(skip(0));
}

// A decoder that has already failed stays failed and leaves its cursor where it was.
void test_failed_decoder_stays_failed(void)
{
    dec_open(NULL, 4);
    TEST_ASSERT_FALSE(g_dec.ok);
    TEST_ASSERT_FALSE(read_header());
    TEST_ASSERT_EQUAL_size_t(0, g_dec.pos);
}

// The reserved length field is two octets, so a constructed type holding more than 65535 octets
// has no encoding here and the close reports it rather than back-patching a truncated length.
static uint8_t g_filler[0x10000];
static uint8_t g_wide[0x10010];
void test_seq_end_refuses_over_65535_octets(void)
{
    memset(g_filler, 0x5A, sizeof(g_filler));
    enc_open(g_wide, sizeof(g_wide));
    const size_t seq = seq_begin((uint8_t)SNMP_TAG_BER_SEQUENCE);
    SnmpBer.tlv.bytes = g_filler;
    SnmpBer.tlv.len = sizeof(g_filler);
    SnmpBer.put_raw(SnmpBer.internal);
    TEST_ASSERT_TRUE(g_enc.ok);
    seq_end(seq);
    TEST_ASSERT_FALSE(g_enc.ok);
    TEST_ASSERT_FALSE(SnmpBer.ok);
}
