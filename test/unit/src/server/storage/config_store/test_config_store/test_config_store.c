// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
    protocore_config_get_str(key, buf, sizeof(buf), def);
    return buf;
}

// clear only empties the namespace it is called on, so every namespace a case touches is emptied.
void setUp(void)
{
    protocore_config_begin("wifi");
    protocore_config_clear();
    protocore_config_begin("net");
    protocore_config_clear();
    protocore_config_begin("t");
    protocore_config_clear();
}
void tearDown(void)
{
}

// A string comes back exactly as it was stored, including one that is empty and one that fills the
// destination.
void test_string_round_trip(void)
{
    TEST_ASSERT_TRUE(protocore_config_set_str("ssid", "my network"));
    TEST_ASSERT_EQUAL_STRING("my network", get_str("ssid", "?"));

    TEST_ASSERT_TRUE(protocore_config_set_str("ssid", "other"));
    TEST_ASSERT_EQUAL_STRING("other", get_str("ssid", "?"));

    // The returned count is the characters written, so it agrees with the string's own length.
    char buf[32];
    TEST_ASSERT_EQUAL_size_t(5u, protocore_config_get_str("ssid", buf, sizeof(buf), ""));
}

// A u32 round-trips across the whole 32-bit range, including both ends.
void test_u32_round_trip(void)
{
    TEST_ASSERT_TRUE(protocore_config_set_u32("port", 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_config_get_u32("port", 999u));

    TEST_ASSERT_TRUE(protocore_config_set_u32("port", 8080u));
    TEST_ASSERT_EQUAL_UINT32(8080u, protocore_config_get_u32("port", 999u));

    TEST_ASSERT_TRUE(protocore_config_set_u32("port", 4294967295u));
    TEST_ASSERT_EQUAL_UINT32(4294967295u, protocore_config_get_u32("port", 999u));
}

// A blob round-trips byte for byte, embedded zeros and all - which is what distinguishes it from
// the string type.
void test_blob_round_trip(void)
{
    static const uint8_t key[] = {0xDE, 0xAD, 0x00, 0xBE, 0xEF};
    TEST_ASSERT_TRUE(protocore_config_set_blob("psk", key, sizeof(key)));

    uint8_t out[8];
    memset(out, 0xAA, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(sizeof(key), protocore_config_get_blob("psk", out, sizeof(out)));
    TEST_ASSERT_EQUAL_MEMORY(key, out, sizeof(key));
    TEST_ASSERT_EQUAL_UINT8(0xAA, out[5]); // nothing written past the blob's length
}

// A key that was never set reports the caller's default rather than a zero the caller cannot tell
// apart from a stored zero.
void test_an_absent_key_reports_the_default(void)
{
    TEST_ASSERT_EQUAL_STRING("fallback", get_str("nothere", "fallback"));
    TEST_ASSERT_EQUAL_UINT32(4242u, protocore_config_get_u32("nothere", 4242u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_config_get_blob("nothere", (uint8_t[8]){0}, 8));

    // A null default is the empty string, not a dereference.
    TEST_ASSERT_EQUAL_STRING("", get_str("nothere", NULL));
}

// ESP-IDF's NVS caps a key at 16 bytes including the terminator. A 15-character key is stored; a
// 16-character one is refused outright, so two keys sharing a 15-character prefix cannot collapse
// onto one entry.
void test_an_overlong_key_is_refused_not_truncated(void)
{
    TEST_ASSERT_TRUE(protocore_config_set_str("abcdefghijklmno", "fits")); // 15 characters
    TEST_ASSERT_EQUAL_STRING("fits", get_str("abcdefghijklmno", "?"));

    TEST_ASSERT_FALSE(protocore_config_set_str("abcdefghijklmnoP", "too long")); // 16
    TEST_ASSERT_FALSE(protocore_config_set_str("abcdefghijklmnoQ", "too long"));
    TEST_ASSERT_FALSE(protocore_config_set_u32("abcdefghijklmnoP", 1u));

    // Neither over-long key aliased onto the 15-character one, which still holds its own value.
    TEST_ASSERT_EQUAL_STRING("fits", get_str("abcdefghijklmno", "?"));
    TEST_ASSERT_EQUAL_STRING("?", get_str("abcdefghijklmnoP", "?"));

    // An empty key names nothing.
    TEST_ASSERT_FALSE(protocore_config_set_str("", "x"));
}

// A namespace is part of a key's identity, so the same key name in two namespaces holds two values
// and neither read sees the other.
void test_namespaces_hold_separate_values_for_one_key(void)
{
    protocore_config_begin("wifi");
    TEST_ASSERT_TRUE(protocore_config_set_str("host", "ap"));

    protocore_config_begin("net");
    TEST_ASSERT_TRUE(protocore_config_set_str("host", "gateway"));
    TEST_ASSERT_EQUAL_STRING("gateway", get_str("host", "?"));

    protocore_config_begin("wifi");
    TEST_ASSERT_EQUAL_STRING("ap", get_str("host", "?"));

    // Clearing one namespace leaves the other untouched.
    protocore_config_clear();
    TEST_ASSERT_EQUAL_STRING("?", get_str("host", "?"));
    protocore_config_begin("net");
    TEST_ASSERT_EQUAL_STRING("gateway", get_str("host", "?"));
}

// erase drops one key and reports whether it was there; clear drops every key in the namespace.
void test_erase_drops_one_key_and_clear_drops_them_all(void)
{
    TEST_ASSERT_TRUE(protocore_config_set_str("a", "1"));
    TEST_ASSERT_TRUE(protocore_config_set_str("b", "2"));

    TEST_ASSERT_TRUE(protocore_config_erase("a"));
    TEST_ASSERT_FALSE(protocore_config_erase("a")); // already gone
    TEST_ASSERT_EQUAL_STRING("?", get_str("a", "?"));
    TEST_ASSERT_EQUAL_STRING("2", get_str("b", "?"));

    TEST_ASSERT_TRUE(protocore_config_clear());
    TEST_ASSERT_EQUAL_STRING("?", get_str("b", "?"));
}

// A namespace has the same name limit a key does, and one that cannot be opened leaves the store
// addressing nothing rather than a previous namespace.
void test_an_unusable_namespace_is_refused(void)
{
    TEST_ASSERT_TRUE(protocore_config_begin("t"));
    TEST_ASSERT_TRUE(protocore_config_set_str("k", "v"));

    TEST_ASSERT_FALSE(protocore_config_begin(NULL));
    TEST_ASSERT_FALSE(protocore_config_begin(""));
    TEST_ASSERT_FALSE(protocore_config_begin("abcdefghijklmnoP")); // 16 characters

    // The refused open did not leave "t" addressable, so the value is not reachable by accident.
    TEST_ASSERT_EQUAL_STRING("?", get_str("k", "?"));

    TEST_ASSERT_TRUE(protocore_config_begin("t"));
    TEST_ASSERT_EQUAL_STRING("v", get_str("k", "?"));
}

// A read into a destination smaller than the value fills what fits and always terminates, so a
// caller never reads past its own buffer.
void test_a_short_destination_is_bounded_and_terminated(void)
{
    TEST_ASSERT_TRUE(protocore_config_set_str("ssid", "0123456789"));

    char small[5];
    memset(small, 'Z', sizeof(small));
    size_t n = protocore_config_get_str("ssid", small, sizeof(small), "");
    TEST_ASSERT_EQUAL_size_t(4u, n);
    TEST_ASSERT_EQUAL_STRING("0123", small);

    // The default is bounded the same way when the key is absent.
    memset(small, 'Z', sizeof(small));
    n = protocore_config_get_str("nothere", small, sizeof(small), "0123456789");
    TEST_ASSERT_EQUAL_size_t(4u, n);
    TEST_ASSERT_EQUAL_STRING("0123", small);
}

// A read with no destination, or no room in it, writes nothing and reports nothing read.
void test_a_read_with_no_room_is_refused(void)
{
    TEST_ASSERT_TRUE(protocore_config_set_str("ssid", "value"));

    char sentinel[4] = {'z', 'z', 'z', '\0'};
    TEST_ASSERT_EQUAL_size_t(0u, protocore_config_get_str("ssid", sentinel, 0, "d"));
    TEST_ASSERT_EQUAL_CHAR('z', sentinel[0]);
    TEST_ASSERT_EQUAL_size_t(0u, protocore_config_get_str("ssid", NULL, 8, "d"));

    TEST_ASSERT_EQUAL_size_t(0u, protocore_config_get_blob("ssid", NULL, 8));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_config_get_blob("ssid", sentinel, 0));
    TEST_ASSERT_EQUAL_CHAR('z', sentinel[0]);
}

// A write with no value is refused rather than stored as something else.
void test_a_write_with_no_value_is_refused(void)
{
    TEST_ASSERT_FALSE(protocore_config_set_str("ssid", NULL));
    TEST_ASSERT_FALSE(protocore_config_set_blob("psk", NULL, 4));
    TEST_ASSERT_EQUAL_STRING("?", get_str("ssid", "?"));
}
