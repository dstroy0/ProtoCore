// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the captive-portal form reader (server/core/provisioning_service/provisioning_service.h).
//
// Two documents govern the body this parses. RFC 1866 sec 8.2.1 defines application/x-www-form-
// urlencoded: space becomes '+', every other escaped character becomes "%HH" holding its ASCII
// code, a name is separated from its value by '=', and pairs from each other by '&'. RFC 3986
// sec 2.1 defines the triplet itself, pct-encoded = "%" HEXDIG HEXDIG, and states that the
// uppercase hex digits are equivalent to the lowercase ones.
//
// test_the_specs_own_worked_escapes is the load-bearing case: both documents print an escape and
// say what octet it stands for - RFC 3986's "%20" for the space and RFC 1866's "%0D%0A" for a line
// break - and a decoder that reproduces those two is decoding, not guessing.

#include "server/core/provisioning_service/provisioning_service.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static char g_val[64];

// Read @p key out of @p body and return the decoded value, so each case reads as one assertion.
static const char *field(const char *body, const char *key)
{
    TEST_ASSERT_TRUE_MESSAGE(protocore_prov_form_field(body, key, g_val, sizeof(g_val)), key);
    return g_val;
}

// RFC 3986 sec 2.1: "%20 is the percent-encoding for the binary octet 00100000 (ABNF: %x20), which
// in US-ASCII corresponds to the space character (SP)".
// RFC 1866 sec 8.2.1: "Line breaks ... are represented as CR LF pairs, i.e. `%0D%0A'".
void test_the_specs_own_worked_escapes(void)
{
    TEST_ASSERT_EQUAL_STRING("a b", field("ssid=a%20b", "ssid"));
    TEST_ASSERT_EQUAL_STRING("a\r\nb", field("note=a%0D%0Ab", "note"));
}

// RFC 1866 sec 8.2.1 rule 1: "space characters are replaced by `+'".
void test_plus_decodes_to_a_space(void)
{
    TEST_ASSERT_EQUAL_STRING("My AP", field("ssid=My+AP", "ssid"));
    TEST_ASSERT_EQUAL_STRING("   ", field("ssid=+++", "ssid"));
}

// RFC 3986 sec 2.1: the triplet holds "the two hexadecimal digits representing that octet's numeric
// value". Each expectation below is that octet's US-ASCII code written in hex.
void test_a_triplet_is_the_octets_numeric_value(void)
{
    TEST_ASSERT_EQUAL_STRING("A", field("k=%41", "k"));           // 'A' is 0x41
    TEST_ASSERT_EQUAL_STRING("~", field("k=%7E", "k"));           // '~' is 0x7E
    TEST_ASSERT_EQUAL_STRING("%", field("k=%25", "k"));           // '%' is 0x25, so an escaped percent survives
    TEST_ASSERT_EQUAL_STRING("&", field("k=%26", "k"));           // '&' is 0x26, and it does not split the field
    TEST_ASSERT_EQUAL_STRING("=", field("k=%3D", "k"));           // '=' is 0x3D
    TEST_ASSERT_EQUAL_STRING("+", field("k=%2B", "k"));           // '+' is 0x2B, so a literal plus is not a space
    TEST_ASSERT_EQUAL_STRING("p@ss!", field("k=p%40ss%21", "k")); // '@' 0x40, '!' 0x21
}

// RFC 3986 sec 2.1: "The uppercase hexadecimal digits 'A' through 'F' are equivalent to the
// lowercase digits 'a' through 'f'".
void test_hex_digit_case_is_equivalent(void)
{
    TEST_ASSERT_EQUAL_STRING("/", field("k=%2F", "k"));
    TEST_ASSERT_EQUAL_STRING("/", field("k=%2f", "k"));
    TEST_ASSERT_EQUAL_STRING("\xEF", field("k=%eF", "k"));
}

// RFC 1866 sec 8.2.1 rule 2: "the name separated from the value by `=' and the pairs separated from
// each other by `&'". Reachable at the head, in the middle, and at the tail of the body.
void test_pairs_are_separated_by_ampersand(void)
{
    static const char *const BODY = "ssid=MyAP&psk=secret&chan=6";
    TEST_ASSERT_EQUAL_STRING("MyAP", field(BODY, "ssid"));
    TEST_ASSERT_EQUAL_STRING("secret", field(BODY, "psk"));
    TEST_ASSERT_EQUAL_STRING("6", field(BODY, "chan"));
}

// A field present with nothing after its '=' has the empty value, which is not the same answer as a
// field that is not there at all.
void test_an_empty_value_is_still_a_present_field(void)
{
    char v[8];
    TEST_ASSERT_TRUE(protocore_prov_form_field("ssid=&psk=x", "ssid", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("", v);
    TEST_ASSERT_FALSE(protocore_prov_form_field("ssid=x", "psk", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("", v);
}

// A name runs from the start of the body or from a '&' up to its '=', so neither a longer name
// ending in the key nor a longer name starting with it is that field.
void test_a_name_matches_only_a_whole_field(void)
{
    TEST_ASSERT_EQUAL_STRING("right", field("myssid=wrong&ssid=right", "ssid"));
    TEST_ASSERT_EQUAL_STRING("right", field("ssidx=wrong&ssid=right", "ssid"));
    TEST_ASSERT_EQUAL_STRING("right", field("xssid=wrong&ssid=right", "ssid"));
}

// RFC 3986 sec 2.1: pct-encoded = "%" HEXDIG HEXDIG. Anything else after a '%' is not a triplet, so
// the octets stay as they were written rather than becoming some other byte.
void test_an_incomplete_triplet_is_not_decoded(void)
{
    TEST_ASSERT_EQUAL_STRING("a%zzb", field("k=a%zzb", "k")); // neither digit is a HEXDIG
    TEST_ASSERT_EQUAL_STRING("a%4zb", field("k=a%4zb", "k")); // second digit is not a HEXDIG
    TEST_ASSERT_EQUAL_STRING("a%4", field("k=a%4", "k"));     // the body ends inside the triplet
    TEST_ASSERT_EQUAL_STRING("a%", field("k=a%", "k"));
}

// The value is bounded by the destination and always terminated, so a long passphrase truncates
// instead of running past the buffer.
void test_the_value_is_bounded_and_terminated(void)
{
    char v[4];
    TEST_ASSERT_TRUE(protocore_prov_form_field("ssid=abcdef", "ssid", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("abc", v);
    TEST_ASSERT_EQUAL_size_t(3u, strlen(v));

    char one[1];
    TEST_ASSERT_TRUE(protocore_prov_form_field("ssid=abcdef", "ssid", one, sizeof(one)));
    TEST_ASSERT_EQUAL_STRING("", one);
}

void test_null_arguments_and_zero_capacity_are_refused(void)
{
    char v[8];
    v[0] = 'x';
    TEST_ASSERT_FALSE(protocore_prov_form_field(NULL, "ssid", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("", v);
    TEST_ASSERT_FALSE(protocore_prov_form_field("ssid=x", NULL, v, sizeof(v)));
    TEST_ASSERT_FALSE(protocore_prov_form_field("ssid=x", "ssid", NULL, sizeof(v)));
    v[0] = 'x';
    TEST_ASSERT_FALSE(protocore_prov_form_field("ssid=x", "ssid", v, 0));
    TEST_ASSERT_EQUAL_CHAR('x', v[0]); // zero capacity writes nothing, not even a terminator
}

// There is no key/value store off-target, so a load reports no stored credentials and empties both
// destinations rather than leaving whatever the caller had there.
void test_the_host_credential_store_holds_nothing(void)
{
    char ssid[8] = "x";
    char psk[8] = "y";
    TEST_ASSERT_FALSE(protocore_provisioning_load(ssid, sizeof(ssid), psk, sizeof(psk)));
    TEST_ASSERT_EQUAL_STRING("", ssid);
    TEST_ASSERT_EQUAL_STRING("", psk);
    protocore_provisioning_clear();
    protocore_provisioning_begin("TestAP");
}

// A destination is only written through when it has both a pointer and room, so each of the two
// buffers is cleared independently of the other.
void test_load_writes_only_the_destinations_it_was_given(void)
{
    char psk[8] = "y";
    TEST_ASSERT_FALSE(protocore_provisioning_load(NULL, 8, psk, 0));
    TEST_ASSERT_EQUAL_STRING("y", psk);

    char ssid[8] = "z";
    TEST_ASSERT_FALSE(protocore_provisioning_load(ssid, 0, NULL, 8));
    TEST_ASSERT_EQUAL_STRING("z", ssid);
}
