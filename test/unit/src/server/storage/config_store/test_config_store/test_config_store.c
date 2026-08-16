// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the typed NVS configuration store (server/storage/config_store/config_store.h).
//
// The load-bearing case is test_an_overlong_key_is_refused_not_truncated. ESP-IDF's NVS caps a key
// at NVS_KEY_NAME_MAX_SIZE = 16 bytes including the terminator, which config_store.h restates as
// "Keys are limited to 15 chars (NVS), plus null". A store that truncated instead of refusing would
// alias two settings whose first 15 characters match onto one entry, and a device would silently
// come up with the wrong credential. Everything else here is category 3: typed round-trips,
// per-namespace isolation, and the default a missing key reports.

#include "server/storage/config_store/config_store.h"
#include <string.h>

#include <unity.h>

static const char *get_str(const char *key, const char *def)
{
    static char buf[64];
    ConfigStore.get_str_args.key = key;
    ConfigStore.get_str_args.out = buf;
    ConfigStore.get_str_args.out_cap = sizeof(buf);
    ConfigStore.get_str_args.def = def;
    ConfigStore.get_str(protocore_config_store_span());
    return buf;
}

// clear only empties the namespace it is called on, so every namespace a case touches is emptied.
void setUp(void)
{
    ConfigStore.begin_args.ns = "wifi";
    ConfigStore.begin(protocore_config_store_span());
    ConfigStore.clear(protocore_config_store_span());
    ConfigStore.begin_args.ns = "net";
    ConfigStore.begin(protocore_config_store_span());
    ConfigStore.clear(protocore_config_store_span());
    ConfigStore.begin_args.ns = "t";
    ConfigStore.begin(protocore_config_store_span());
    ConfigStore.clear(protocore_config_store_span());
}
void tearDown(void)
{
}

// A string comes back exactly as it was stored, including one that is empty and one that fills the
// destination.
void test_string_round_trip(void)
{
    ConfigStore.set_str_args.key = "ssid";
    ConfigStore.set_str_args.val = "my network";
    ConfigStore.set_str(protocore_config_store_span());
    TEST_ASSERT_TRUE(ConfigStore.ok);
    TEST_ASSERT_EQUAL_STRING("my network", get_str("ssid", "?"));

    ConfigStore.set_str_args.key = "ssid";
    ConfigStore.set_str_args.val = "other";
    ConfigStore.set_str(protocore_config_store_span());
    TEST_ASSERT_TRUE(ConfigStore.ok);
    TEST_ASSERT_EQUAL_STRING("other", get_str("ssid", "?"));

    // The returned count is the characters written, so it agrees with the string's own length.
    char buf[32];
    ConfigStore.get_str_args.key = "ssid";
    ConfigStore.get_str_args.out = buf;
    ConfigStore.get_str_args.out_cap = sizeof(buf);
    ConfigStore.get_str_args.def = "";
    ConfigStore.get_str(protocore_config_store_span());
    TEST_ASSERT_EQUAL_size_t(5u, ConfigStore.n);
}

// A u32 round-trips across the whole 32-bit range, including both ends.
void test_u32_round_trip(void)
{
    ConfigStore.set_u32_args.key = "port";
    ConfigStore.set_u32_args.val = 0u;
    ConfigStore.set_u32(protocore_config_store_span());
    TEST_ASSERT_TRUE(ConfigStore.ok);
    ConfigStore.get_u32_args.key = "port";
    ConfigStore.get_u32_args.def = 999u;
    ConfigStore.get_u32(protocore_config_store_span());
    TEST_ASSERT_EQUAL_UINT32(0u, ConfigStore.ms);

    ConfigStore.set_u32_args.key = "port";
    ConfigStore.set_u32_args.val = 8080u;
    ConfigStore.set_u32(protocore_config_store_span());
    TEST_ASSERT_TRUE(ConfigStore.ok);
    ConfigStore.get_u32_args.key = "port";
    ConfigStore.get_u32_args.def = 999u;
    ConfigStore.get_u32(protocore_config_store_span());
    TEST_ASSERT_EQUAL_UINT32(8080u, ConfigStore.ms);

    ConfigStore.set_u32_args.key = "port";
    ConfigStore.set_u32_args.val = 4294967295u;
    ConfigStore.set_u32(protocore_config_store_span());
    TEST_ASSERT_TRUE(ConfigStore.ok);
    ConfigStore.get_u32_args.key = "port";
    ConfigStore.get_u32_args.def = 999u;
    ConfigStore.get_u32(protocore_config_store_span());
    TEST_ASSERT_EQUAL_UINT32(4294967295u, ConfigStore.ms);
}

// A blob round-trips byte for byte, embedded zeros and all - which is what distinguishes it from
// the string type.
void test_blob_round_trip(void)
{
    static const uint8_t key[] = {0xDE, 0xAD, 0x00, 0xBE, 0xEF};
    ConfigStore.set_blob_args.key = "psk";
    ConfigStore.set_blob_args.data = key;
    ConfigStore.set_blob_args.len = sizeof(key);
    ConfigStore.set_blob(protocore_config_store_span());
    TEST_ASSERT_TRUE(ConfigStore.ok);

    uint8_t out[8];
    memset(out, 0xAA, sizeof(out));
    ConfigStore.get_blob_args.key = "psk";
    ConfigStore.get_blob_args.out = out;
    ConfigStore.get_blob_args.out_cap = sizeof(out);
    ConfigStore.get_blob(protocore_config_store_span());
    TEST_ASSERT_EQUAL_size_t(sizeof(key), ConfigStore.n);
    TEST_ASSERT_EQUAL_MEMORY(key, out, sizeof(key));
    TEST_ASSERT_EQUAL_UINT8(0xAA, out[5]); // nothing written past the blob's length
}

// A key that was never set reports the caller's default rather than a zero the caller cannot tell
// apart from a stored zero.
void test_an_absent_key_reports_the_default(void)
{
    TEST_ASSERT_EQUAL_STRING("fallback", get_str("nothere", "fallback"));
    ConfigStore.get_u32_args.key = "nothere";
    ConfigStore.get_u32_args.def = 4242u;
    ConfigStore.get_u32(protocore_config_store_span());
    TEST_ASSERT_EQUAL_UINT32(4242u, ConfigStore.ms);
    ConfigStore.get_blob_args.key = "nothere";
    ConfigStore.get_blob_args.out = (uint8_t[8]){0};
    ConfigStore.get_blob_args.out_cap = 8;
    ConfigStore.get_blob(protocore_config_store_span());
    TEST_ASSERT_EQUAL_size_t(0u, ConfigStore.n);

    // A null default is the empty string, not a dereference.
    TEST_ASSERT_EQUAL_STRING("", get_str("nothere", NULL));
}

// ESP-IDF's NVS caps a key at 16 bytes including the terminator. A 15-character key is stored; a
// 16-character one is refused outright, so two keys sharing a 15-character prefix cannot collapse
// onto one entry.
void test_an_overlong_key_is_refused_not_truncated(void)
{
    ConfigStore.set_str_args.key = "abcdefghijklmno";
    ConfigStore.set_str_args.val = "fits";
    ConfigStore.set_str(protocore_config_store_span());
    TEST_ASSERT_TRUE(ConfigStore.ok); // 15 characters
    TEST_ASSERT_EQUAL_STRING("fits", get_str("abcdefghijklmno", "?"));

    ConfigStore.set_str_args.key = "abcdefghijklmnoP";
    ConfigStore.set_str_args.val = "too long";
    ConfigStore.set_str(protocore_config_store_span());
    ConfigStore.set_str_args.key = "abcdefghijklmnoQ";
    ConfigStore.set_str_args.val = "too long";
    ConfigStore.set_str(protocore_config_store_span());
    TEST_ASSERT_FALSE(ConfigStore.ok); // 16
    TEST_ASSERT_FALSE(ConfigStore.ok);
    ConfigStore.set_u32_args.key = "abcdefghijklmnoP";
    ConfigStore.set_u32_args.val = 1u;
    ConfigStore.set_u32(protocore_config_store_span());
    TEST_ASSERT_FALSE(ConfigStore.ok);

    // Neither over-long key aliased onto the 15-character one, which still holds its own value.
    TEST_ASSERT_EQUAL_STRING("fits", get_str("abcdefghijklmno", "?"));
    TEST_ASSERT_EQUAL_STRING("?", get_str("abcdefghijklmnoP", "?"));

    ConfigStore.set_str_args.key = "";
    ConfigStore.set_str_args.val = "x";
    ConfigStore.set_str(protocore_config_store_span());
    // An empty key names nothing.
    TEST_ASSERT_FALSE(ConfigStore.ok);
}

// A namespace is part of a key's identity, so the same key name in two namespaces holds two values
// and neither read sees the other.
void test_namespaces_hold_separate_values_for_one_key(void)
{
    ConfigStore.begin_args.ns = "wifi";
    ConfigStore.begin(protocore_config_store_span());
    ConfigStore.set_str_args.key = "host";
    ConfigStore.set_str_args.val = "ap";
    ConfigStore.set_str(protocore_config_store_span());
    TEST_ASSERT_TRUE(ConfigStore.ok);

    ConfigStore.begin_args.ns = "net";
    ConfigStore.begin(protocore_config_store_span());
    ConfigStore.set_str_args.key = "host";
    ConfigStore.set_str_args.val = "gateway";
    ConfigStore.set_str(protocore_config_store_span());
    TEST_ASSERT_TRUE(ConfigStore.ok);
    TEST_ASSERT_EQUAL_STRING("gateway", get_str("host", "?"));

    ConfigStore.begin_args.ns = "wifi";
    ConfigStore.begin(protocore_config_store_span());
    TEST_ASSERT_EQUAL_STRING("ap", get_str("host", "?"));

    ConfigStore.clear(protocore_config_store_span());
    // Clearing one namespace leaves the other untouched.
    ConfigStore.ok;
    TEST_ASSERT_EQUAL_STRING("?", get_str("host", "?"));
    ConfigStore.begin_args.ns = "net";
    ConfigStore.begin(protocore_config_store_span());
    TEST_ASSERT_EQUAL_STRING("gateway", get_str("host", "?"));
}

// erase drops one key and reports whether it was there; clear drops every key in the namespace.
void test_erase_drops_one_key_and_clear_drops_them_all(void)
{
    ConfigStore.set_str_args.key = "a";
    ConfigStore.set_str_args.val = "1";
    ConfigStore.set_str(protocore_config_store_span());
    TEST_ASSERT_TRUE(ConfigStore.ok);
    ConfigStore.set_str_args.key = "b";
    ConfigStore.set_str_args.val = "2";
    ConfigStore.set_str(protocore_config_store_span());
    TEST_ASSERT_TRUE(ConfigStore.ok);

    ConfigStore.erase_args.key = "a";
    ConfigStore.erase(protocore_config_store_span());
    TEST_ASSERT_TRUE(ConfigStore.ok);
    ConfigStore.erase_args.key = "a";
    ConfigStore.erase(protocore_config_store_span());
    TEST_ASSERT_FALSE(ConfigStore.ok); // already gone
    TEST_ASSERT_EQUAL_STRING("?", get_str("a", "?"));
    TEST_ASSERT_EQUAL_STRING("2", get_str("b", "?"));

    ConfigStore.clear(protocore_config_store_span());
    TEST_ASSERT_TRUE(ConfigStore.ok);
    TEST_ASSERT_EQUAL_STRING("?", get_str("b", "?"));
}

// A namespace has the same name limit a key does, and one that cannot be opened leaves the store
// addressing nothing rather than a previous namespace.
void test_an_unusable_namespace_is_refused(void)
{
    ConfigStore.begin_args.ns = "t";
    ConfigStore.begin(protocore_config_store_span());
    TEST_ASSERT_TRUE(ConfigStore.ok);
    ConfigStore.set_str_args.key = "k";
    ConfigStore.set_str_args.val = "v";
    ConfigStore.set_str(protocore_config_store_span());
    TEST_ASSERT_TRUE(ConfigStore.ok);

    ConfigStore.begin_args.ns = NULL;
    ConfigStore.begin(protocore_config_store_span());
    TEST_ASSERT_FALSE(ConfigStore.ok);
    ConfigStore.begin_args.ns = "";
    ConfigStore.begin(protocore_config_store_span());
    TEST_ASSERT_FALSE(ConfigStore.ok);
    ConfigStore.begin_args.ns = "abcdefghijklmnoP";
    ConfigStore.begin(protocore_config_store_span());
    TEST_ASSERT_FALSE(ConfigStore.ok); // 16 characters

    // The refused open did not leave "t" addressable, so the value is not reachable by accident.
    TEST_ASSERT_EQUAL_STRING("?", get_str("k", "?"));

    ConfigStore.begin_args.ns = "t";
    ConfigStore.begin(protocore_config_store_span());
    TEST_ASSERT_TRUE(ConfigStore.ok);
    TEST_ASSERT_EQUAL_STRING("v", get_str("k", "?"));
}

// A read into a destination smaller than the value fills what fits and always terminates, so a
// caller never reads past its own buffer.
void test_a_short_destination_is_bounded_and_terminated(void)
{
    ConfigStore.set_str_args.key = "ssid";
    ConfigStore.set_str_args.val = "0123456789";
    ConfigStore.set_str(protocore_config_store_span());
    TEST_ASSERT_TRUE(ConfigStore.ok);

    char small[5];
    memset(small, 'Z', sizeof(small));
    ConfigStore.get_str_args.key = "ssid";
    ConfigStore.get_str_args.out = small;
    ConfigStore.get_str_args.out_cap = sizeof(small);
    ConfigStore.get_str_args.def = "";
    ConfigStore.get_str(protocore_config_store_span());
    size_t n = ConfigStore.n;
    TEST_ASSERT_EQUAL_size_t(4u, n);
    TEST_ASSERT_EQUAL_STRING("0123", small);

    // The default is bounded the same way when the key is absent.
    memset(small, 'Z', sizeof(small));
    ConfigStore.get_str_args.key = "nothere";
    ConfigStore.get_str_args.out = small;
    ConfigStore.get_str_args.out_cap = sizeof(small);
    ConfigStore.get_str_args.def = "0123456789";
    ConfigStore.get_str(protocore_config_store_span());
    n = ConfigStore.n;
    TEST_ASSERT_EQUAL_size_t(4u, n);
    TEST_ASSERT_EQUAL_STRING("0123", small);
}

// A read with no destination, or no room in it, writes nothing and reports nothing read.
void test_a_read_with_no_room_is_refused(void)
{
    ConfigStore.set_str_args.key = "ssid";
    ConfigStore.set_str_args.val = "value";
    ConfigStore.set_str(protocore_config_store_span());
    TEST_ASSERT_TRUE(ConfigStore.ok);

    char sentinel[4] = {'z', 'z', 'z', '\0'};
    ConfigStore.get_str_args.key = "ssid";
    ConfigStore.get_str_args.out = sentinel;
    ConfigStore.get_str_args.out_cap = 0;
    ConfigStore.get_str_args.def = "d";
    ConfigStore.get_str(protocore_config_store_span());
    TEST_ASSERT_EQUAL_size_t(0u, ConfigStore.n);
    TEST_ASSERT_EQUAL_CHAR('z', sentinel[0]);
    ConfigStore.get_str_args.key = "ssid";
    ConfigStore.get_str_args.out = NULL;
    ConfigStore.get_str_args.out_cap = 8;
    ConfigStore.get_str_args.def = "d";
    ConfigStore.get_str(protocore_config_store_span());
    TEST_ASSERT_EQUAL_size_t(0u, ConfigStore.n);

    ConfigStore.get_blob_args.key = "ssid";
    ConfigStore.get_blob_args.out = NULL;
    ConfigStore.get_blob_args.out_cap = 8;
    ConfigStore.get_blob(protocore_config_store_span());
    TEST_ASSERT_EQUAL_size_t(0u, ConfigStore.n);
    ConfigStore.get_blob_args.key = "ssid";
    ConfigStore.get_blob_args.out = sentinel;
    ConfigStore.get_blob_args.out_cap = 0;
    ConfigStore.get_blob(protocore_config_store_span());
    TEST_ASSERT_EQUAL_size_t(0u, ConfigStore.n);
    TEST_ASSERT_EQUAL_CHAR('z', sentinel[0]);
}

// A write with no value is refused rather than stored as something else.
void test_a_write_with_no_value_is_refused(void)
{
    ConfigStore.set_str_args.key = "ssid";
    ConfigStore.set_str_args.val = NULL;
    ConfigStore.set_str(protocore_config_store_span());
    TEST_ASSERT_FALSE(ConfigStore.ok);
    ConfigStore.set_blob_args.key = "psk";
    ConfigStore.set_blob_args.data = NULL;
    ConfigStore.set_blob_args.len = 4;
    ConfigStore.set_blob(protocore_config_store_span());
    TEST_ASSERT_FALSE(ConfigStore.ok);
    TEST_ASSERT_EQUAL_STRING("?", get_str("ssid", "?"));
}
