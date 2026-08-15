// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the IEEE 2030.5 (Smart Energy Profile 2.0) resource codec (services/energy/sep2/sep2.h).
//
// IEEE 2030.5 is not freely distributable, so the element and attribute names come from the module's
// own documented resource shapes. Two things here are standard and are asserted as such: the XML
// namespace name "urn:ieee:std:2030.5:ns", which IEEE 2030.5 publishes as the namespace every one of
// its documents lives in, and XML 1.0 sec 2.4 - "the ampersand character and the left angle bracket
// MUST NOT appear in their literal form", and the double-quote must be escaped inside an attribute
// value delimited by double quotes.
//
// test_xml_special_characters_are_escaped is the load-bearing case: an href or an mRID carrying an
// ampersand is exactly how a real deployment produces a document that is not well-formed XML, and a
// 2030.5 client answers that with a parse failure rather than with a curtailment.

#include "services/energy/sep2/sep2.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static char g_out[1024];

#define DECL "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
#define NS " xmlns=\"urn:ieee:std:2030.5:ns\""

// The root resource: the recommended poll rate as an attribute, and one link element per function-set
// list the device offers.
void test_device_capability_document(void)
{
    size_t n = protocore_sep2_device_capability(900u, "/edev", "/derp", g_out, sizeof(g_out));
    TEST_ASSERT_EQUAL_STRING(DECL "<DeviceCapability" NS " pollRate=\"900\">"
                                  "<EndDeviceListLink href=\"/edev\"/>"
                                  "<DERProgramListLink href=\"/derp\"/>"
                                  "</DeviceCapability>",
                             g_out);
    TEST_ASSERT_EQUAL_UINT(strlen(g_out), n);
    TEST_ASSERT_NOT_NULL(strstr(g_out, "urn:ieee:std:2030.5:ns"));
}

// The device registration: its href attribute plus the short-form and long-form identifiers. The sFDI
// is decimal and wider than 32 bits, so it is written through the 64-bit path.
void test_end_device_document(void)
{
    size_t n = protocore_sep2_end_device(68719476735u, "3E4F5A6B7C8D9E0F", "/edev/1", g_out, sizeof(g_out));
    TEST_ASSERT_EQUAL_STRING(DECL "<EndDevice" NS " href=\"/edev/1\">"
                                  "<sFDI>68719476735</sFDI>"
                                  "<lFDI>3E4F5A6B7C8D9E0F</lFDI>"
                                  "</EndDevice>",
                             g_out);
    TEST_ASSERT_EQUAL_UINT(strlen(g_out), n);

    // 2^36 - 1 above and 0 here bracket the range the identifier is defined over.
    TEST_ASSERT_TRUE(protocore_sep2_end_device(0u, "00", "/edev/0", g_out, sizeof(g_out)) > 0u);
    TEST_ASSERT_NOT_NULL(strstr(g_out, "<sFDI>0</sFDI>"));
}

// The control event: the message RID, the interval it applies over, and the real-power setpoint. A
// negative setpoint is how the DER is told to absorb rather than export, so its sign has to survive.
void test_der_control_document(void)
{
    size_t n = protocore_sep2_der_control("ABC123", 1700000000u, 3600u, -1500, g_out, sizeof(g_out));
    TEST_ASSERT_EQUAL_STRING(DECL "<DERControl" NS ">"
                                  "<mRID>ABC123</mRID>"
                                  "<interval><start>1700000000</start><duration>3600</duration></interval>"
                                  "<DERControlBase><opModFixedW>-1500</opModFixedW></DERControlBase>"
                                  "</DERControl>",
                             g_out);
    TEST_ASSERT_EQUAL_UINT(strlen(g_out), n);

    TEST_ASSERT_TRUE(protocore_sep2_der_control("M", 0u, 0u, 0, g_out, sizeof(g_out)) > 0u);
    TEST_ASSERT_NOT_NULL(strstr(g_out, "<opModFixedW>0</opModFixedW>"));
    TEST_ASSERT_TRUE(protocore_sep2_der_control("M", 0u, 0u, 2147483647, g_out, sizeof(g_out)) > 0u);
    TEST_ASSERT_NOT_NULL(strstr(g_out, "<opModFixedW>2147483647</opModFixedW>"));
    TEST_ASSERT_TRUE(protocore_sep2_der_control("M", 0u, 0u, -2147483647 - 1, g_out, sizeof(g_out)) > 0u);
    TEST_ASSERT_NOT_NULL(strstr(g_out, "<opModFixedW>-2147483648</opModFixedW>"));
    // The interval spans the full 32-bit epoch range the fields are defined over.
    TEST_ASSERT_TRUE(protocore_sep2_der_control("M", 4294967295u, 4294967295u, 0, g_out, sizeof(g_out)) > 0u);
    TEST_ASSERT_NOT_NULL(strstr(g_out, "<start>4294967295</start><duration>4294967295</duration>"));
}

// XML 1.0 sec 2.4: '&' and '<' must never appear literally, '>' is escaped for compatibility, and a
// '"' inside a double-quoted attribute value must be escaped. Every caller-supplied string here goes
// through that escape, in element content and in attribute values alike.
void test_xml_special_characters_are_escaped(void)
{
    size_t n = protocore_sep2_device_capability(1u, "/a&b", "/c<d>e\"f", g_out, sizeof(g_out));
    TEST_ASSERT_TRUE(n > 0u);
    TEST_ASSERT_NOT_NULL(strstr(g_out, "<EndDeviceListLink href=\"/a&amp;b\"/>"));
    TEST_ASSERT_NOT_NULL(strstr(g_out, "<DERProgramListLink href=\"/c&lt;d&gt;e&quot;f\"/>"));

    n = protocore_sep2_der_control("A&B<C>", 0u, 0u, 0, g_out, sizeof(g_out));
    TEST_ASSERT_TRUE(n > 0u);
    TEST_ASSERT_NOT_NULL(strstr(g_out, "<mRID>A&amp;B&lt;C&gt;</mRID>"));

    n = protocore_sep2_end_device(1u, "x&y", "/edev/\"1\"", g_out, sizeof(g_out));
    TEST_ASSERT_TRUE(n > 0u);
    TEST_ASSERT_NOT_NULL(strstr(g_out, "href=\"/edev/&quot;1&quot;\""));
    TEST_ASSERT_NOT_NULL(strstr(g_out, "<lFDI>x&amp;y</lFDI>"));

    // No literal '&' or '<' survives from the caller's text into the document body.
    for (size_t i = 0; i < n; i++)
    {
        if (g_out[i] == '&')
        {
            TEST_ASSERT_TRUE_MESSAGE(strncmp(g_out + i, "&amp;", 5) == 0 || strncmp(g_out + i, "&lt;", 4) == 0 ||
                                         strncmp(g_out + i, "&gt;", 4) == 0 || strncmp(g_out + i, "&quot;", 6) == 0,
                                     "bare ampersand in XML output");
        }
    }
}

// A null string member contributes nothing rather than being dereferenced, so the document stays
// well-formed with an empty attribute or element.
void test_null_strings_render_as_empty(void)
{
    TEST_ASSERT_TRUE(protocore_sep2_device_capability(60u, NULL, NULL, g_out, sizeof(g_out)) > 0u);
    TEST_ASSERT_NOT_NULL(strstr(g_out, "<EndDeviceListLink href=\"\"/>"));
    TEST_ASSERT_NOT_NULL(strstr(g_out, "<DERProgramListLink href=\"\"/>"));

    TEST_ASSERT_TRUE(protocore_sep2_end_device(1u, NULL, NULL, g_out, sizeof(g_out)) > 0u);
    TEST_ASSERT_NOT_NULL(strstr(g_out, "href=\"\""));
    TEST_ASSERT_NOT_NULL(strstr(g_out, "<lFDI></lFDI>"));

    TEST_ASSERT_TRUE(protocore_sep2_der_control(NULL, 0u, 0u, 0, g_out, sizeof(g_out)) > 0u);
    TEST_ASSERT_NOT_NULL(strstr(g_out, "<mRID></mRID>"));
}

// A buffer that cannot hold the whole document reports 0 rather than a truncated one: a half-written
// XML document is not parseable, and the length a caller would send with it would be a lie.
void test_overflow_reports_zero(void)
{
    size_t full = protocore_sep2_device_capability(900u, "/edev", "/derp", g_out, sizeof(g_out));
    TEST_ASSERT_TRUE(full > 0u);
    TEST_ASSERT_EQUAL_UINT(full, protocore_sep2_device_capability(900u, "/edev", "/derp", g_out, full + 1u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_sep2_device_capability(900u, "/edev", "/derp", g_out, full));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_sep2_device_capability(900u, "/edev", "/derp", g_out, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_sep2_device_capability(900u, "/edev", "/derp", NULL, sizeof(g_out)));

    size_t ed = protocore_sep2_end_device(1u, "AB", "/edev/1", g_out, sizeof(g_out));
    TEST_ASSERT_TRUE(ed > 0u);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_sep2_end_device(1u, "AB", "/edev/1", g_out, ed));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_sep2_end_device(1u, "AB", "/edev/1", NULL, sizeof(g_out)));

    size_t dc = protocore_sep2_der_control("M", 1u, 2u, 3, g_out, sizeof(g_out));
    TEST_ASSERT_TRUE(dc > 0u);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_sep2_der_control("M", 1u, 2u, 3, g_out, dc));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_sep2_der_control("M", 1u, 2u, 3, NULL, sizeof(g_out)));
}
