// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/application/nts/nts.h"
#include <string.h>

#include <unity.h>

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

static uint16_t rec_type(const uint8_t *b)
{
    return (uint16_t)(((b[0] & 0x7f) << 8) + b[1]);
}
static int rec_critical(const uint8_t *b)
{
    return b[0] >> 7;
}

void test_ke_length_counts_body_only_ef_counts_whole_field(void)
{
    static const uint8_t VALUE[2] = {0xA5, 0x5A};
    uint8_t rec[16];
    uint8_t ef[16];

    size_t rn = protocore_nts_ke_record(PROTO_TRUE, NTS_KE_NEXT_PROTOCOL, VALUE, 2, rec, sizeof(rec));
    TEST_ASSERT_EQUAL_size_t(6, rn);
    TEST_ASSERT_EQUAL_UINT16(2, be16(rec + 2));

    size_t en = protocore_nts_ef(NTS_EF_COOKIE, VALUE, 2, ef, sizeof(ef));
    TEST_ASSERT_EQUAL_size_t(8, en);
    TEST_ASSERT_EQUAL_UINT16(8, be16(ef + 2));
    TEST_ASSERT_EQUAL_HEX8(0x00, ef[6]);
    TEST_ASSERT_EQUAL_HEX8(0x00, ef[7]);
}

void test_ke_record_field_layout(void)
{
    static const uint8_t NTPV4[2] = {0x00, 0x00};
    uint8_t out[8];
    size_t n = protocore_nts_ke_record(PROTO_TRUE, NTS_KE_NEXT_PROTOCOL, NTPV4, 2, out, sizeof(out));
    static const uint8_t WANT[6] = {0x80, 0x01, 0x00, 0x02, 0x00, 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, n);
    TEST_ASSERT_EQUAL_INT(1, rec_critical(out));
    TEST_ASSERT_EQUAL_UINT16(NTS_KE_NEXT_PROTOCOL, rec_type(out));
}

void test_ke_record_critical_bit_is_separable_from_the_type(void)
{
    uint8_t out[8];
    size_t n = protocore_nts_ke_record(PROTO_FALSE, NTS_KE_AEAD_ALGORITHM, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_INT(0, rec_critical(out));
    TEST_ASSERT_EQUAL_UINT16(NTS_KE_AEAD_ALGORITHM, rec_type(out));
    TEST_ASSERT_EQUAL_UINT16(0, be16(out + 2));
}

void test_ke_record_type_is_fifteen_bits(void)
{
    uint8_t out[8];
    TEST_ASSERT_EQUAL_size_t(4, protocore_nts_ke_record(PROTO_FALSE, 0x7FFF, NULL, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(0, rec_critical(out));
    TEST_ASSERT_EQUAL_UINT16(0x7FFF, rec_type(out));

    TEST_ASSERT_EQUAL_size_t(4, protocore_nts_ke_record(PROTO_TRUE, 0x7FFF, NULL, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(1, rec_critical(out));
    TEST_ASSERT_EQUAL_UINT16(0x7FFF, rec_type(out));
}

void test_ke_request_is_the_three_records_the_rfc_requires(void)
{
    uint8_t out[32];
    size_t n = protocore_nts_ke_request(out, sizeof(out));
    static const uint8_t WANT[16] = {
        0x80, 0x01, 0x00, 0x02, 0x00, 0x00,
        0x80, 0x04, 0x00, 0x02, 0x00, 0x0F,
        0x80, 0x00, 0x00, 0x00
    };
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

void test_ke_parse_recovers_the_request_it_was_built_from(void)
{
    uint8_t req[32];
    size_t n = protocore_nts_ke_request(req, sizeof(req));
    Records r;
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_TRUE(protocore_nts_ke_parse(req, n, collect, &r));
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

void test_ke_parse_requires_end_of_message(void)
{
    static const uint8_t NO_END[6] = {0x80, 0x01, 0x00, 0x02, 0x00, 0x00};
    TEST_ASSERT_FALSE(protocore_nts_ke_parse(NO_END, sizeof(NO_END), NULL, NULL));

    static const uint8_t TRUNCATED[5] = {0x80, 0x01, 0x00, 0x02, 0x00};
    TEST_ASSERT_FALSE(protocore_nts_ke_parse(TRUNCATED, sizeof(TRUNCATED), NULL, NULL));

    static const uint8_t END_ONLY[4] = {0x80, 0x00, 0x00, 0x00};
    TEST_ASSERT_TRUE(protocore_nts_ke_parse(END_ONLY, sizeof(END_ONLY), NULL, NULL));

    TEST_ASSERT_FALSE(protocore_nts_ke_parse(END_ONLY, 0, NULL, NULL));
}

void test_ke_parse_stops_at_end_of_message(void)
{
    static const uint8_t STREAM[10] = {
        0x80, 0x00, 0x00, 0x00,
        0x80, 0x05, 0x00, 0x02, 0xDE, 0xAD
    };
    Records r;
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_TRUE(protocore_nts_ke_parse(STREAM, sizeof(STREAM), collect, &r));
    TEST_ASSERT_EQUAL_INT(1, r.count);
    TEST_ASSERT_EQUAL_UINT16(NTS_KE_END_OF_MESSAGE, r.type[0]);
}

void test_rfc7822_length_includes_header_and_padding(void)
{
    uint8_t out[64];

    uint8_t uid[32];
    memset(uid, 0xAB, sizeof(uid));
    TEST_ASSERT_EQUAL_size_t(36, protocore_nts_ef_unique_id(uid, sizeof(uid), out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT16(36, be16(out + 2));

    static const uint8_t FIVE[5] = {1, 2, 3, 4, 5};
    TEST_ASSERT_EQUAL_size_t(12, protocore_nts_ef(NTS_EF_COOKIE, FIVE, sizeof(FIVE), out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT16(12, be16(out + 2));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(FIVE, out + 4, sizeof(FIVE));
    TEST_ASSERT_EQUAL_HEX8(0x00, out[9]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[10]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[11]);

    TEST_ASSERT_EQUAL_size_t(4, protocore_nts_ef(NTS_EF_COOKIE, NULL, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT16(4, be16(out + 2));
}

void test_extension_field_types_match_the_registry(void)
{
    uint8_t out[64];
    static const uint8_t V[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    TEST_ASSERT_EQUAL_UINT16(0x0104, NTS_EF_UNIQUE_IDENTIFIER);
    TEST_ASSERT_EQUAL_UINT16(0x0204, NTS_EF_COOKIE);
    TEST_ASSERT_EQUAL_UINT16(0x0304, NTS_EF_COOKIE_PLACEHOLDER);
    TEST_ASSERT_EQUAL_UINT16(0x0404, NTS_EF_AUTH_AND_ENCRYPTED);

    TEST_ASSERT_EQUAL_size_t(12, protocore_nts_ef_unique_id(V, sizeof(V), out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT16(NTS_EF_UNIQUE_IDENTIFIER, be16(out));

    TEST_ASSERT_EQUAL_size_t(12, protocore_nts_ef_cookie(V, sizeof(V), out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT16(NTS_EF_COOKIE, be16(out));
}

void test_exporter_label_is_the_registered_string(void)
{
    TEST_ASSERT_EQUAL_STRING("EXPORTER-network-time-security", NTS_EXPORTER_LABEL);
}

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

void test_ke_record_fails_closed(void)
{
    static const uint8_t BODY[2] = {0, 0};
    uint8_t out[8];
    TEST_ASSERT_EQUAL_size_t(0, protocore_nts_ke_record(PROTO_TRUE, NTS_KE_NEXT_PROTOCOL, BODY, 2, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, protocore_nts_ke_record(PROTO_TRUE, NTS_KE_NEXT_PROTOCOL, NULL, 2, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, protocore_nts_ke_record(PROTO_TRUE, NTS_KE_NEXT_PROTOCOL, BODY, 2, out, 5));
    TEST_ASSERT_EQUAL_size_t(6, protocore_nts_ke_record(PROTO_TRUE, NTS_KE_NEXT_PROTOCOL, BODY, 2, out, 6));

    TEST_ASSERT_EQUAL_size_t(0,
                             protocore_nts_ke_record(PROTO_TRUE, NTS_KE_NEXT_PROTOCOL, BODY, 0x10000, out, sizeof(out)));
}

void test_ke_request_needs_all_sixteen_octets(void)
{
    uint8_t out[32];
    TEST_ASSERT_EQUAL_size_t(0, protocore_nts_ke_request(out, 5));
    TEST_ASSERT_EQUAL_size_t(0, protocore_nts_ke_request(out, 11));
    TEST_ASSERT_EQUAL_size_t(0, protocore_nts_ke_request(out, 15));
    TEST_ASSERT_EQUAL_size_t(16, protocore_nts_ke_request(out, 16));
}

void test_extension_field_length_bound(void)
{
    static uint8_t big[65536];
    static uint8_t out[65536];
    memset(big, 0x5A, sizeof(big));

    TEST_ASSERT_EQUAL_size_t(0, protocore_nts_ef(NTS_EF_COOKIE, big, 65529, out, sizeof(out)));

    TEST_ASSERT_EQUAL_size_t(65532, protocore_nts_ef(NTS_EF_COOKIE, big, 65528, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT16(65532, be16(out + 2));
}

void test_extension_field_fails_closed(void)
{
    uint8_t out[64];
    static const uint8_t V[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_EQUAL_size_t(0, protocore_nts_ef(NTS_EF_COOKIE, V, sizeof(V), NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, protocore_nts_ef(NTS_EF_COOKIE, NULL, 8, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, protocore_nts_ef(NTS_EF_COOKIE, V, sizeof(V), out, 11));
    TEST_ASSERT_EQUAL_size_t(12, protocore_nts_ef(NTS_EF_COOKIE, V, sizeof(V), out, 12));
}
