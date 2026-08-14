// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the UTMC common-database codec (services/transportation/utmc/utmc.h).
//
// The UTMC technical specifications are UK DfT documents and are not obtainable here, so the two
// document shapes are asserted as the module documents them, not against a spec clause.
//
// The load-bearing case is test_attribute_values_use_the_xml_predefined_entities, and that one IS
// standard-anchored: XML 1.0 sec 4.6 defines exactly five predefined entities, and sec 2.4 requires
// `&` and `<` to be escaped everywhere while `"` must be escaped inside a double-quoted attribute
// value and `'` need not be. A detector id or a sign message carrying an ampersand would otherwise
// produce a document no XML parser will accept, which is the whole reason the escaper exists.

#include "services/transportation/utmc/utmc.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// The request document: one object id in an XML declaration plus a UTMCRequest element.
void test_request_document(void)
{
    static const char WANT[] = "<?xml version=\"1.0\"?><UTMCRequest><object id=\"D1234\"/></UTMCRequest>";
    char out[256];

    TEST_ASSERT_EQUAL_UINT(sizeof(WANT) - 1u, protocore_utmc_request("D1234", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING(WANT, out);
}

// The response document: the object id, its value, the quality flag as a decimal, and a timestamp.
void test_response_document(void)
{
    static const char WANT[] = "<?xml version=\"1.0\"?><UTMCResponse><object id=\"SG7\" value=\"3\" quality=\"0\" "
                               "timestamp=\"2026-08-13T12:00:00Z\"/></UTMCResponse>";
    char out[256];

    TEST_ASSERT_EQUAL_UINT(sizeof(WANT) - 1u, protocore_utmc_response("SG7", "3", UTMC_QUALITY_GOOD,
                                                                      "2026-08-13T12:00:00Z", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING(WANT, out);
}

// XML 1.0 sec 4.6: amp, lt, gt, quot, apos. Inside a double-quoted attribute value the escaper must
// replace &, <, > and ", and may leave ' as itself.
void test_attribute_values_use_the_xml_predefined_entities(void)
{
    static const char WANT[] = "<?xml version=\"1.0\"?><UTMCRequest><object "
                               "id=\"a&amp;b&lt;c&gt;d&quot;e'f\"/></UTMCRequest>";
    char out[256];

    TEST_ASSERT_EQUAL_UINT(sizeof(WANT) - 1u, protocore_utmc_request("a&b<c>d\"e'f", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING(WANT, out);

    // the value and timestamp attributes go through the same escaper
    static const char WANT_R[] = "<?xml version=\"1.0\"?><UTMCResponse><object id=\"x\" value=\"1 &lt; 2\" "
                                 "quality=\"1\" timestamp=\"a&amp;b\"/></UTMCResponse>";
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT_R) - 1u,
                           protocore_utmc_response("x", "1 < 2", UTMC_QUALITY_SUSPECT, "a&b", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING(WANT_R, out);
}

// Each quality flag renders as its own decimal, so a suspect reading cannot be read as a good one.
void test_quality_flag_renders_as_a_decimal(void)
{
    char out[256];
    static const uint8_t Q[3] = {UTMC_QUALITY_GOOD, UTMC_QUALITY_SUSPECT, UTMC_QUALITY_ABSENT};
    static const char *const WANT[3] = {"quality=\"0\"", "quality=\"1\"", "quality=\"2\""};

    for (size_t i = 0; i < 3; i++)
    {
        TEST_ASSERT_TRUE(protocore_utmc_response("o", "v", Q[i], "t", out, sizeof(out)) > 0);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, WANT[i]), WANT[i]);
    }
    TEST_ASSERT_NOT_EQUAL(UTMC_QUALITY_GOOD, UTMC_QUALITY_SUSPECT);
    TEST_ASSERT_NOT_EQUAL(UTMC_QUALITY_SUSPECT, UTMC_QUALITY_ABSENT);
}

// What request() writes, parse_request() reads back, for an id with nothing to escape.
void test_request_round_trip(void)
{
    static const char *const IDS[4] = {"D1234", "A", "detector/007", "a.b.c-1_2"};
    char doc[256];
    char id[64];

    for (size_t i = 0; i < 4; i++)
    {
        size_t n = protocore_utmc_request(IDS[i], doc, sizeof(doc));
        TEST_ASSERT_TRUE(n > 0);
        TEST_ASSERT_EQUAL_UINT(strlen(IDS[i]), protocore_utmc_parse_request(doc, n, id, sizeof(id)));
        TEST_ASSERT_EQUAL_STRING(IDS[i], id);
    }
}

// The parser copies the raw attribute text and stops at the closing quote. It does not unescape, so
// an id that needed escaping comes back in its escaped form - the caller's XML layer owns that.
void test_parse_returns_the_raw_attribute_text(void)
{
    static const char DOC[] = "<?xml version=\"1.0\"?><UTMCRequest><object id=\"a&amp;b\"/></UTMCRequest>";
    char id[64];

    TEST_ASSERT_EQUAL_UINT(7u, protocore_utmc_parse_request(DOC, sizeof(DOC) - 1u, id, sizeof(id)));
    TEST_ASSERT_EQUAL_STRING("a&amp;b", id);
}

// An empty attribute is a legal find of length zero, not a failure.
void test_parse_accepts_an_empty_id(void)
{
    static const char DOC[] = "<UTMCRequest><object id=\"\"/></UTMCRequest>";
    char id[64];

    id[0] = 'x';
    TEST_ASSERT_EQUAL_UINT(0u, protocore_utmc_parse_request(DOC, sizeof(DOC) - 1u, id, sizeof(id)));
    TEST_ASSERT_EQUAL_CHAR('\0', id[0]);
}

// No id attribute, an unterminated one, and a truncated document all report 0 rather than a
// half-copied object name.
void test_parse_refuses_malformed_documents(void)
{
    char id[64];

    static const char NO_ID[] = "<UTMCRequest><object/></UTMCRequest>";
    TEST_ASSERT_EQUAL_UINT(0u, protocore_utmc_parse_request(NO_ID, sizeof(NO_ID) - 1u, id, sizeof(id)));

    static const char UNTERMINATED[] = "<UTMCRequest><object id=\"D1234";
    TEST_ASSERT_EQUAL_UINT(0u, protocore_utmc_parse_request(UNTERMINATED, sizeof(UNTERMINATED) - 1u, id, sizeof(id)));

    TEST_ASSERT_EQUAL_UINT(0u, protocore_utmc_parse_request(NULL, 10, id, sizeof(id)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_utmc_parse_request(NO_ID, sizeof(NO_ID) - 1u, NULL, sizeof(id)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_utmc_parse_request(NO_ID, sizeof(NO_ID) - 1u, id, 0));
}

// An id longer than the caller's buffer is refused whole: half an object name addresses a different
// object.
void test_parse_refuses_an_oversized_id(void)
{
    static const char DOC[] = "<UTMCRequest><object id=\"D1234\"/></UTMCRequest>";
    char id[6]; // holds "D1234" plus the NUL, exactly

    TEST_ASSERT_EQUAL_UINT(5u, protocore_utmc_parse_request(DOC, sizeof(DOC) - 1u, id, sizeof(id)));
    TEST_ASSERT_EQUAL_STRING("D1234", id);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_utmc_parse_request(DOC, sizeof(DOC) - 1u, id, 5u));
}

// A document that does not fit reports 0 rather than a truncated one, and the boundary is exact: the
// terminator needs the octet past the text.
void test_build_overflow_is_refused_whole(void)
{
    static const char WANT[] = "<?xml version=\"1.0\"?><UTMCRequest><object id=\"D1\"/></UTMCRequest>";
    const size_t need = sizeof(WANT) - 1u;
    char out[256];

    TEST_ASSERT_EQUAL_UINT(need, protocore_utmc_request("D1", out, need + 1u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_utmc_request("D1", out, need));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_utmc_request("D1", out, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_utmc_request("D1", NULL, sizeof(out)));

    TEST_ASSERT_EQUAL_UINT(0u, protocore_utmc_response("o", "v", UTMC_QUALITY_GOOD, "t", out, 16));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_utmc_response("o", "v", UTMC_QUALITY_GOOD, "t", NULL, sizeof(out)));
}

// A null id renders as an empty attribute rather than as a crash or the text "(null)".
void test_null_text_renders_empty(void)
{
    static const char WANT[] = "<?xml version=\"1.0\"?><UTMCRequest><object id=\"\"/></UTMCRequest>";
    char out[256];

    TEST_ASSERT_EQUAL_UINT(sizeof(WANT) - 1u, protocore_utmc_request(NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING(WANT, out);
}
