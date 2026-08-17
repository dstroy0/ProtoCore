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

#include "core_setup/hal/nvs.h"                          // the store the save / load / clear paths use
#include "network_drivers/physical/physical.h"            // what begin() asked the radio for
#include "network_drivers/transport/udp/server/server.h"  // the port the catch-all DNS bound

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
    Prov.form_field_args.body = body;
    Prov.form_field_args.key = key;
    Prov.form_field_args.out = g_val;
    Prov.form_field_args.cap = sizeof(g_val);
    Prov.form_field(protocore_provisioning_service_span());
    TEST_ASSERT_TRUE_MESSAGE(Prov.ok, key);
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
    Prov.form_field_args.body = "ssid=&psk=x";
    Prov.form_field_args.key = "ssid";
    Prov.form_field_args.out = v;
    Prov.form_field_args.cap = sizeof(v);
    Prov.form_field(protocore_provisioning_service_span());
    TEST_ASSERT_TRUE(Prov.ok);
    TEST_ASSERT_EQUAL_STRING("", v);
    Prov.form_field_args.body = "ssid=x";
    Prov.form_field_args.key = "psk";
    Prov.form_field_args.out = v;
    Prov.form_field_args.cap = sizeof(v);
    Prov.form_field(protocore_provisioning_service_span());
    TEST_ASSERT_FALSE(Prov.ok);
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
    Prov.form_field_args.body = "ssid=abcdef";
    Prov.form_field_args.key = "ssid";
    Prov.form_field_args.out = v;
    Prov.form_field_args.cap = sizeof(v);
    Prov.form_field(protocore_provisioning_service_span());
    TEST_ASSERT_TRUE(Prov.ok);
    TEST_ASSERT_EQUAL_STRING("abc", v);
    TEST_ASSERT_EQUAL_size_t(3u, strlen(v));

    char one[1];
    Prov.form_field_args.body = "ssid=abcdef";
    Prov.form_field_args.key = "ssid";
    Prov.form_field_args.out = one;
    Prov.form_field_args.cap = sizeof(one);
    Prov.form_field(protocore_provisioning_service_span());
    TEST_ASSERT_TRUE(Prov.ok);
    TEST_ASSERT_EQUAL_STRING("", one);
}

void test_null_arguments_and_zero_capacity_are_refused(void)
{
    char v[8];
    v[0] = 'x';
    Prov.form_field_args.body = NULL;
    Prov.form_field_args.key = "ssid";
    Prov.form_field_args.out = v;
    Prov.form_field_args.cap = sizeof(v);
    Prov.form_field(protocore_provisioning_service_span());
    TEST_ASSERT_FALSE(Prov.ok);
    TEST_ASSERT_EQUAL_STRING("", v);
    Prov.form_field_args.body = "ssid=x";
    Prov.form_field_args.key = NULL;
    Prov.form_field_args.out = v;
    Prov.form_field_args.cap = sizeof(v);
    Prov.form_field(protocore_provisioning_service_span());
    TEST_ASSERT_FALSE(Prov.ok);
    Prov.form_field_args.body = "ssid=x";
    Prov.form_field_args.key = "ssid";
    Prov.form_field_args.out = NULL;
    Prov.form_field_args.cap = sizeof(v);
    Prov.form_field(protocore_provisioning_service_span());
    TEST_ASSERT_FALSE(Prov.ok);
    v[0] = 'x';
    Prov.form_field_args.body = "ssid=x";
    Prov.form_field_args.key = "ssid";
    Prov.form_field_args.out = v;
    Prov.form_field_args.cap = 0;
    Prov.form_field(protocore_provisioning_service_span());
    TEST_ASSERT_FALSE(Prov.ok);
    TEST_ASSERT_EQUAL_CHAR('x', v[0]); // zero capacity writes nothing, not even a terminator
}

// An empty store reports no stored credentials and empties both destinations rather than leaving
// whatever the caller had there. clear() first, so this does not depend on what ran before it.
void test_an_empty_credential_store_reports_nothing(void)
{
    Prov.clear(protocore_provisioning_service_span());
    char ssid[8] = "x";
    char psk[8] = "y";
    Prov.load_args.ssid = ssid;
    Prov.load_args.ssid_cap = sizeof(ssid);
    Prov.load_args.psk = psk;
    Prov.load_args.psk_cap = sizeof(psk);
    Prov.load(protocore_provisioning_service_span());
    TEST_ASSERT_FALSE(Prov.ok);
    TEST_ASSERT_EQUAL_STRING("", ssid);
    TEST_ASSERT_EQUAL_STRING("", psk);
}

// A destination is only written through when it has both a pointer and room, so each of the two
// buffers is cleared independently of the other.
void test_load_writes_only_the_destinations_it_was_given(void)
{
    char psk[8] = "y";
    Prov.load_args.ssid = NULL;
    Prov.load_args.ssid_cap = 8;
    Prov.load_args.psk = psk;
    Prov.load_args.psk_cap = 0;
    Prov.load(protocore_provisioning_service_span());
    TEST_ASSERT_FALSE(Prov.ok);
    TEST_ASSERT_EQUAL_STRING("y", psk);

    char ssid[8] = "z";
    Prov.load_args.ssid = ssid;
    Prov.load_args.ssid_cap = 0;
    Prov.load_args.psk = NULL;
    Prov.load_args.psk_cap = 8;
    Prov.load(protocore_provisioning_service_span());
    TEST_ASSERT_FALSE(Prov.ok);
    TEST_ASSERT_EQUAL_STRING("z", ssid);
}

// ---------------------------------------------------------------------------
// The portal half
// ---------------------------------------------------------------------------
//
// These were unreachable until the module stopped being keyed on PROTOCORE_HAS_VENDOR_NVS: every
// seam it uses already had a host arm (hal/nvs.h -> host_nvs.c, Physical.wifi_ap_* ->
// physical_mock.c, protocore_platform_restart -> the host counter), so the vendor condition was
// compiling working code out of the host build and putting a stub in its place.

// What the save path stores is what the load path returns. The store is the same hal/nvs.h the
// ESP arm uses, so this is the round trip and not a stand-in for it.
void test_saved_credentials_load_back(void)
{
    Prov.clear(protocore_provisioning_service_span());
    TEST_ASSERT_TRUE(protocore_nvs_put_str(PROTOCORE_PROV_NVS_NAMESPACE, PROTOCORE_PROV_KEY_SSID, "some-network"));
    TEST_ASSERT_TRUE(protocore_nvs_put_str(PROTOCORE_PROV_NVS_NAMESPACE, PROTOCORE_PROV_KEY_PSK, "a secret"));

    char ssid[33] = {0};
    char psk[64] = {0};
    Prov.load_args.ssid = ssid;
    Prov.load_args.ssid_cap = sizeof(ssid);
    Prov.load_args.psk = psk;
    Prov.load_args.psk_cap = sizeof(psk);
    Prov.load(protocore_provisioning_service_span());
    TEST_ASSERT_TRUE(Prov.ok);
    TEST_ASSERT_EQUAL_STRING("some-network", ssid);
    TEST_ASSERT_EQUAL_STRING("a secret", psk);
}

// An open access point has no passphrase, so a stored SSID on its own is still a usable credential
// and the PSK destination comes back empty rather than stale.
void test_an_ssid_without_a_passphrase_still_loads(void)
{
    Prov.clear(protocore_provisioning_service_span());
    TEST_ASSERT_TRUE(protocore_nvs_put_str(PROTOCORE_PROV_NVS_NAMESPACE, PROTOCORE_PROV_KEY_SSID, "open-ap"));

    char ssid[33] = {0};
    char psk[64] = "leftover";
    Prov.load_args.ssid = ssid;
    Prov.load_args.ssid_cap = sizeof(ssid);
    Prov.load_args.psk = psk;
    Prov.load_args.psk_cap = sizeof(psk);
    Prov.load(protocore_provisioning_service_span());
    TEST_ASSERT_TRUE(Prov.ok);
    TEST_ASSERT_EQUAL_STRING("open-ap", ssid);
    TEST_ASSERT_EQUAL_STRING("", psk);
}

// clear() drops the namespace, so credentials that loaded a moment ago stop loading. This is what
// puts a device back into provisioning.
void test_clear_takes_the_credentials_away(void)
{
    TEST_ASSERT_TRUE(protocore_nvs_put_str(PROTOCORE_PROV_NVS_NAMESPACE, PROTOCORE_PROV_KEY_SSID, "some-network"));
    char ssid[33] = {0};
    Prov.load_args.ssid = ssid;
    Prov.load_args.ssid_cap = sizeof(ssid);
    Prov.load_args.psk = NULL;
    Prov.load_args.psk_cap = 0;
    Prov.load(protocore_provisioning_service_span());
    TEST_ASSERT_TRUE(Prov.ok);

    Prov.clear(protocore_provisioning_service_span());
    Prov.load_args.ssid = ssid;
    Prov.load_args.ssid_cap = sizeof(ssid);
    Prov.load_args.psk = NULL;
    Prov.load_args.psk_cap = 0;
    Prov.load(protocore_provisioning_service_span());
    TEST_ASSERT_FALSE(Prov.ok);
    TEST_ASSERT_EQUAL_STRING("", ssid);
}

// begin() brings the softAP up under the name it was given and binds the catch-all DNS. The host
// arm of Physical records the AP it was asked for, so the name that reached the radio is assertable.
void test_begin_raises_the_softap_under_the_name_it_was_given(void)
{
    Prov.begin_args.ap_ssid = "ProtoCore-Setup";
    Prov.begin(protocore_provisioning_service_span());
    TEST_ASSERT_EQUAL_STRING("ProtoCore-Setup", Physical.wifi.ssid);
    TEST_ASSERT_NULL(Physical.wifi.password); // a provisioning AP is open, or nobody can reach it
}

// The catch-all DNS binds UDP/53: that is the whole hijack, and a portal that bound anything else
// would leave a client's queries going to the real resolver it cannot reach.
void test_begin_binds_the_catch_all_dns_on_port_53(void)
{
    Prov.begin_args.ap_ssid = "ProtoCore-Setup";
    Prov.begin(protocore_provisioning_service_span());
    TEST_ASSERT_EQUAL_UINT16(53, UdpListener.port);
    TEST_ASSERT_NOT_NULL(UdpListener.bind.handler);
    TEST_ASSERT_NULL(UdpListener.bind.group_ip); // a catch-all, not a multicast join
}
