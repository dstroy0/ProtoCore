// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for schema-driven config export / restore (server/storage/config_io/config_io.h).
//
// The blob format is this module's own, not a standard's, so every expectation here is category 3:
// a property the pair must hold whatever the implementation. The load-bearing one is
// test_export_import_round_trip - the whole point of the blob is to carry a device's settings to
// another device and back, so export followed by import into a wiped store must reproduce every
// field's value exactly. A formatter and a parser that disagree lose settings silently on restore.
//
// The key and value length limits come from the store this writes into: config_store.h states
// "Keys are limited to 15 chars (NVS), plus null".

#include "server/storage/config_io/config_io.h"
#include "server/storage/config_store/config_store.h"
#include <string.h>

#include <unity.h>

static uint8_t config_io_work[16]; // the borrow an entry takes; ConfigIo never reads it

static const protocore_cfg_field SCHEMA[] = {
    {"ssid", PROTOCORE_CFG_STR},
    {"port", PROTOCORE_CFG_U32},
    {"name", PROTOCORE_CFG_STR},
};
static const size_t N = sizeof(SCHEMA) / sizeof(SCHEMA[0]);

void setUp(void)
{
    ConfigStore.begin_args.ns = "t";
    ConfigStore.begin(protocore_config_store_span());
    ConfigStore.clear(protocore_config_store_span());
}
void tearDown(void)
{
}

static const char *get_str(const char *key)
{
    static char buf[64];
    ConfigStore.get_str_args.key = key;
    ConfigStore.get_str_args.out = buf;
    ConfigStore.get_str_args.out_cap = sizeof(buf);
    ConfigStore.get_str_args.def = "";
    ConfigStore.get_str(protocore_config_store_span());
    return buf;
}

// One field per line, "key=value" with a trailing newline on every line including the last, in
// schema order. The return is the character count, so it agrees with the text's own length.
void test_export_writes_one_key_value_line_per_field(void)
{
    ConfigStore.set_str_args.key = "ssid";
    ConfigStore.set_str_args.val = "myssid";
    ConfigStore.set_str(protocore_config_store_span());
    ConfigStore.set_u32_args.key = "port";
    ConfigStore.set_u32_args.val = 8080;
    ConfigStore.set_u32(protocore_config_store_span());
    ConfigStore.set_str_args.key = "name";
    ConfigStore.set_str_args.val = "node1";
    ConfigStore.set_str(protocore_config_store_span());

    char buf[256];
    ConfigIo.export_args.ns = "t";
    ConfigIo.export_args.fields = SCHEMA;
    ConfigIo.export_args.n = N;
    ConfigIo.export_args.out = buf;
    ConfigIo.export_args.cap = sizeof(buf);
    ConfigIo.export(config_io_work);
    int n = ConfigIo.n;
    TEST_ASSERT_EQUAL_STRING("ssid=myssid\nport=8080\nname=node1\n", buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), n);
}

// A field the store has never been given is exported at its type's zero: the empty string, and the
// decimal 0. The line is still emitted, so the blob always carries the whole schema.
void test_export_carries_every_field_even_when_unset(void)
{
    char buf[256];
    ConfigIo.export_args.ns = "t";
    ConfigIo.export_args.fields = SCHEMA;
    ConfigIo.export_args.n = N;
    ConfigIo.export_args.out = buf;
    ConfigIo.export_args.cap = sizeof(buf);
    ConfigIo.export(config_io_work);
    int n = ConfigIo.n;
    TEST_ASSERT_EQUAL_STRING("ssid=\nport=0\nname=\n", buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), n);
}

// Export then wipe then import must restore every field to the value it had. This is what the blob
// exists for, so it is asserted on both types and on values that are awkward to parse: the
// 32-bit maximum, and a string holding the '=' the format splits on.
void test_export_import_round_trip(void)
{
    ConfigStore.set_str_args.key = "ssid";
    ConfigStore.set_str_args.val = "abc";
    ConfigStore.set_str(protocore_config_store_span());
    ConfigStore.set_u32_args.key = "port";
    ConfigStore.set_u32_args.val = 4294967295u;
    ConfigStore.set_u32(protocore_config_store_span());
    ConfigStore.set_str_args.key = "name";
    ConfigStore.set_str_args.val = "a=b";
    ConfigStore.set_str(protocore_config_store_span());

    char blob[256];
    ConfigIo.export_args.ns = "t";
    ConfigIo.export_args.fields = SCHEMA;
    ConfigIo.export_args.n = N;
    ConfigIo.export_args.out = blob;
    ConfigIo.export_args.cap = sizeof(blob);
    ConfigIo.export(config_io_work);
    int n = ConfigIo.n;
    TEST_ASSERT_TRUE(n > 0);

    ConfigStore.clear(protocore_config_store_span());
    ConfigStore.get_u32_args.key = "port";
    ConfigStore.get_u32_args.def = 0;
    ConfigStore.get_u32(protocore_config_store_span());
    TEST_ASSERT_EQUAL_UINT32(0u, ConfigStore.ms);
    TEST_ASSERT_EQUAL_STRING("", get_str("ssid"));

    ConfigIo.import_args.ns = "t";
    ConfigIo.import_args.fields = SCHEMA;
    ConfigIo.import_args.n = N;
    ConfigIo.import_args.text = blob;
    ConfigIo.import_args.len = strlen(blob);
    ConfigIo.import(config_io_work);
    TEST_ASSERT_EQUAL_INT(3, ConfigIo.n);
    TEST_ASSERT_EQUAL_STRING("abc", get_str("ssid"));
    ConfigStore.get_u32_args.key = "port";
    ConfigStore.get_u32_args.def = 0;
    ConfigStore.get_u32(protocore_config_store_span());
    TEST_ASSERT_EQUAL_UINT32(4294967295u, ConfigStore.ms);
    TEST_ASSERT_EQUAL_STRING("a=b", get_str("name"));
}

// Importing a blob the store already holds changes nothing, so a restore is safe to repeat and the
// second export is byte-identical to the first.
void test_import_is_idempotent(void)
{
    ConfigStore.set_str_args.key = "ssid";
    ConfigStore.set_str_args.val = "abc";
    ConfigStore.set_str(protocore_config_store_span());
    ConfigStore.set_u32_args.key = "port";
    ConfigStore.set_u32_args.val = 1234;
    ConfigStore.set_u32(protocore_config_store_span());
    ConfigStore.set_str_args.key = "name";
    ConfigStore.set_str_args.val = "x";
    ConfigStore.set_str(protocore_config_store_span());

    char first[256];
    ConfigIo.export_args.ns = "t";
    ConfigIo.export_args.fields = SCHEMA;
    ConfigIo.export_args.n = N;
    ConfigIo.export_args.out = first;
    ConfigIo.export_args.cap = sizeof(first);
    ConfigIo.export(config_io_work);
    (void)ConfigIo.n;

    ConfigIo.import_args.ns = "t";
    ConfigIo.import_args.fields = SCHEMA;
    ConfigIo.import_args.n = N;
    ConfigIo.import_args.text = first;
    ConfigIo.import_args.len = strlen(first);
    ConfigIo.import(config_io_work);
    TEST_ASSERT_EQUAL_INT(3, ConfigIo.n);
    ConfigIo.import_args.ns = "t";
    ConfigIo.import_args.fields = SCHEMA;
    ConfigIo.import_args.n = N;
    ConfigIo.import_args.text = first;
    ConfigIo.import_args.len = strlen(first);
    ConfigIo.import(config_io_work);
    TEST_ASSERT_EQUAL_INT(3, ConfigIo.n);

    char second[256];
    ConfigIo.export_args.ns = "t";
    ConfigIo.export_args.fields = SCHEMA;
    ConfigIo.export_args.n = N;
    ConfigIo.export_args.out = second;
    ConfigIo.export_args.cap = sizeof(second);
    ConfigIo.export(config_io_work);
    (void)ConfigIo.n;
    TEST_ASSERT_EQUAL_STRING(first, second);
}

// The schema is the whitelist: a line whose key is not in it is skipped rather than written, so a
// blob from another device cannot introduce keys this build does not declare. The count reports
// only the fields actually written.
void test_import_writes_only_keys_the_schema_declares(void)
{
    const char *text = "port=42\nbogus=99\nssid=here\n";
    ConfigIo.import_args.ns = "t";
    ConfigIo.import_args.fields = SCHEMA;
    ConfigIo.import_args.n = N;
    ConfigIo.import_args.text = text;
    ConfigIo.import_args.len = strlen(text);
    ConfigIo.import(config_io_work);
    TEST_ASSERT_EQUAL_INT(2, ConfigIo.n);
    ConfigStore.get_u32_args.key = "port";
    ConfigStore.get_u32_args.def = 0;
    ConfigStore.get_u32(protocore_config_store_span());
    TEST_ASSERT_EQUAL_UINT32(42u, ConfigStore.ms);
    TEST_ASSERT_EQUAL_STRING("here", get_str("ssid"));
    ConfigStore.get_u32_args.key = "bogus";
    ConfigStore.get_u32_args.def = 7;
    ConfigStore.get_u32(protocore_config_store_span());
    TEST_ASSERT_EQUAL_UINT32(7u, ConfigStore.ms);
}

// A schema entry with no key is skipped rather than dereferenced, so the entries after it are still
// reachable.
void test_import_steps_over_a_keyless_schema_entry(void)
{
    static const protocore_cfg_field gapped[] = {
        {NULL, PROTOCORE_CFG_U32},
        {"zz", PROTOCORE_CFG_U32},
    };
    const char *text = "zz=7\n";
    ConfigIo.import_args.ns = "t";
    ConfigIo.import_args.fields = gapped;
    ConfigIo.import_args.n = 2;
    ConfigIo.import_args.text = text;
    ConfigIo.import_args.len = strlen(text);
    ConfigIo.import(config_io_work);
    TEST_ASSERT_EQUAL_INT(1, ConfigIo.n);
    ConfigStore.get_u32_args.key = "zz";
    ConfigStore.get_u32_args.def = 0;
    ConfigStore.get_u32(protocore_config_store_span());
    TEST_ASSERT_EQUAL_UINT32(7u, ConfigStore.ms);
}

// A field typed as neither a string nor a u32 has no setter to reach, so it is rejected rather than
// guessed at.
void test_import_rejects_a_field_of_an_unknown_type(void)
{
    static const protocore_cfg_field bad[] = {
        {"weird", (protocore_cfg_type)9},
    };
    const char *text = "weird=5\n";
    ConfigIo.import_args.ns = "t";
    ConfigIo.import_args.fields = bad;
    ConfigIo.import_args.n = 1;
    ConfigIo.import_args.text = text;
    ConfigIo.import_args.len = strlen(text);
    ConfigIo.import(config_io_work);
    TEST_ASSERT_EQUAL_INT(0, ConfigIo.n);
}

// A line with no '=' carries no value, so it is skipped and the lines around it are still read. The
// last line needs no trailing newline: the end of the text ends it.
void test_import_skips_a_line_with_no_separator(void)
{
    const char *text = "bogus\n\nport=42";
    ConfigIo.import_args.ns = "t";
    ConfigIo.import_args.fields = SCHEMA;
    ConfigIo.import_args.n = N;
    ConfigIo.import_args.text = text;
    ConfigIo.import_args.len = strlen(text);
    ConfigIo.import(config_io_work);
    TEST_ASSERT_EQUAL_INT(1, ConfigIo.n);
    ConfigStore.get_u32_args.key = "port";
    ConfigStore.get_u32_args.def = 0;
    ConfigStore.get_u32(protocore_config_store_span());
    TEST_ASSERT_EQUAL_UINT32(42u, ConfigStore.ms);
}

// The line splits on the FIRST '=', so everything after it is the value, separators and all.
void test_import_splits_on_the_first_separator(void)
{
    const char *text = "name=a=b=c\n";
    ConfigIo.import_args.ns = "t";
    ConfigIo.import_args.fields = SCHEMA;
    ConfigIo.import_args.n = N;
    ConfigIo.import_args.text = text;
    ConfigIo.import_args.len = strlen(text);
    ConfigIo.import(config_io_work);
    TEST_ASSERT_EQUAL_INT(1, ConfigIo.n);
    TEST_ASSERT_EQUAL_STRING("a=b=c", get_str("name"));
}

// The store's key limit is 15 characters plus the null, so a 16-character key cannot be stored and
// the line is dropped rather than truncated into a different key. An empty key is no key at all,
// and an over-long value is dropped rather than clipped.
void test_import_drops_a_line_past_the_store_limits(void)
{
    char text[512];
    size_t pos = 0;

    const char *empty_key = "=novalue\n";
    memcpy(text + pos, empty_key, strlen(empty_key));
    pos += strlen(empty_key);

    const char *long_key = "0123456789abcdef=5\n"; // 16 characters before the '='
    memcpy(text + pos, long_key, strlen(long_key));
    pos += strlen(long_key);

    memcpy(text + pos, "ssid=", 5);
    pos += 5;
    for (size_t i = 0; i < 128; i++) // 128 characters, one past the 127 a value may carry
    {
        text[pos++] = 'x';
    }
    text[pos++] = '\n';
    text[pos] = '\0';

    ConfigIo.import_args.ns = "t";
    ConfigIo.import_args.fields = SCHEMA;
    ConfigIo.import_args.n = N;
    ConfigIo.import_args.text = text;
    ConfigIo.import_args.len = pos;
    ConfigIo.import(config_io_work);
    TEST_ASSERT_EQUAL_INT(0, ConfigIo.n);
    TEST_ASSERT_EQUAL_STRING("", get_str("ssid"));
}

// A buffer too small for the whole schema reports 0 and leaves an empty string rather than a blob
// carrying some fields: a partial export restored elsewhere would silently drop the rest.
void test_export_fails_closed_on_a_short_buffer(void)
{
    ConfigStore.set_str_args.key = "ssid";
    ConfigStore.set_str_args.val = "value";
    ConfigStore.set_str(protocore_config_store_span());

    char buf[4];
    ConfigIo.export_args.ns = "t";
    ConfigIo.export_args.fields = SCHEMA;
    ConfigIo.export_args.n = N;
    ConfigIo.export_args.out = buf;
    ConfigIo.export_args.cap = sizeof(buf);
    ConfigIo.export(config_io_work);
    TEST_ASSERT_EQUAL_INT(0, ConfigIo.n);
    TEST_ASSERT_EQUAL_STRING("", buf);

    // One byte short of the whole blob is still a refusal, not a truncation.
    char whole[64];
    ConfigIo.export_args.ns = "t";
    ConfigIo.export_args.fields = SCHEMA;
    ConfigIo.export_args.n = N;
    ConfigIo.export_args.out = whole;
    ConfigIo.export_args.cap = sizeof(whole);
    ConfigIo.export(config_io_work);
    int n = ConfigIo.n;
    TEST_ASSERT_TRUE(n > 0);
    char tight[64];
    ConfigIo.export_args.ns = "t";
    ConfigIo.export_args.fields = SCHEMA;
    ConfigIo.export_args.n = N;
    ConfigIo.export_args.out = tight;
    ConfigIo.export_args.cap = (size_t)n;
    ConfigIo.export(config_io_work);
    TEST_ASSERT_EQUAL_INT(0, ConfigIo.n);
    TEST_ASSERT_EQUAL_STRING("", tight);
    ConfigIo.export_args.ns = "t";
    ConfigIo.export_args.fields = SCHEMA;
    ConfigIo.export_args.n = N;
    ConfigIo.export_args.out = tight;
    ConfigIo.export_args.cap = (size_t)n + 1;
    ConfigIo.export(config_io_work);
    TEST_ASSERT_EQUAL_INT(n, ConfigIo.n);
}

// A call with no destination, no schema, or no text writes nothing and reports nothing written. A
// zero capacity is refused before the buffer is touched at all.
void test_missing_arguments_are_refused(void)
{
    char out[128];
    static const protocore_cfg_field one[] = {{"ssid", PROTOCORE_CFG_STR}};

    ConfigIo.export_args.ns = "t";
    ConfigIo.export_args.fields = one;
    ConfigIo.export_args.n = 1;
    ConfigIo.export_args.out = NULL;
    ConfigIo.export_args.cap = sizeof(out);
    ConfigIo.export(config_io_work);
    TEST_ASSERT_EQUAL_INT(0, ConfigIo.n);
    ConfigIo.export_args.ns = "t";
    ConfigIo.export_args.fields = NULL;
    ConfigIo.export_args.n = 1;
    ConfigIo.export_args.out = out;
    ConfigIo.export_args.cap = sizeof(out);
    ConfigIo.export(config_io_work);
    TEST_ASSERT_EQUAL_INT(0, ConfigIo.n);
    ConfigIo.import_args.ns = "t";
    ConfigIo.import_args.fields = NULL;
    ConfigIo.import_args.n = 1;
    ConfigIo.import_args.text = "ssid=x";
    ConfigIo.import_args.len = 6;
    ConfigIo.import(config_io_work);
    TEST_ASSERT_EQUAL_INT(0, ConfigIo.n);
    ConfigIo.import_args.ns = "t";
    ConfigIo.import_args.fields = one;
    ConfigIo.import_args.n = 1;
    ConfigIo.import_args.text = NULL;
    ConfigIo.import_args.len = 0;
    ConfigIo.import(config_io_work);
    TEST_ASSERT_EQUAL_INT(0, ConfigIo.n);

    char sentinel[8] = {'z', '\0'};
    ConfigIo.export_args.ns = "t";
    ConfigIo.export_args.fields = one;
    ConfigIo.export_args.n = 1;
    ConfigIo.export_args.out = sentinel;
    ConfigIo.export_args.cap = 0;
    ConfigIo.export(config_io_work);
    TEST_ASSERT_EQUAL_INT(0, ConfigIo.n);
    TEST_ASSERT_EQUAL_CHAR('z', sentinel[0]);
}
