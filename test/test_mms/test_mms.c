// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/mms: the IEC 61850 MMS Read PDU codec.

#include "services/energy/mms/mms.h"
#include <string.h>
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static int find(const uint8_t *hay, size_t hlen, const uint8_t *needle, size_t nlen)
{
    if (nlen > hlen)
    {
        return -1;
    }
    for (size_t i = 0; i + nlen <= hlen; i++)
    {
        if (memcmp(hay + i, needle, nlen) == 0)
        {
            return (int)i;
        }
    }
    return -1;
}

void test_read_request_structure(void)
{
    uint8_t out[256];
    size_t n = pc_mms_read_request(42, "LD0/GGIO1$ST$Ind1$stVal", out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_HEX8(MMS_PDU_CONFIRMED_REQUEST, out[0]); // 0xA0
    // invokeID 42 -> 02 01 2A present near the front.
    const uint8_t iid[] = {0x02, 0x01, 0x2A};
    TEST_ASSERT_TRUE(find(out, n, iid, 3) >= 0);
    // The read service tag A4 is present.
    const uint8_t svc[] = {MMS_SERVICE_READ};
    TEST_ASSERT_TRUE(find(out, n, svc, 1) >= 0);
    // The VisibleString object name (1A) carrying the item.
    const uint8_t name[] = {0x1A, 0x17, 'L', 'D', '0', '/'};
    TEST_ASSERT_TRUE(find(out, n, name, sizeof(name)) >= 0);
}

void test_read_request_parse(void)
{
    uint8_t buf[256];
    size_t n = pc_mms_read_request(0x1234, "X", buf, sizeof(buf));
    MmsPdu p;
    TEST_ASSERT_TRUE(pc_mms_parse(buf, n, &p));
    TEST_ASSERT_EQUAL_HEX8(MMS_PDU_CONFIRMED_REQUEST, p.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(0x1234, p.invoke_id);
    TEST_ASSERT_EQUAL_HEX8(MMS_SERVICE_READ, p.service_tag);
    TEST_ASSERT_NOT_NULL(p.service_body);
}

void test_read_response_roundtrip(void)
{
    // A caller-encoded Data value: boolean-ish [3] BOOLEAN true -> 83 01 FF (context Data).
    uint8_t data[] = {0x83, 0x01, 0xFF};
    uint8_t buf[128];
    size_t n = pc_mms_read_response(7, data, sizeof(data), buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_HEX8(MMS_PDU_CONFIRMED_RESPONSE, buf[0]); // 0xA1
    MmsPdu p;
    TEST_ASSERT_TRUE(pc_mms_parse(buf, n, &p));
    TEST_ASSERT_EQUAL_UINT32(7, p.invoke_id);
    TEST_ASSERT_EQUAL_HEX8(MMS_SERVICE_READ, p.service_tag);
    // The data value survives inside the service body.
    TEST_ASSERT_TRUE(find(p.service_body, p.service_len, data, sizeof(data)) >= 0);
}

void test_parse_rejects_bad_tag(void)
{
    uint8_t bad[] = {0x30, 0x03, 0x02, 0x01, 0x01}; // SEQUENCE, not an MMS PDU tag
    MmsPdu p;
    TEST_ASSERT_FALSE(pc_mms_parse(bad, sizeof(bad), &p));
}

// invokeID INTEGER minimal encoding: id 0 emits a single 0x00 octet; an id whose
// top content byte has the MSB set gets a leading 0x00 so it stays unsigned.
void test_invoke_id_zero_and_msb(void)
{
    uint8_t buf[64];
    MmsPdu p;
    // id 0 -> int_content emits {0x00}; round-trips back to 0.
    size_t n = pc_mms_read_request(0, "x", buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(pc_mms_parse(buf, n, &p));
    TEST_ASSERT_EQUAL_UINT32(0, p.invoke_id);
    // id 0x80: top byte MSB set -> content is {0x00, 0x80}; decodes back to 0x80.
    n = pc_mms_read_request(0x80, "x", buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(pc_mms_parse(buf, n, &p));
    TEST_ASSERT_EQUAL_UINT32(0x80, p.invoke_id);
}

void test_read_request_bad_args(void)
{
    uint8_t out[256];
    TEST_ASSERT_EQUAL_UINT32(0, pc_mms_read_request(1, NULL, out, sizeof(out))); // null name
    TEST_ASSERT_EQUAL_UINT32(0, pc_mms_read_request(1, "x", NULL, sizeof(out))); // null out
    char longname[130];
    memset(longname, 'A', 129);
    longname[129] = 0; // 129 chars > 128 cap -> rejected
    TEST_ASSERT_EQUAL_UINT32(0, pc_mms_read_request(1, longname, out, sizeof(out)));
    // Tiny cap: the final PDU wrap overflows the destination -> 0.
    TEST_ASSERT_EQUAL_UINT32(0, pc_mms_read_request(1, "x", out, 4));
}

// A 128-char item name forces a >=128 (long-form 0x81) BER length at every wrap, so
// this drives the multi-octet len_octets/write_len encode path AND, once parsed back,
// the long-form length DECODE path for both the outer PDU and the service body.
void test_read_request_long_name_long_form(void)
{
    char name[129];
    memset(name, 'A', 128);
    name[128] = 0;
    uint8_t out[512];
    size_t n = pc_mms_read_request(0x1234, name, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_HEX8(0x81, out[1]); // outer length is long-form (one following octet)
    MmsPdu p;
    TEST_ASSERT_TRUE(pc_mms_parse(out, n, &p));
    TEST_ASSERT_EQUAL_UINT32(0x1234, p.invoke_id);
    TEST_ASSERT_EQUAL_HEX8(MMS_SERVICE_READ, p.service_tag);
    TEST_ASSERT_NOT_NULL(p.service_body);
    TEST_ASSERT_TRUE(p.service_len >= 128); // service length was decoded long-form
}

void test_read_response_bad_args_and_overflow(void)
{
    uint8_t out[512];
    uint8_t data3[] = {0x83, 0x01, 0xFF};
    // data_len set but data null -> reject.
    TEST_ASSERT_EQUAL_UINT32(0, pc_mms_read_response(7, NULL, 5, out, sizeof(out)));
    // null out -> reject.
    TEST_ASSERT_EQUAL_UINT32(0, pc_mms_read_response(7, data3, sizeof(data3), NULL, sizeof(out)));
    // Valid PDU but a tiny cap -> the final wrap overflows -> 0.
    TEST_ASSERT_EQUAL_UINT32(0, pc_mms_read_response(7, data3, sizeof(data3), out, 4));
    // Over-large AccessResult data is rejected at each successive nesting layer:
    uint8_t big[256];
    memset(big, 0x55, sizeof(big));
    TEST_ASSERT_EQUAL_UINT32(0, pc_mms_read_response(7, big, 254, out, sizeof(out))); // innermost wrap overflows
    TEST_ASSERT_EQUAL_UINT32(0, pc_mms_read_response(7, big, 251, out, sizeof(out))); // service wrap overflows
    TEST_ASSERT_EQUAL_UINT32(0, pc_mms_read_response(7, big, 250, out, sizeof(out))); // body assembly overflows
}

void test_parse_null_and_short(void)
{
    MmsPdu p;
    TEST_ASSERT_FALSE(pc_mms_parse(NULL, 8, &p));
    uint8_t one[] = {0xA0};
    TEST_ASSERT_FALSE(pc_mms_parse(one, 1, &p)); // len < 2
    uint8_t buf[8] = {0xA0, 0x02, 0x02, 0x01, 0x2A};
    TEST_ASSERT_FALSE(pc_mms_parse(buf, 5, NULL)); // null out
}

void test_parse_malformed(void)
{
    MmsPdu p;
    // Outer length in long form but the count byte is malformed (nb == 0).
    uint8_t bad_outer_nb[] = {0xA0, 0x80};
    TEST_ASSERT_FALSE(pc_mms_parse(bad_outer_nb, sizeof(bad_outer_nb), &p));
    // Outer length claims more body than is present.
    uint8_t over_body[] = {0xA0, 0x0A, 0x02, 0x01, 0x2A};
    TEST_ASSERT_FALSE(pc_mms_parse(over_body, sizeof(over_body), &p));
    // First inner element is not the invokeID INTEGER (0x02).
    uint8_t no_invoke[] = {0xA0, 0x03, 0x99, 0x01, 0x00};
    TEST_ASSERT_FALSE(pc_mms_parse(no_invoke, sizeof(no_invoke), &p));
    // invokeID length > 4.
    uint8_t big_id[] = {0xA0, 0x07, 0x02, 0x05, 0x01, 0x02, 0x03, 0x04, 0x05};
    TEST_ASSERT_FALSE(pc_mms_parse(big_id, sizeof(big_id), &p));
    // Service length in long form but the count byte is malformed (nb == 0).
    uint8_t bad_svc_nb[] = {0xA0, 0x05, 0x02, 0x01, 0x2A, 0xA4, 0x80};
    TEST_ASSERT_FALSE(pc_mms_parse(bad_svc_nb, sizeof(bad_svc_nb), &p));
    // Service length claims more than is present.
    uint8_t over_svc[] = {0xA0, 0x06, 0x02, 0x01, 0x2A, 0xA4, 0x05, 0x01};
    TEST_ASSERT_FALSE(pc_mms_parse(over_svc, sizeof(over_svc), &p));
}

// A confirmed PDU carrying ONLY the invokeID (no confirmedService) parses fine, with
// the service fields cleared.
void test_parse_no_service(void)
{
    uint8_t only_id[] = {0xA0, 0x03, 0x02, 0x01, 0x2A};
    MmsPdu p;
    TEST_ASSERT_TRUE(pc_mms_parse(only_id, sizeof(only_id), &p));
    TEST_ASSERT_EQUAL_UINT32(0x2A, p.invoke_id);
    TEST_ASSERT_EQUAL_HEX8(0, p.service_tag);
    TEST_ASSERT_NULL(p.service_body);
    TEST_ASSERT_EQUAL_UINT32(0, p.service_len);
}

// An AccessResult payload of 256 octets or more needs a 3-octet BER length, so the
// innermost wrap can never fit its 256-byte scratch: the builder must fail closed
// rather than truncate. (pc_mms_read_response bounds only `data`, not `data_len`.)
void test_read_response_rejects_over_long_payload(void)
{
    static uint8_t big[512];
    memset(big, 0x5A, sizeof(big));
    uint8_t out[1024];
    // 256 is the first length needing the 3-octet form; 1 + 3 + 256 = 260 > the 256-byte scratch.
    TEST_ASSERT_EQUAL_UINT32(0, pc_mms_read_response(1, big, 256, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT32(0, pc_mms_read_response(1, big, 512, out, sizeof(out)));
    // The destination is left untouched when the build fails closed.
    out[0] = 0xEE;
    TEST_ASSERT_EQUAL_UINT32(0, pc_mms_read_response(1, big, 300, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0xEE, out[0]);
}

// The one payload size that fills the 256-byte body buffer exactly drives the OUTER wrap
// to a 256-octet value, which BER must encode in the 3-octet (0x82) long form. The caller's
// cap is its own buffer, not the fixed scratch, so this succeeds and round-trips.
void test_read_response_three_octet_outer_length(void)
{
    static uint8_t data[247];
    memset(data, 0x33, sizeof(data));
    uint8_t out[512];
    // 247 -> inner 1+2+247 = 250 -> service 1+2+250 = 253 -> body 3 (invokeID) + 253 = 256 exactly.
    size_t n = pc_mms_read_response(7, data, sizeof(data), out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT32(260, n); // tag + 3 length octets + 256 value
    TEST_ASSERT_EQUAL_HEX8(MMS_PDU_CONFIRMED_RESPONSE, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x82, out[1]); // long form, two following length octets
    TEST_ASSERT_EQUAL_HEX8(0x01, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[3]); // 0x0100 == 256
    // The 2-byte long form decodes back, so the PDU still parses.
    MmsPdu p;
    TEST_ASSERT_TRUE(pc_mms_parse(out, n, &p));
    TEST_ASSERT_EQUAL_UINT32(7, p.invoke_id);
    TEST_ASSERT_EQUAL_HEX8(MMS_SERVICE_READ, p.service_tag);
    TEST_ASSERT_TRUE(find(p.service_body, p.service_len, data, sizeof(data)) >= 0);
    // One octet less of cap and it fails closed instead of truncating.
    TEST_ASSERT_EQUAL_UINT32(0, pc_mms_read_response(7, data, sizeof(data), out, n - 1));
}

// A zero-length AccessResult is legal: `data` may be null when data_len is 0, and the
// TLV writes the tag + length with no value copied.
void test_read_response_empty_data(void)
{
    uint8_t out[128];
    size_t n = pc_mms_read_response(3, NULL, 0, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_HEX8(MMS_PDU_CONFIRMED_RESPONSE, out[0]);
    MmsPdu p;
    TEST_ASSERT_TRUE(pc_mms_parse(out, n, &p));
    TEST_ASSERT_EQUAL_UINT32(3, p.invoke_id);
    TEST_ASSERT_EQUAL_HEX8(MMS_SERVICE_READ, p.service_tag);
    TEST_ASSERT_EQUAL_UINT32(2, p.service_len); // the empty A1 listOfAccessResult: tag + zero length
}

// The confirmed-error PDU tag is accepted by the parser alongside request/response.
void test_parse_confirmed_error_tag(void)
{
    uint8_t err[] = {MMS_PDU_CONFIRMED_ERROR, 0x03, 0x02, 0x01, 0x09};
    MmsPdu p;
    TEST_ASSERT_TRUE(pc_mms_parse(err, sizeof(err), &p));
    TEST_ASSERT_EQUAL_HEX8(MMS_PDU_CONFIRMED_ERROR, p.pdu_tag);
    TEST_ASSERT_EQUAL_UINT32(9, p.invoke_id);
    TEST_ASSERT_EQUAL_HEX8(0, p.service_tag);
}

// The remaining BER length-field rejections: an unsupported long form (nb > 2), a long
// form whose length octets run off the end, and a service length field that starts
// exactly at the end of the buffer.
void test_parse_length_field_guards(void)
{
    MmsPdu p;
    // Outer length long form with nb == 3: unsupported (only 1- and 2-byte forms are decoded).
    uint8_t nb3[] = {0xA0, 0x83, 0x00, 0x00, 0x01, 0x02, 0x01, 0x2A};
    TEST_ASSERT_FALSE(pc_mms_parse(nb3, sizeof(nb3), &p));
    // Outer length long form (nb == 1) but the length octet itself is past the end.
    uint8_t trunc[] = {0xA0, 0x81};
    TEST_ASSERT_FALSE(pc_mms_parse(trunc, sizeof(trunc), &p));
    // The service tag is the very last octet, so its length field starts at len -> out of bounds.
    uint8_t svc_at_end[] = {0xA0, 0x04, 0x02, 0x01, 0x2A, MMS_SERVICE_READ};
    TEST_ASSERT_FALSE(pc_mms_parse(svc_at_end, sizeof(svc_at_end), &p));
}

// Truncations around the invokeID: a body with no room for the invokeID header at all,
// and an invokeID whose declared content runs past the end of the buffer.
void test_parse_invoke_id_truncated(void)
{
    MmsPdu p;
    // Zero-length body: off + 2 is already past the 2-octet PDU.
    uint8_t empty_body[] = {0xA0, 0x00};
    TEST_ASSERT_FALSE(pc_mms_parse(empty_body, sizeof(empty_body), &p));
    // invokeID claims 4 content octets but only 1 is present.
    uint8_t short_id[] = {0xA0, 0x02, 0x02, 0x04, 0x01};
    TEST_ASSERT_FALSE(pc_mms_parse(short_id, sizeof(short_id), &p));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_read_request_structure);
    RUN_TEST(test_read_request_parse);
    RUN_TEST(test_read_response_roundtrip);
    RUN_TEST(test_parse_rejects_bad_tag);
    RUN_TEST(test_invoke_id_zero_and_msb);
    RUN_TEST(test_read_request_bad_args);
    RUN_TEST(test_read_request_long_name_long_form);
    RUN_TEST(test_read_response_bad_args_and_overflow);
    RUN_TEST(test_parse_null_and_short);
    RUN_TEST(test_parse_malformed);
    RUN_TEST(test_parse_no_service);
    RUN_TEST(test_read_response_rejects_over_long_payload);
    RUN_TEST(test_read_response_three_octet_outer_length);
    RUN_TEST(test_read_response_empty_data);
    RUN_TEST(test_parse_confirmed_error_tag);
    RUN_TEST(test_parse_length_field_guards);
    RUN_TEST(test_parse_invoke_id_truncated);
    return UNITY_END();
}
