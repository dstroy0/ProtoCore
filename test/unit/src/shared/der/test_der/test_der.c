// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for src/shared/der/der.h - the ASN.1 DER reader X.509 is parsed with.
//
// X.690 sec 10 makes DER a canonical subset of BER: one encoding per value. That is what most of
// these cases are about. A reader that also accepts the BER spellings accepts two encodings of the
// same certificate, and a signature covers only one of them - so an attacker who can get the second
// one accepted has a certificate that verifies as something it is not. Each refusal below names the
// clause it comes from.
//
// The accepting cases are read off the encodings the RFCs print, not composed here: RFC 5280
// sec 4.1's Certificate SEQUENCE, and the times sec 4.1.2.5 spells out.

#include "shared/der/der.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// One read at @p pos over @p buf, so a case reads as one call.
static proto_bool read_at(const uint8_t *buf, size_t len, size_t pos)
{
    Der.read_args.buf = buf;
    Der.read_args.len = len;
    Der.read_args.pos = pos;
    Der.read(NULL);
    return Der.ok;
}

// ---------------------------------------------------------------------------
// X.690 sec 8.1: identifier and length octets
// ---------------------------------------------------------------------------

// The short form: one length octet under 128.
void test_a_short_form_value_reports_its_content_and_successor(void)
{
    const uint8_t v[] = {0x02, 0x01, 0x2A, 0xFF}; // INTEGER 42, then a byte that is not part of it
    TEST_ASSERT_TRUE(read_at(v, sizeof(v), 0));
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_DER_INTEGER, Der.tlv.tag);
    TEST_ASSERT_EQUAL_UINT(1, Der.tlv.len);
    TEST_ASSERT_EQUAL_HEX8(0x2A, Der.tlv.content[0]);
    TEST_ASSERT_EQUAL_UINT(3, Der.tlv.next); // where the 0xFF begins
}

// The long form: a count octet with the high bit set, then that many length octets.
void test_a_long_form_length_is_read(void)
{
    uint8_t v[4 + 200];
    memset(v, 0, sizeof(v));
    v[0] = PROTOCORE_DER_OCTET_STRING;
    v[1] = 0x81; // long form, one length octet
    v[2] = 200;
    TEST_ASSERT_TRUE(read_at(v, sizeof(v), 0));
    TEST_ASSERT_EQUAL_UINT(200, Der.tlv.len);
    TEST_ASSERT_EQUAL_UINT(203, Der.tlv.next);
}

// X.690 sec 10.1: the length is the shortest form that fits. 200 needs the long form, but a value
// under 128 does not, and spelling it long is a second encoding of the same value.
void test_a_long_form_that_fits_the_short_one_is_refused(void)
{
    const uint8_t v[] = {0x02, 0x81, 0x01, 0x2A};
    TEST_ASSERT_FALSE(read_at(v, sizeof(v), 0));
}

// The same clause: a long form padded with a leading zero octet.
void test_a_long_form_with_a_leading_zero_is_refused(void)
{
    const uint8_t v[] = {0x04, 0x82, 0x00, 0x80};
    TEST_ASSERT_FALSE(read_at(v, sizeof(v), 0));
}

// X.690 sec 8.1.3.6: the indefinite form is BER, and sec 10.1 forbids it in DER. Accepting it would
// mean scanning for an end-of-contents marker inside attacker-supplied bytes.
void test_an_indefinite_length_is_refused(void)
{
    const uint8_t v[] = {0x30, 0x80, 0x02, 0x01, 0x2A, 0x00, 0x00};
    TEST_ASSERT_FALSE(read_at(v, sizeof(v), 0));
}

// A length that runs past the buffer is refused rather than clamped: the content would otherwise be
// whatever follows the caller's bytes.
void test_a_length_past_the_buffer_is_refused(void)
{
    const uint8_t v[] = {0x04, 0x10, 0x01, 0x02};
    TEST_ASSERT_FALSE(read_at(v, sizeof(v), 0));
}

// The sum of position and length must not be allowed to wrap: on a 64-bit host a length octet run
// can spell a number that wraps the address space.
void test_a_length_that_would_wrap_is_refused(void)
{
    const uint8_t v[] = {0x04, 0x88, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_FALSE(read_at(v, sizeof(v), 0));
}

// X.690 sec 8.1.2.4: tag number 31 introduces the multi-octet form. No X.509 field this profile
// reads uses one, so it is refused rather than walked past.
void test_a_multi_octet_tag_is_refused(void)
{
    const uint8_t v[] = {0x1F, 0x81, 0x00, 0x01, 0x00};
    TEST_ASSERT_FALSE(read_at(v, sizeof(v), 0));
}

void test_an_empty_or_truncated_buffer_is_refused(void)
{
    const uint8_t v[] = {0x02};
    TEST_ASSERT_FALSE(read_at(v, 0, 0));
    TEST_ASSERT_FALSE(read_at(v, sizeof(v), 0)); // an identifier with no length octet
    TEST_ASSERT_FALSE(read_at(NULL, 4, 0));
}

// ---------------------------------------------------------------------------
// Stepping inside: RFC 5280 sec 4.1's Certificate SEQUENCE
// ---------------------------------------------------------------------------

// Certificate ::= SEQUENCE { tbsCertificate, signatureAlgorithm, signatureValue }. Entering it puts
// the reader on the first field, and each field's `next` walks to the one after it.
void test_entering_a_sequence_lands_on_its_first_field(void)
{
    const uint8_t cert[] = {
        0x30, 0x09,             // Certificate SEQUENCE, 9 octets
        0x30, 0x03, 0x02, 0x01, 0x02, // tbsCertificate SEQUENCE { INTEGER 2 }
        0x02, 0x01, 0x07,       // signatureAlgorithm, standing in as INTEGER 7
        0x03, 0x01, 0x00,       // signatureValue BIT STRING, empty
    };
    Der.read_args.buf = cert;
    Der.read_args.len = sizeof(cert);
    Der.read_args.pos = 0;
    Der.enter(NULL);
    TEST_ASSERT_TRUE(Der.ok);
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_DER_SEQUENCE, Der.tlv.tag); // tbsCertificate
    TEST_ASSERT_EQUAL_UINT(3, Der.tlv.len);

    // The field after it.
    TEST_ASSERT_TRUE(read_at(cert, sizeof(cert), Der.tlv.next));
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_DER_INTEGER, Der.tlv.tag);
    TEST_ASSERT_EQUAL_HEX8(0x07, Der.tlv.content[0]);
}

// X.690 sec 8.1.2.5: only a constructed value holds other values, so entering a primitive is a
// caller error rather than a read of whatever the content happens to spell.
void test_entering_a_primitive_is_refused(void)
{
    const uint8_t v[] = {0x02, 0x03, 0x30, 0x01, 0x00};
    Der.read_args.buf = v;
    Der.read_args.len = sizeof(v);
    Der.read_args.pos = 0;
    Der.enter(NULL);
    TEST_ASSERT_FALSE(Der.ok);
}

void test_entering_an_empty_sequence_is_refused(void)
{
    const uint8_t v[] = {0x30, 0x00};
    Der.read_args.buf = v;
    Der.read_args.len = sizeof(v);
    Der.read_args.pos = 0;
    Der.enter(NULL);
    TEST_ASSERT_FALSE(Der.ok);
}

// A context-specific constructed tag is what X.509 spells its optional fields with: version is
// [0] EXPLICIT, extensions are [3] EXPLICIT.
void test_a_context_tag_is_entered_like_any_constructed_value(void)
{
    const uint8_t v[] = {0xA0, 0x03, 0x02, 0x01, 0x02}; // [0] EXPLICIT { INTEGER 2 } - version v3
    TEST_ASSERT_TRUE(read_at(v, sizeof(v), 0));
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_DER_CONTEXT_CONSTRUCTED(0), Der.tlv.tag);

    Der.read_args.pos = 0;
    Der.enter(NULL);
    TEST_ASSERT_TRUE(Der.ok);
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_DER_INTEGER, Der.tlv.tag);
    TEST_ASSERT_EQUAL_HEX8(0x02, Der.tlv.content[0]); // v3
}

// ---------------------------------------------------------------------------
// INTEGER (X.690 sec 8.3)
// ---------------------------------------------------------------------------

static proto_bool uint_at(const uint8_t *buf, size_t len)
{
    Der.read_args.buf = buf;
    Der.read_args.len = len;
    Der.read_args.pos = 0;
    Der.uint(NULL);
    return Der.ok;
}

void test_an_integer_reports_its_value(void)
{
    const uint8_t zero[] = {0x02, 0x01, 0x00};
    TEST_ASSERT_TRUE(uint_at(zero, sizeof(zero)));
    TEST_ASSERT_EQUAL_UINT64(0, Der.u64);

    const uint8_t v3[] = {0x02, 0x01, 0x02};
    TEST_ASSERT_TRUE(uint_at(v3, sizeof(v3)));
    TEST_ASSERT_EQUAL_UINT64(2, Der.u64);

    const uint8_t big[] = {0x02, 0x03, 0x01, 0x00, 0x01}; // 65537, the usual RSA exponent
    TEST_ASSERT_TRUE(uint_at(big, sizeof(big)));
    TEST_ASSERT_EQUAL_UINT64(65537, Der.u64);
}

// sec 8.3.2: a leading 0x00 is there only to keep a high-bit value positive.
void test_a_leading_zero_is_read_when_the_value_needs_it(void)
{
    const uint8_t v[] = {0x02, 0x02, 0x00, 0x80};
    TEST_ASSERT_TRUE(uint_at(v, sizeof(v)));
    TEST_ASSERT_EQUAL_UINT64(0x80, Der.u64);
}

// And refused when it does not: that is a second encoding of the same number.
void test_a_redundant_leading_zero_is_refused(void)
{
    const uint8_t v[] = {0x02, 0x02, 0x00, 0x7F};
    TEST_ASSERT_FALSE(uint_at(v, sizeof(v)));
}

// sec 8.3.3: the encoding is two's complement, so a set high bit is a negative number. No X.509
// field this profile reads is one, and reading it as unsigned would turn -1 into a huge positive.
void test_a_negative_integer_is_refused(void)
{
    const uint8_t v[] = {0x02, 0x01, 0xFF};
    TEST_ASSERT_FALSE(uint_at(v, sizeof(v)));
}

// A serial number is up to 20 octets (RFC 5280 sec 4.1.2.2), which does not fit the value this
// reports, so it is refused rather than truncated to its low 8 octets.
void test_an_integer_wider_than_the_value_is_refused(void)
{
    const uint8_t v[] = {0x02, 0x09, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    TEST_ASSERT_FALSE(uint_at(v, sizeof(v)));
}

void test_an_empty_integer_is_refused(void)
{
    const uint8_t v[] = {0x02, 0x00};
    TEST_ASSERT_FALSE(uint_at(v, sizeof(v)));
}

// ---------------------------------------------------------------------------
// BIT STRING (X.690 sec 8.6)
// ---------------------------------------------------------------------------

// sec 8.6.2.2: the first content octet counts the unused bits in the last one. A key and a
// signature are whole octets, so the reader hands back the content past that octet.
void test_a_bit_string_yields_its_octets_past_the_unused_count(void)
{
    const uint8_t v[] = {0x03, 0x04, 0x00, 0xDE, 0xAD, 0xBE};
    Der.read_args.buf = v;
    Der.read_args.len = sizeof(v);
    Der.read_args.pos = 0;
    Der.bitstring(NULL);
    TEST_ASSERT_TRUE(Der.ok);
    TEST_ASSERT_EQUAL_UINT(3, Der.tlv.len);
    TEST_ASSERT_EQUAL_HEX8(0xDE, Der.tlv.content[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBE, Der.tlv.content[2]);
}

// A non-zero count would mean the last octet is partial. A key is not, so rather than shift the
// bits out and hand back something that is nearly the key, it is refused.
void test_a_bit_string_with_unused_bits_is_refused(void)
{
    const uint8_t v[] = {0x03, 0x03, 0x04, 0xDE, 0xA0};
    Der.read_args.buf = v;
    Der.read_args.len = sizeof(v);
    Der.read_args.pos = 0;
    Der.bitstring(NULL);
    TEST_ASSERT_FALSE(Der.ok);
}

void test_an_empty_bit_string_is_refused(void)
{
    const uint8_t v[] = {0x03, 0x00};
    Der.read_args.buf = v;
    Der.read_args.len = sizeof(v);
    Der.read_args.pos = 0;
    Der.bitstring(NULL);
    TEST_ASSERT_FALSE(Der.ok);
}

// ---------------------------------------------------------------------------
// OBJECT IDENTIFIER (X.690 sec 8.19)
// ---------------------------------------------------------------------------

// id-ecPublicKey, 1.2.840.10045.2.1 (RFC 5480 sec 2.1.1).
static const uint8_t OID_EC_PUBKEY[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01};
// id-Ed25519, 1.3.101.112 (RFC 8410 sec 3).
static const uint8_t OID_ED25519[] = {0x2B, 0x65, 0x70};

void test_an_oid_matches_only_itself(void)
{
    const uint8_t v[] = {0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01};
    Der.read_args.buf = v;
    Der.read_args.len = sizeof(v);
    Der.read_args.pos = 0;
    Der.oid_args.oid = OID_EC_PUBKEY;
    Der.oid_args.oid_len = sizeof(OID_EC_PUBKEY);
    Der.oid_eq(NULL);
    TEST_ASSERT_TRUE(Der.ok);

    Der.read_args.pos = 0;
    Der.oid_args.oid = OID_ED25519;
    Der.oid_args.oid_len = sizeof(OID_ED25519);
    Der.oid_eq(NULL);
    TEST_ASSERT_FALSE(Der.ok);
}

// A prefix is not a match: an OID that begins with another names a different thing entirely.
void test_an_oid_prefix_is_not_a_match(void)
{
    const uint8_t v[] = {0x06, 0x03, 0x2A, 0x86, 0x48}; // the first three octets of id-ecPublicKey
    Der.read_args.buf = v;
    Der.read_args.len = sizeof(v);
    Der.read_args.pos = 0;
    Der.oid_args.oid = OID_EC_PUBKEY;
    Der.oid_args.oid_len = sizeof(OID_EC_PUBKEY);
    Der.oid_eq(NULL);
    TEST_ASSERT_FALSE(Der.ok);
}

void test_a_value_that_is_not_an_oid_does_not_match_one(void)
{
    const uint8_t v[] = {0x04, 0x03, 0x2B, 0x65, 0x70}; // the Ed25519 octets, tagged OCTET STRING
    Der.read_args.buf = v;
    Der.read_args.len = sizeof(v);
    Der.read_args.pos = 0;
    Der.oid_args.oid = OID_ED25519;
    Der.oid_args.oid_len = sizeof(OID_ED25519);
    Der.oid_eq(NULL);
    TEST_ASSERT_FALSE(Der.ok);
}

// ---------------------------------------------------------------------------
// Validity times (RFC 5280 sec 4.1.2.5)
// ---------------------------------------------------------------------------

static proto_bool time_at(const uint8_t *buf, size_t len)
{
    Der.read_args.buf = buf;
    Der.read_args.len = len;
    Der.read_args.pos = 0;
    Der.time(NULL);
    return Der.ok;
}

// sec 4.1.2.5.1: YYMMDDHHMMSSZ, and YY < 50 is 20YY.
void test_a_utc_time_below_the_pivot_is_this_century(void)
{
    const uint8_t v[] = {0x17, 0x0D, '2', '6', '0', '8', '1', '8', '0', '0', '0', '0', '0', '0', 'Z'};
    TEST_ASSERT_TRUE(time_at(v, sizeof(v)));
    TEST_ASSERT_EQUAL_UINT64(1787011200ULL, Der.u64); // 2026-08-18T00:00:00Z
}

// And YY >= 50 is 19YY. The pivot is the one thing a reader can get silently wrong: a certificate
// that expired in 1998 would otherwise read as valid until 2098.
void test_a_utc_time_at_or_above_the_pivot_is_last_century(void)
{
    const uint8_t v[] = {0x17, 0x0D, '9', '8', '0', '1', '0', '1', '0', '0', '0', '0', '0', '0', 'Z'};
    TEST_ASSERT_TRUE(time_at(v, sizeof(v)));
    TEST_ASSERT_EQUAL_UINT64(883612800ULL, Der.u64); // 1998-01-01T00:00:00Z

    // At the pivot itself: 50 is 1950, which is before the epoch this reports seconds from, so it
    // is refused rather than reported as 2050. That confusion is the whole reason the pivot exists,
    // and sec 4.1.2.5 requires GeneralizedTime for 2050 and later anyway - the next case is that
    // path. A real certificate's dates are well inside the representable range: 1998 above is one.
    const uint8_t pivot[] = {0x17, 0x0D, '5', '0', '0', '1', '0', '1', '0', '0', '0', '0', '0', '0', 'Z'};
    TEST_ASSERT_FALSE(time_at(pivot, sizeof(pivot)));
}

// sec 4.1.2.5.2: YYYYMMDDHHMMSSZ. RFC 5280 requires GeneralizedTime for 2050 and later.
void test_a_generalized_time_carries_its_whole_year(void)
{
    const uint8_t v[] = {0x18, 0x0F, '2', '0', '5', '0', '0', '1', '0', '1',
                         '0',  '0',  '0', '0', '0', '0', 'Z'};
    TEST_ASSERT_TRUE(time_at(v, sizeof(v)));
    TEST_ASSERT_EQUAL_UINT64(2524608000ULL, Der.u64); // 2050-01-01T00:00:00Z
}

// A leap day is a real date, and the year-2000 rule is the one most leap tests get wrong.
void test_a_leap_day_is_counted(void)
{
    const uint8_t v[] = {0x17, 0x0D, '0', '0', '0', '2', '2', '9', '0', '0', '0', '0', '0', '0', 'Z'};
    TEST_ASSERT_TRUE(time_at(v, sizeof(v)));
    TEST_ASSERT_EQUAL_UINT64(951782400ULL, Der.u64); // 2000-02-29T00:00:00Z
}

// sec 4.1.2.5.1 requires seconds and Zulu. A time without them, or with a differential, is a second
// spelling and is refused.
void test_a_time_missing_its_seconds_or_zone_is_refused(void)
{
    const uint8_t no_z[] = {0x17, 0x0D, '2', '6', '0', '8', '1', '8', '0', '0', '0', '0', '0', '0', '+'};
    TEST_ASSERT_FALSE(time_at(no_z, sizeof(no_z)));

    const uint8_t no_secs[] = {0x17, 0x0B, '2', '6', '0', '8', '1', '8', '0', '0', '0', '0', 'Z'};
    TEST_ASSERT_FALSE(time_at(no_secs, sizeof(no_secs)));
}

void test_a_time_with_a_non_digit_or_an_impossible_field_is_refused(void)
{
    const uint8_t letter[] = {0x17, 0x0D, '2', 'X', '0', '8', '1', '8', '0', '0', '0', '0', '0', '0', 'Z'};
    TEST_ASSERT_FALSE(time_at(letter, sizeof(letter)));

    const uint8_t month13[] = {0x17, 0x0D, '2', '6', '1', '3', '0', '1', '0', '0', '0', '0', '0', '0', 'Z'};
    TEST_ASSERT_FALSE(time_at(month13, sizeof(month13)));

    const uint8_t hour24[] = {0x17, 0x0D, '2', '6', '0', '8', '1', '8', '2', '4', '0', '0', '0', '0', 'Z'};
    TEST_ASSERT_FALSE(time_at(hour24, sizeof(hour24)));

    const uint8_t day0[] = {0x17, 0x0D, '2', '6', '0', '8', '0', '0', '0', '0', '0', '0', '0', '0', 'Z'};
    TEST_ASSERT_FALSE(time_at(day0, sizeof(day0)));
}

void test_a_value_that_is_not_a_time_is_refused(void)
{
    const uint8_t v[] = {0x04, 0x0D, '2', '6', '0', '8', '1', '8', '0', '0', '0', '0', '0', '0', 'Z'};
    TEST_ASSERT_FALSE(time_at(v, sizeof(v)));
}
