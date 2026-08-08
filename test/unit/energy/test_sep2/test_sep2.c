// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/sep2: the IEEE 2030.5 resource document builders.

#include "services/energy/sep2/sep2.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static proto_bool has(const char *hay, const char *needle)
{
    return strstr(hay, needle) != NULL;
}

void test_device_capability(void)
{
    char buf[512];
    size_t n = pc_sep2_device_capability(900, "/edev", "/derp", buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
    TEST_ASSERT_TRUE(has(buf, "<DeviceCapability xmlns=\"urn:ieee:std:2030.5:ns\" pollRate=\"900\">"));
    TEST_ASSERT_TRUE(has(buf, "<EndDeviceListLink href=\"/edev\"/>"));
    TEST_ASSERT_TRUE(has(buf, "<DERProgramListLink href=\"/derp\"/>"));
    TEST_ASSERT_TRUE(has(buf, "</DeviceCapability>"));
}

void test_end_device(void)
{
    char buf[512];
    pc_sep2_end_device(123456789ULL, "3E4F...A1", "/edev/0", buf, sizeof(buf));
    TEST_ASSERT_TRUE(has(buf, "<EndDevice xmlns=\"urn:ieee:std:2030.5:ns\" href=\"/edev/0\">"));
    TEST_ASSERT_TRUE(has(buf, "<sFDI>123456789</sFDI>"));
    TEST_ASSERT_TRUE(has(buf, "<lFDI>3E4F...A1</lFDI>"));
}

void test_der_control_negative_setpoint(void)
{
    char buf[512];
    pc_sep2_der_control("A1B2", 1720000000u, 3600, -1500, buf, sizeof(buf));
    TEST_ASSERT_TRUE(has(buf, "<mRID>A1B2</mRID>"));
    TEST_ASSERT_TRUE(has(buf, "<start>1720000000</start>"));
    TEST_ASSERT_TRUE(has(buf, "<duration>3600</duration>"));
    TEST_ASSERT_TRUE(has(buf, "<opModFixedW>-1500</opModFixedW>"));
}

void test_xml_escape_in_href(void)
{
    char buf[512];
    pc_sep2_device_capability(60, "/e?a=1&b=2", "/d", buf, sizeof(buf));
    TEST_ASSERT_TRUE(has(buf, "href=\"/e?a=1&amp;b=2\""));
}

void test_overflow(void)
{
    char buf[16];
    TEST_ASSERT_EQUAL_size_t(0, pc_sep2_device_capability(900, "/edev", "/derp", buf, sizeof(buf)));
}

void test_device_capability_null_out_and_zero_cap(void)
{
    char buf[64];
    TEST_ASSERT_EQUAL_size_t(0, pc_sep2_device_capability(900, "/edev", "/derp", NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0, pc_sep2_device_capability(900, "/edev", "/derp", buf, 0));
}

void test_end_device_null_out_and_zero_cap(void)
{
    char buf[64];
    TEST_ASSERT_EQUAL_size_t(0, pc_sep2_end_device(123456789ULL, "3E4F...A1", "/edev/0", NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0, pc_sep2_end_device(123456789ULL, "3E4F...A1", "/edev/0", buf, 0));
}

void test_der_control_null_out_and_zero_cap(void)
{
    char buf[64];
    TEST_ASSERT_EQUAL_size_t(0, pc_sep2_der_control("A1B2", 1720000000u, 3600, -1500, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0, pc_sep2_der_control("A1B2", 1720000000u, 3600, -1500, buf, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_device_capability);
    RUN_TEST(test_end_device);
    RUN_TEST(test_der_control_negative_setpoint);
    RUN_TEST(test_xml_escape_in_href);
    RUN_TEST(test_overflow);
    RUN_TEST(test_device_capability_null_out_and_zero_cap);
    RUN_TEST(test_end_device_null_out_and_zero_cap);
    RUN_TEST(test_der_control_null_out_and_zero_cap);
    return UNITY_END();
}
