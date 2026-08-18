// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Network Time Security wire codec (network_drivers/application/nts/nts.h).
//
// The load-bearing case is test_ke_request_is_the_three_records_the_rfc_requires: every one of the
// sixteen octets is fixed by RFC 8915 - the record layout of Figure 2 (sec 4), the Critical Bit
// rules of sec 4.1.1 / 4.1.2, Protocol ID 0 for NTPv4 from the sec 7.7 registry, and AEAD Numeric
// Identifier 15 named in sec 4.1.5 - and the derivation is written out beside the array. The
// critical-bit and record-type accessors used throughout are the two C expressions RFC 8915 sec 4
// prints verbatim: `b[0] >> 7` and `((b[0] & 0x7f) << 8) + b[1]`.
//
// test_ke_parse_refuses_a_record_after_end_of_message asserts what sec 4.1.1 requires - End of
// Message "MUST occur exactly once as the final record" - not what the parser happened to do.
//
// The extension-field lengths come from RFC 7822 sec 7.5 as updated (Length counts the whole field
// including padding, fields are zero-padded to a four-octet word, maximum field length 65532), and
// the field types from the RFC 8915 sec 7.5 IANA table.

#include "network_drivers/application/nts/nts.h"
#include <string.h>

#include <unity.h>

static uint8_t nts_work[16]; // the borrow an entry takes; Nts never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

// RFC 8915 sec 4: "the critical bit is 'b[0] >> 7' while the record type is '((b[0] & 0x7f) << 8) + b[1]'".
static uint16_t rec_type(const uint8_t *b)
{
    return (uint16_t)(((b[0] & 0x7f) << 8) + b[1]);
}
static int rec_critical(const uint8_t *b)
{
    return b[0] >> 7;
}

// RFC 8915 sec 4: an NTS-KE record's length field "gives just the length of the body", while an NTP
// extension field's length "gives the length of the entire extension field including the type and
// length subfields".
//
//   KE record, 2-octet body: header 4 + body 2 = 6 written, Body Length field = 2.
//   NTP EF,    2-octet value: 4 + 2 = 6, padded to the next word (RFC 7822 sec 7.5) = 8 written,
//                             Length field = 8, and the two pad octets are zero.
void test_ke_length_counts_body_only_ef_counts_whole_field(void)
{
    static const uint8_t VALUE[2] = {0xA5, 0x5A};
    uint8_t rec[16];
    uint8_t ef[16];

    Nts.ke_record_args.critical = PROTO_TRUE;
    Nts.ke_record_args.type = NTS_KE_NEXT_PROTOCOL;
    Nts.ke_record_args.body = VALUE;
    Nts.ke_record_args.body_len = 2;
    Nts.ke_record_args.out = rec;
    Nts.ke_record_args.cap = sizeof(rec);
    Nts.ke_record(nts_work);
    size_t rn = Nts.n;
    TEST_ASSERT_EQUAL_size_t(6, rn);
    TEST_ASSERT_EQUAL_UINT16(2, be16(rec + 2));

    Nts.ef_args.field_type = NTS_EF_COOKIE;
    Nts.ef_args.value = VALUE;
    Nts.ef_args.value_len = 2;
    Nts.ef_args.out = ef;
    Nts.ef_args.cap = sizeof(ef);
    Nts.ef(nts_work);
    size_t en = Nts.n;
    TEST_ASSERT_EQUAL_size_t(8, en);
    TEST_ASSERT_EQUAL_UINT16(8, be16(ef + 2));
    TEST_ASSERT_EQUAL_HEX8(0x00, ef[6]);
    TEST_ASSERT_EQUAL_HEX8(0x00, ef[7]);
}

// RFC 8915 Figure 2: [C | Record Type : 15 bits][Body Length : 16 bits][Body].
// NTS Next Protocol Negotiation is Record Type 1 (sec 7.6 Table 4) and its Critical Bit MUST be set
// (sec 4.1.2), so the first octet is 0x80 | 0x00 and the second is 0x01. Its body is a sequence of
// 16-bit Protocol IDs; one ID is two octets, so Body Length = 0x0002.
void test_ke_record_field_layout(void)
{
    static const uint8_t NTPV4[2] = {0x00, 0x00};
    uint8_t out[8];
    Nts.ke_record_args.critical = PROTO_TRUE;
    Nts.ke_record_args.type = NTS_KE_NEXT_PROTOCOL;
    Nts.ke_record_args.body = NTPV4;
    Nts.ke_record_args.body_len = 2;
    Nts.ke_record_args.out = out;
    Nts.ke_record_args.cap = sizeof(out);
    Nts.ke_record(nts_work);
    size_t n = Nts.n;
    static const uint8_t WANT[6] = {0x80, 0x01, 0x00, 0x02, 0x00, 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, n);
    TEST_ASSERT_EQUAL_INT(1, rec_critical(out));
    TEST_ASSERT_EQUAL_UINT16(NTS_KE_NEXT_PROTOCOL, rec_type(out));
}

// sec 4.1.5 lets the AEAD Algorithm Negotiation record carry a clear Critical Bit, so the two
// subfields must be independently recoverable by the RFC's own expressions. An empty body writes the
// 4-octet header alone with Body Length 0.
void test_ke_record_critical_bit_is_separable_from_the_type(void)
{
    uint8_t out[8];
    Nts.ke_record_args.critical = PROTO_FALSE;
    Nts.ke_record_args.type = NTS_KE_AEAD_ALGORITHM;
    Nts.ke_record_args.body = NULL;
    Nts.ke_record_args.body_len = 0;
    Nts.ke_record_args.out = out;
    Nts.ke_record_args.cap = sizeof(out);
    Nts.ke_record(nts_work);
    size_t n = Nts.n;
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_INT(0, rec_critical(out));
    TEST_ASSERT_EQUAL_UINT16(NTS_KE_AEAD_ALGORITHM, rec_type(out));
    TEST_ASSERT_EQUAL_UINT16(0, be16(out + 2));
}

// sec 4: "Record Type Number: A 15-bit integer in network byte order", and sec 7.6 bounds the
// registry at 0-32767 inclusive. 32767 = 0x7FFF is therefore the largest type, and it must survive
// both settings of the Critical Bit without either subfield bleeding into the other.
void test_ke_record_type_is_fifteen_bits(void)
{
    uint8_t out[8];
    Nts.ke_record_args.critical = PROTO_FALSE;
    Nts.ke_record_args.type = 0x7FFF;
    Nts.ke_record_args.body = NULL;
    Nts.ke_record_args.body_len = 0;
    Nts.ke_record_args.out = out;
    Nts.ke_record_args.cap = sizeof(out);
    Nts.ke_record(nts_work);
    TEST_ASSERT_EQUAL_size_t(4, Nts.n);
    TEST_ASSERT_EQUAL_INT(0, rec_critical(out));
    TEST_ASSERT_EQUAL_UINT16(0x7FFF, rec_type(out));

    Nts.ke_record_args.critical = PROTO_TRUE;
    Nts.ke_record_args.type = 0x7FFF;
    Nts.ke_record_args.body = NULL;
    Nts.ke_record_args.body_len = 0;
    Nts.ke_record_args.out = out;
    Nts.ke_record_args.cap = sizeof(out);
    Nts.ke_record(nts_work);
    TEST_ASSERT_EQUAL_size_t(4, Nts.n);
    TEST_ASSERT_EQUAL_INT(1, rec_critical(out));
    TEST_ASSERT_EQUAL_UINT16(0x7FFF, rec_type(out));
}

// The whole client request, octet by octet, from RFC 8915 alone:
//
//   sec 4.1.2  Next Protocol Negotiation is type 1, Critical Bit MUST be set   -> 0x80 0x01
//              body is 16-bit Protocol IDs; Protocol ID 0 = NTPv4 (sec 7.7)    -> 0x00 0x02 0x00 0x00
//   sec 4.1.5  AEAD Algorithm Negotiation is type 4 (sec 7.6)                  -> 0x80 0x04
//              body is 16-bit AEAD Numeric Identifiers; AEAD_AES_SIV_CMAC_256
//              is Numeric Identifier 15 = 0x000F                               -> 0x00 0x02 0x00 0x0F
//   sec 4.1.1  End of Message is type 0, zero-length body, Critical Bit MUST
//              be set                                                          -> 0x80 0x00 0x00 0x00
//
//   6 + 6 + 4 = 16 octets.
void test_ke_request_is_the_three_records_the_rfc_requires(void)
{
    uint8_t out[32];
    Nts.ke_request_args.out = out;
    Nts.ke_request_args.cap = sizeof(out);
    Nts.ke_request(nts_work);
    size_t n = Nts.n;
    static const uint8_t WANT[16] = {0x80, 0x01, 0x00, 0x02, 0x00, 0x00, 0x80, 0x04,
                                     0x00, 0x02, 0x00, 0x0F, 0x80, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, n);
}

typedef struct
{
    int count;
    uint16_t type[8];
    proto_bool critical[8];
    size_t body_len[8];
} Records;

static void collect(proto_bool critical, uint16_t type, const uint8_t *body, size_t body_len, void *arg)
{
    (void)body;
    Records *r = (Records *)arg;
    if (r->count < 8)
    {
        r->type[r->count] = type;
        r->critical[r->count] = critical;
        r->body_len[r->count] = body_len;
        r->count++;
    }
}

// Round-trip identity: the walk must surface the builder's own three records, in order, with the
// Critical Bit and body length each record was written with.
void test_ke_parse_recovers_the_request_it_was_built_from(void)
{
    uint8_t req[32];
    Nts.ke_request_args.out = req;
    Nts.ke_request_args.cap = sizeof(req);
    Nts.ke_request(nts_work);
    size_t n = Nts.n;
    Records r;
    memset(&r, 0, sizeof(r));
    Nts.ke_parse_args.buf = req;
    Nts.ke_parse_args.len = n;
    Nts.ke_parse_args.cb = collect;
    Nts.ke_parse_args.arg = &r;
    Nts.ke_parse(nts_work);
    TEST_ASSERT_TRUE(Nts.ok);
    TEST_ASSERT_EQUAL_INT(3, r.count);
    TEST_ASSERT_EQUAL_UINT16(NTS_KE_NEXT_PROTOCOL, r.type[0]);
    TEST_ASSERT_EQUAL_UINT16(NTS_KE_AEAD_ALGORITHM, r.type[1]);
    TEST_ASSERT_EQUAL_UINT16(NTS_KE_END_OF_MESSAGE, r.type[2]);
    TEST_ASSERT_TRUE(r.critical[0]);
    TEST_ASSERT_TRUE(r.critical[1]);
    TEST_ASSERT_TRUE(r.critical[2]);
    TEST_ASSERT_EQUAL_size_t(2, r.body_len[0]);
    TEST_ASSERT_EQUAL_size_t(2, r.body_len[1]);
    TEST_ASSERT_EQUAL_size_t(0, r.body_len[2]);
}

// sec 4: "The sequence SHALL be terminated by a 'End of Message' record." A Next Protocol record on
// its own is not terminated, so it is not a message. A record whose Body Length runs past the buffer
// is not a record at all. A lone End of Message record is the shortest legal message: 4 octets.
void test_ke_parse_requires_end_of_message(void)
{
    static const uint8_t NO_END[6] = {0x80, 0x01, 0x00, 0x02, 0x00, 0x00};
    Nts.ke_parse_args.buf = NO_END;
    Nts.ke_parse_args.len = sizeof(NO_END);
    Nts.ke_parse_args.cb = NULL;
    Nts.ke_parse_args.arg = NULL;
    Nts.ke_parse(nts_work);
    TEST_ASSERT_FALSE(Nts.ok);

    static const uint8_t TRUNCATED[5] = {0x80, 0x01, 0x00, 0x02, 0x00};
    Nts.ke_parse_args.buf = TRUNCATED;
    Nts.ke_parse_args.len = sizeof(TRUNCATED);
    Nts.ke_parse_args.cb = NULL;
    Nts.ke_parse_args.arg = NULL;
    Nts.ke_parse(nts_work);
    TEST_ASSERT_FALSE(Nts.ok);

    static const uint8_t END_ONLY[4] = {0x80, 0x00, 0x00, 0x00};
    Nts.ke_parse_args.buf = END_ONLY;
    Nts.ke_parse_args.len = sizeof(END_ONLY);
    Nts.ke_parse_args.cb = NULL;
    Nts.ke_parse_args.arg = NULL;
    Nts.ke_parse(nts_work);
    TEST_ASSERT_TRUE(Nts.ok);

    Nts.ke_parse_args.buf = END_ONLY;
    Nts.ke_parse_args.len = 0;
    Nts.ke_parse_args.cb = NULL;
    Nts.ke_parse_args.arg = NULL;
    Nts.ke_parse(nts_work);
    TEST_ASSERT_FALSE(Nts.ok);
}

// sec 4.1.1: End of Message "MUST occur exactly once as the final record of every NTS-KE request and
// response", which sec 4 restates as the property that makes NTS-KE messages self-delimiting. A
// stream carrying a New Cookie record (type 5) after End of Message therefore is not a well-formed
// message, and a second End of Message is the "exactly once" half of the same rule.
void test_ke_parse_refuses_a_record_after_end_of_message(void)
{
    static const uint8_t TRAILING[10] = {0x80, 0x00, 0x00, 0x00, 0x80, 0x05, 0x00, 0x02, 0xDE, 0xAD};
    Nts.ke_parse_args.buf = TRAILING;
    Nts.ke_parse_args.len = sizeof(TRAILING);
    Nts.ke_parse_args.cb = NULL;
    Nts.ke_parse_args.arg = NULL;
    Nts.ke_parse(nts_work);
    TEST_ASSERT_FALSE(Nts.ok);

    static const uint8_t TWO_ENDS[8] = {0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00};
    Nts.ke_parse_args.buf = TWO_ENDS;
    Nts.ke_parse_args.len = sizeof(TWO_ENDS);
    Nts.ke_parse_args.cb = NULL;
    Nts.ke_parse_args.arg = NULL;
    Nts.ke_parse(nts_work);
    TEST_ASSERT_FALSE(Nts.ok);

    static const uint8_t STUB[6] = {0x80, 0x00, 0x00, 0x00, 0x80, 0x05};
    Nts.ke_parse_args.buf = STUB;
    Nts.ke_parse_args.len = sizeof(STUB);
    Nts.ke_parse_args.cb = NULL;
    Nts.ke_parse_args.arg = NULL;
    Nts.ke_parse(nts_work);
    TEST_ASSERT_FALSE(Nts.ok);
}

// RFC 7822 sec 7.5 as updated: "The Length field is a 16-bit unsigned integer that indicates the
// length of the entire extension field in octets, including the Padding field", and "All extension
// fields are zero-padded to a word (four octets) boundary."
//
//   Unique Identifier, 32-octet body (RFC 8915 sec 5.3: "MUST be at least 32 octets long"):
//     4 + 32 = 36, already a multiple of 4 -> 36 written, Length = 36, no padding.
//   Cookie, 5-octet value: 4 + 5 = 9 -> next word boundary is 12, so three zero pad octets.
//   Cookie, empty value:   4 + 0 = 4, the bare header, Length = 4.
void test_rfc7822_length_includes_header_and_padding(void)
{
    uint8_t out[64];

    uint8_t uid[32];
    memset(uid, 0xAB, sizeof(uid));
    Nts.ef_unique_id_args.nonce = uid;
    Nts.ef_unique_id_args.nonce_len = sizeof(uid);
    Nts.ef_unique_id_args.out = out;
    Nts.ef_unique_id_args.cap = sizeof(out);
    Nts.ef_unique_id(nts_work);
    TEST_ASSERT_EQUAL_size_t(36, Nts.n);
    TEST_ASSERT_EQUAL_UINT16(36, be16(out + 2));

    static const uint8_t FIVE[5] = {1, 2, 3, 4, 5};
    Nts.ef_args.field_type = NTS_EF_COOKIE;
    Nts.ef_args.value = FIVE;
    Nts.ef_args.value_len = sizeof(FIVE);
    Nts.ef_args.out = out;
    Nts.ef_args.cap = sizeof(out);
    Nts.ef(nts_work);
    TEST_ASSERT_EQUAL_size_t(12, Nts.n);
    TEST_ASSERT_EQUAL_UINT16(12, be16(out + 2));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(FIVE, out + 4, sizeof(FIVE));
    TEST_ASSERT_EQUAL_HEX8(0x00, out[9]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[10]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[11]);

    Nts.ef_args.field_type = NTS_EF_COOKIE;
    Nts.ef_args.value = NULL;
    Nts.ef_args.value_len = 0;
    Nts.ef_args.out = out;
    Nts.ef_args.cap = sizeof(out);
    Nts.ef(nts_work);
    TEST_ASSERT_EQUAL_size_t(4, Nts.n);
    TEST_ASSERT_EQUAL_UINT16(4, be16(out + 2));
}

// "All extension fields are zero-padded to a word (four octets) boundary" (RFC 7822 sec 7.5) holds
// for every value length, and the emitted Length always equals the emitted octet count.
void test_every_extension_field_is_word_aligned(void)
{
    static const uint8_t V[64] = {0};
    uint8_t out[128];
    for (size_t len = 0; len <= 64; len++)
    {
        Nts.ef_args.field_type = NTS_EF_COOKIE;
        Nts.ef_args.value = len ? V : NULL;
        Nts.ef_args.value_len = len;
        Nts.ef_args.out = out;
        Nts.ef_args.cap = sizeof(out);
        Nts.ef(nts_work);
        size_t n = Nts.n;
        TEST_ASSERT_EQUAL_size_t(0, n & 3u);
        TEST_ASSERT_TRUE(n >= 4 + len);
        TEST_ASSERT_TRUE(n < 4 + len + 4);
        TEST_ASSERT_EQUAL_UINT16((uint16_t)n, be16(out + 2));
    }
}

// RFC 8915 sec 7.5, the "NTP Extension Field Types" registry entries this memo allocated:
//   0x0104 Unique Identifier, 0x0204 NTS Cookie, 0x0304 NTS Cookie Placeholder,
//   0x0404 NTS Authenticator and Encrypted Extension Fields.
// The Field Type is the first 16-bit field of the extension field (RFC 7822 Figure 14).
void test_extension_field_types_match_the_registry(void)
{
    uint8_t out[64];
    static const uint8_t V[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    TEST_ASSERT_EQUAL_UINT16(0x0104, NTS_EF_UNIQUE_IDENTIFIER);
    TEST_ASSERT_EQUAL_UINT16(0x0204, NTS_EF_COOKIE);
    TEST_ASSERT_EQUAL_UINT16(0x0304, NTS_EF_COOKIE_PLACEHOLDER);
    TEST_ASSERT_EQUAL_UINT16(0x0404, NTS_EF_AUTH_AND_ENCRYPTED);

    Nts.ef_unique_id_args.nonce = V;
    Nts.ef_unique_id_args.nonce_len = sizeof(V);
    Nts.ef_unique_id_args.out = out;
    Nts.ef_unique_id_args.cap = sizeof(out);
    Nts.ef_unique_id(nts_work);
    TEST_ASSERT_EQUAL_size_t(12, Nts.n);
    TEST_ASSERT_EQUAL_UINT16(NTS_EF_UNIQUE_IDENTIFIER, be16(out));

    Nts.ef_cookie_args.cookie = V;
    Nts.ef_cookie_args.cookie_len = sizeof(V);
    Nts.ef_cookie_args.out = out;
    Nts.ef_cookie_args.cap = sizeof(out);
    Nts.ef_cookie(nts_work);
    TEST_ASSERT_EQUAL_size_t(12, Nts.n);
    TEST_ASSERT_EQUAL_UINT16(NTS_EF_COOKIE, be16(out));
}

// RFC 8915 sec 7.3 Table 1, the TLS Exporter Labels registry entry allocated by this memo.
void test_exporter_label_is_the_registered_string(void)
{
    TEST_ASSERT_EQUAL_STRING("EXPORTER-network-time-security", NTS_EXPORTER_LABEL);
}

// RFC 8915 sec 7.6 Table 4 (Record Types 0-7), sec 7.7 Table 5 (Protocol ID 0 = NTPv4), sec 4.1.5
// (AEAD_AES_SIV_CMAC_256 is Numeric Identifier 15), and sec 4 ("the Critical Bit is the most
// significant bit of the first octet", i.e. bit 15 of the first 16-bit field = 0x8000).
void test_record_type_numbers_match_the_registry(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, NTS_KE_END_OF_MESSAGE);
    TEST_ASSERT_EQUAL_UINT16(1, NTS_KE_NEXT_PROTOCOL);
    TEST_ASSERT_EQUAL_UINT16(2, NTS_KE_ERROR);
    TEST_ASSERT_EQUAL_UINT16(3, NTS_KE_WARNING);
    TEST_ASSERT_EQUAL_UINT16(4, NTS_KE_AEAD_ALGORITHM);
    TEST_ASSERT_EQUAL_UINT16(5, NTS_KE_COOKIE);
    TEST_ASSERT_EQUAL_UINT16(6, NTS_KE_NTPV4_SERVER);
    TEST_ASSERT_EQUAL_UINT16(7, NTS_KE_NTPV4_PORT);
    TEST_ASSERT_EQUAL_UINT16(0x8000, NTS_KE_CRITICAL);
    TEST_ASSERT_EQUAL_UINT16(0, NTS_NEXT_PROTO_NTPV4);
    TEST_ASSERT_EQUAL_UINT16(15, NTS_AEAD_AES_SIV_CMAC_256);
}

// A record is 4 header octets plus its body, so 6 octets is the smallest buffer that holds a 2-octet
// body and 5 is one short. Body Length is a 16-bit field (sec 4 Figure 2), so a body of 0x10000
// octets has no representable length and cannot be framed.
void test_ke_record_fails_closed(void)
{
    static const uint8_t BODY[2] = {0, 0};
    uint8_t out[8];
    Nts.ke_record_args.critical = PROTO_TRUE;
    Nts.ke_record_args.type = NTS_KE_NEXT_PROTOCOL;
    Nts.ke_record_args.body = BODY;
    Nts.ke_record_args.body_len = 2;
    Nts.ke_record_args.out = NULL;
    Nts.ke_record_args.cap = sizeof(out);
    Nts.ke_record(nts_work);
    TEST_ASSERT_EQUAL_size_t(0, Nts.n);
    Nts.ke_record_args.critical = PROTO_TRUE;
    Nts.ke_record_args.type = NTS_KE_NEXT_PROTOCOL;
    Nts.ke_record_args.body = NULL;
    Nts.ke_record_args.body_len = 2;
    Nts.ke_record_args.out = out;
    Nts.ke_record_args.cap = sizeof(out);
    Nts.ke_record(nts_work);
    TEST_ASSERT_EQUAL_size_t(0, Nts.n);
    Nts.ke_record_args.critical = PROTO_TRUE;
    Nts.ke_record_args.type = NTS_KE_NEXT_PROTOCOL;
    Nts.ke_record_args.body = BODY;
    Nts.ke_record_args.body_len = 2;
    Nts.ke_record_args.out = out;
    Nts.ke_record_args.cap = 5;
    Nts.ke_record(nts_work);
    TEST_ASSERT_EQUAL_size_t(0, Nts.n);
    Nts.ke_record_args.critical = PROTO_TRUE;
    Nts.ke_record_args.type = NTS_KE_NEXT_PROTOCOL;
    Nts.ke_record_args.body = BODY;
    Nts.ke_record_args.body_len = 2;
    Nts.ke_record_args.out = out;
    Nts.ke_record_args.cap = 6;
    Nts.ke_record(nts_work);
    TEST_ASSERT_EQUAL_size_t(6, Nts.n);

    Nts.ke_record_args.critical = PROTO_TRUE;
    Nts.ke_record_args.type = NTS_KE_NEXT_PROTOCOL;
    Nts.ke_record_args.body = BODY;
    Nts.ke_record_args.body_len = 0x10000;
    Nts.ke_record_args.out = out;
    Nts.ke_record_args.cap = sizeof(out);
    Nts.ke_record(nts_work);
    TEST_ASSERT_EQUAL_size_t(0, Nts.n);
}

// A partial request is not a request: the three records of sec 4 total 16 octets, so anything less
// writes nothing at all rather than a truncated message.
void test_ke_request_needs_all_sixteen_octets(void)
{
    uint8_t out[32];
    Nts.ke_request_args.out = out;
    Nts.ke_request_args.cap = 5;
    Nts.ke_request(nts_work);
    TEST_ASSERT_EQUAL_size_t(0, Nts.n);
    Nts.ke_request_args.out = out;
    Nts.ke_request_args.cap = 11;
    Nts.ke_request(nts_work);
    TEST_ASSERT_EQUAL_size_t(0, Nts.n);
    Nts.ke_request_args.out = out;
    Nts.ke_request_args.cap = 15;
    Nts.ke_request(nts_work);
    TEST_ASSERT_EQUAL_size_t(0, Nts.n);
    Nts.ke_request_args.out = out;
    Nts.ke_request_args.cap = 16;
    Nts.ke_request(nts_work);
    TEST_ASSERT_EQUAL_size_t(16, Nts.n);
}

// RFC 7822 sec 7.5 as updated: "the maximum field length cannot be longer than 65532 octets, due to
// the maximum size of the Length field."
//
//   value 65528 -> 4 + 65528 = 65532, already word-aligned, exactly the published maximum.
//   value 65529 -> 4 + 65529 = 65533 -> padded to 65536, past the maximum and past the 16-bit
//                  Length field, so no field can be framed.
void test_extension_field_length_bound(void)
{
    static uint8_t big[65536];
    static uint8_t out[65536];
    memset(big, 0x5A, sizeof(big));

    Nts.ef_args.field_type = NTS_EF_COOKIE;
    Nts.ef_args.value = big;
    Nts.ef_args.value_len = 65529;
    Nts.ef_args.out = out;
    Nts.ef_args.cap = sizeof(out);
    Nts.ef(nts_work);
    TEST_ASSERT_EQUAL_size_t(0, Nts.n);

    Nts.ef_args.field_type = NTS_EF_COOKIE;
    Nts.ef_args.value = big;
    Nts.ef_args.value_len = 65528;
    Nts.ef_args.out = out;
    Nts.ef_args.cap = sizeof(out);
    Nts.ef(nts_work);
    TEST_ASSERT_EQUAL_size_t(65532, Nts.n);
    TEST_ASSERT_EQUAL_UINT16(65532, be16(out + 2));
}

// An 8-octet value pads to 12 (4 + 8 is already word-aligned), so 11 octets of room is one short and
// 12 is exact. A null destination or a null value with a non-zero length writes nothing.
void test_extension_field_fails_closed(void)
{
    uint8_t out[64];
    static const uint8_t V[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    Nts.ef_args.field_type = NTS_EF_COOKIE;
    Nts.ef_args.value = V;
    Nts.ef_args.value_len = sizeof(V);
    Nts.ef_args.out = NULL;
    Nts.ef_args.cap = sizeof(out);
    Nts.ef(nts_work);
    TEST_ASSERT_EQUAL_size_t(0, Nts.n);
    Nts.ef_args.field_type = NTS_EF_COOKIE;
    Nts.ef_args.value = NULL;
    Nts.ef_args.value_len = 8;
    Nts.ef_args.out = out;
    Nts.ef_args.cap = sizeof(out);
    Nts.ef(nts_work);
    TEST_ASSERT_EQUAL_size_t(0, Nts.n);
    Nts.ef_args.field_type = NTS_EF_COOKIE;
    Nts.ef_args.value = V;
    Nts.ef_args.value_len = sizeof(V);
    Nts.ef_args.out = out;
    Nts.ef_args.cap = 11;
    Nts.ef(nts_work);
    TEST_ASSERT_EQUAL_size_t(0, Nts.n);
    Nts.ef_args.field_type = NTS_EF_COOKIE;
    Nts.ef_args.value = V;
    Nts.ef_args.value_len = sizeof(V);
    Nts.ef_args.out = out;
    Nts.ef_args.cap = 12;
    Nts.ef(nts_work);
    TEST_ASSERT_EQUAL_size_t(12, Nts.n);
}
