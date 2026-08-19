// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the zero-heap JSON writer and top-level reader
// (network_drivers/presentation/codec/json/json.h).
//
// RFC 8259 sec 7 publishes one worked example of the hardest thing a JSON string carries: the G
// clef U+1D11E written as the UTF-16 surrogate pair "\uD834\uDD1E". test_rfc8259_g_clef_surrogate
// is the load-bearing case - a reader that combines the pair and re-encodes it as UTF-8 gets the
// four octets RFC 3629 defines for that code point, and one that treats each half as a code point
// of its own does not. The writer's anchor is the object printed verbatim in RFC 8259 sec 13.

#include "network_drivers/presentation/codec/json/json.h"
#include <string.h>

#include <unity.h>

static uint8_t json_work[16]; // the borrow an entry takes; Json never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static char g_buf[512];
static protocore_json_writer g_w;

// Bind the writer to the shared buffer and return it, so each case reads as one build.
static protocore_json_writer *wr(void)
{
    Json.init_args.w = &g_w;
    Json.init_args.buf = g_buf;
    Json.init_args.cap = sizeof(g_buf);
    Json.init(json_work);
    return &g_w;
}

// RFC 8259 sec 13 prints this object verbatim. The whitespace between tokens is insignificant
// (sec 2), so the writer's output is the same member order and the same values with none of it.
static const char *const RFC8259_DOC = "{\n"
                                       "  \"Image\": {\n"
                                       "      \"Width\":  800,\n"
                                       "      \"Height\": 600,\n"
                                       "      \"Title\":  \"View from 15th Floor\",\n"
                                       "      \"Thumbnail\": {\n"
                                       "          \"Url\":    \"http://www.example.com/image/481989943\",\n"
                                       "          \"Height\": 125,\n"
                                       "          \"Width\":  100\n"
                                       "      },\n"
                                       "      \"Animated\" : false,\n"
                                       "      \"IDs\": [116, 943, 234, 38793]\n"
                                       "    }\n"
                                       "}";

// The same document, built through the writer.
void test_rfc8259_section_13_example_document(void)
{
    protocore_json_writer *w = wr();
    Json.begin_object_args.w = w;
    Json.begin_object(json_work);
    Json.key_args.w = w;
    Json.key_args.k = "Image";
    Json.key(json_work);
    Json.begin_object_args.w = w;
    Json.begin_object(json_work);
    Json.kv_int_args.w = w;
    Json.kv_int_args.k = "Width";
    Json.kv_int_args.v = 800;
    Json.kv_int(json_work);
    Json.kv_int_args.w = w;
    Json.kv_int_args.k = "Height";
    Json.kv_int_args.v = 600;
    Json.kv_int(json_work);
    Json.kv_str_args.w = w;
    Json.kv_str_args.k = "Title";
    Json.kv_str_args.v = "View from 15th Floor";
    Json.kv_str(json_work);
    Json.key_args.w = w;
    Json.key_args.k = "Thumbnail";
    Json.key(json_work);
    Json.begin_object_args.w = w;
    Json.begin_object(json_work);
    Json.kv_str_args.w = w;
    Json.kv_str_args.k = "Url";
    Json.kv_str_args.v = "http://www.example.com/image/481989943";
    Json.kv_str(json_work);
    Json.kv_int_args.w = w;
    Json.kv_int_args.k = "Height";
    Json.kv_int_args.v = 125;
    Json.kv_int(json_work);
    Json.kv_int_args.w = w;
    Json.kv_int_args.k = "Width";
    Json.kv_int_args.v = 100;
    Json.kv_int(json_work);
    Json.end_object_args.w = w;
    Json.end_object(json_work);
    Json.kv_bool_args.w = w;
    Json.kv_bool_args.k = "Animated";
    Json.kv_bool_args.v = PROTO_FALSE;
    Json.kv_bool(json_work);
    Json.key_args.w = w;
    Json.key_args.k = "IDs";
    Json.key(json_work);
    Json.begin_array_args.w = w;
    Json.begin_array(json_work);
    Json.put_int_args.w = w;
    Json.put_int_args.v = 116;
    Json.put_int(json_work);
    Json.put_int_args.w = w;
    Json.put_int_args.v = 943;
    Json.put_int(json_work);
    Json.put_int_args.w = w;
    Json.put_int_args.v = 234;
    Json.put_int(json_work);
    Json.put_int_args.w = w;
    Json.put_int_args.v = 38793;
    Json.put_int(json_work);
    Json.end_array_args.w = w;
    Json.end_array(json_work);
    Json.end_object_args.w = w;
    Json.end_object(json_work);
    Json.end_object_args.w = w;
    Json.end_object(json_work);

    TEST_ASSERT_TRUE(protocore_json_ok(w));
    TEST_ASSERT_EQUAL_STRING("{\"Image\":{\"Width\":800,\"Height\":600,\"Title\":\"View from 15th Floor\","
                             "\"Thumbnail\":{\"Url\":\"http://www.example.com/image/481989943\","
                             "\"Height\":125,\"Width\":100},\"Animated\":false,"
                             "\"IDs\":[116,943,234,38793]}}",
                             protocore_json_c_str(w));
    TEST_ASSERT_EQUAL_UINT(strlen(protocore_json_c_str(w)), protocore_json_length(w));
}

// RFC 8259 sec 7: quotation mark, reverse solidus and U+0000..U+001F MUST be escaped. The grammar
// gives two-character forms for %x22 %x5C %x62 %x66 %x6E %x72 %x74 and \uXXXX for the rest; solidus
// is in `unescaped` (%x2F is inside %x23-5B) so it travels as itself.
void test_rfc8259_mandatory_escapes(void)
{
    static const char V[] = {'q', '"', '\\', 0x01, 0x1f, '\b', '\f', '\n', '\r', '\t', '/', '\0'};
    protocore_json_writer *w = wr();
    Json.begin_object_args.w = w;
    Json.begin_object(json_work);
    Json.kv_str_args.w = w;
    Json.kv_str_args.k = "k";
    Json.kv_str_args.v = V;
    Json.kv_str(json_work);
    Json.end_object_args.w = w;
    Json.end_object(json_work);
    TEST_ASSERT_TRUE(protocore_json_ok(w));
    TEST_ASSERT_EQUAL_STRING("{\"k\":\"q\\\"\\\\\\u0001\\u001f\\b\\f\\n\\r\\t/\"}", protocore_json_c_str(w));
}

// A member name is a string too, so the same escaping applies to it.
void test_member_name_is_escaped(void)
{
    protocore_json_writer *w = wr();
    Json.begin_object_args.w = w;
    Json.begin_object(json_work);
    Json.kv_int_args.w = w;
    Json.kv_int_args.k = "a\"b";
    Json.kv_int_args.v = 1;
    Json.kv_int(json_work);
    Json.end_object_args.w = w;
    Json.end_object(json_work);
    TEST_ASSERT_EQUAL_STRING("{\"a\\\"b\":1}", protocore_json_c_str(w));
}

// RFC 8259 sec 3: the three literal names are false / null / true, lowercase, and nothing else.
void test_rfc8259_literal_names(void)
{
    protocore_json_writer *w = wr();
    Json.begin_array_args.w = w;
    Json.begin_array(json_work);
    Json.put_bool_args.w = w;
    Json.put_bool_args.v = PROTO_FALSE;
    Json.put_bool(json_work);
    Json.put_null_args.w = w;
    Json.put_null(json_work);
    Json.put_bool_args.w = w;
    Json.put_bool_args.v = PROTO_TRUE;
    Json.put_bool(json_work);
    Json.end_array_args.w = w;
    Json.end_array(json_work);
    TEST_ASSERT_EQUAL_STRING("[false,null,true]", protocore_json_c_str(w));
}

// Read one string member into @p out; returns what the reader reported.
static proto_bool get_str(const char *doc, const char *key, char *out, size_t cap)
{
    Json.get_str_args.json = doc;
    Json.get_str_args.key = key;
    Json.get_str_args.out = out;
    Json.get_str_args.out_cap = cap;
    Json.get_str(json_work);
    return Json.ok;
}

// RFC 8259 sec 7: "a string containing only the G clef character (U+1D11E) may be represented as
// "\uD834\uDD1E"".
//
// Its UTF-8 form, from RFC 3629's four-byte pattern 11110uuu 10uuzzzz 10yyyyyy 10xxxxxx applied to
// U+1D11E = 0b1_1101_0001_0001_1110:
//   cp >> 18        = 0x00 -> 0xF0 | 0x00 = 0xF0
//   (cp >> 12) & 3F = 0x1D -> 0x80 | 0x1D = 0x9D
//   (cp >>  6) & 3F = 0x04 -> 0x80 | 0x04 = 0x84
//    cp        & 3F = 0x1E -> 0x80 | 0x1E = 0x9E
void test_rfc8259_g_clef_surrogate(void)
{
    static const unsigned char WANT[4] = {0xF0, 0x9D, 0x84, 0x9E};
    char out[16];
    TEST_ASSERT_TRUE(get_str("{\"clef\":\"\\uD834\\uDD1E\"}", "clef", out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(4u, strlen(out));
    TEST_ASSERT_EQUAL_MEMORY(WANT, out, 4);
}

// The three shorter UTF-8 widths, each derived from RFC 3629's pattern the same way.
//   U+005C  -> 0x5C                      (RFC 8259 sec 7's own single-reverse-solidus example)
//   U+00E9  -> C0|(E9>>6)=C3, 80|(E9&3F)=A9
//   U+20AC  -> E0|(20AC>>12)=E2, 80|((20AC>>6)&3F)=82, 80|(20AC&3F)=AC
void test_rfc3629_escape_widths(void)
{
    char out[16];
    TEST_ASSERT_TRUE(get_str("{\"k\":\"\\u005C\"}", "k", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("\\", out);

    TEST_ASSERT_TRUE(get_str("{\"k\":\"\\u00e9\"}", "k", out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(2u, strlen(out));
    TEST_ASSERT_EQUAL_MEMORY("\xC3\xA9", out, 2);

    TEST_ASSERT_TRUE(get_str("{\"k\":\"\\u20AC\"}", "k", out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(3u, strlen(out));
    TEST_ASSERT_EQUAL_MEMORY("\xE2\x82\xAC", out, 3);
}

// RFC 8259 sec 8.2 names "\uDEAD" as a single unpaired UTF-16 surrogate that the grammar admits but
// that encodes no character. It is replaced with U+FFFD, whose UTF-8 form is
//   E0|(FFFD>>12)=EF, 80|((FFFD>>6)&3F)=BF, 80|(FFFD&3F)=BD.
void test_rfc8259_unpaired_surrogate(void)
{
    char out[16];
    TEST_ASSERT_TRUE(get_str("{\"k\":\"\\uDEAD\"}", "k", out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(3u, strlen(out));
    TEST_ASSERT_EQUAL_MEMORY("\xEF\xBF\xBD", out, 3);

    // a high surrogate whose partner is not a low surrogate is unpaired too
    TEST_ASSERT_TRUE(get_str("{\"k\":\"\\uD834\\u0041\"}", "k", out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(4u, strlen(out));
    TEST_ASSERT_EQUAL_MEMORY("\xEF\xBF\xBD"
                             "A",
                             out, 4);
}

// The two-character escapes of RFC 8259 sec 7's grammar, each decoding to the code point the
// grammar's own comment names.
void test_rfc8259_two_character_escapes(void)
{
    static const char WANT[] = {'"', '\\', '/', '\b', '\f', '\n', '\r', '\t', '\0'};
    char out[16];
    TEST_ASSERT_TRUE(get_str("{\"k\":\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"}", "k", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING(WANT, out);
}

// The reader walks members of the top-level object only. In the sec 13 document the sole top-level
// member is Image, so Width - which appears twice, nested - is not found, and Image itself is an
// object rather than a string.
void test_reader_matches_only_top_level_members(void)
{
    char out[64];
    long n = 12345;
    proto_bool b = PROTO_TRUE;

    Json.get_int_args.json = RFC8259_DOC;
    Json.get_int_args.key = "Width";
    Json.get_int_args.out = &n;
    Json.get_int(json_work);
    TEST_ASSERT_FALSE(Json.ok);
    TEST_ASSERT_EQUAL_INT(12345, n); // untouched
    Json.get_str_args.json = RFC8259_DOC;
    Json.get_str_args.key = "Url";
    Json.get_str_args.out = out;
    Json.get_str_args.out_cap = sizeof(out);
    Json.get_str(json_work);
    TEST_ASSERT_FALSE(Json.ok);
    Json.get_bool_args.json = RFC8259_DOC;
    Json.get_bool_args.key = "Animated";
    Json.get_bool_args.out = &b;
    Json.get_bool(json_work);
    TEST_ASSERT_FALSE(Json.ok);
    TEST_ASSERT_TRUE(b);
    Json.get_str_args.json = RFC8259_DOC;
    Json.get_str_args.key = "Image";
    Json.get_str_args.out = out;
    Json.get_str_args.out_cap = sizeof(out);
    Json.get_str(json_work);
    TEST_ASSERT_FALSE(Json.ok);
}

// Whitespace between tokens carries no meaning (RFC 8259 sec 2), so a member found in the RFC's own
// laid-out text is found in the compact form too.
void test_reader_skips_insignificant_whitespace(void)
{
    char out[32];
    long n = 0;
    proto_bool b = PROTO_FALSE;
    static const char *const SPACED = "  {  \"s\" :  \"v\" ,  \"n\" : -7 ,  \"b\" : true  }  ";
    Json.get_str_args.json = SPACED;
    Json.get_str_args.key = "s";
    Json.get_str_args.out = out;
    Json.get_str_args.out_cap = sizeof(out);
    Json.get_str(json_work);
    TEST_ASSERT_TRUE(Json.ok);
    TEST_ASSERT_EQUAL_STRING("v", out);
    Json.get_int_args.json = SPACED;
    Json.get_int_args.key = "n";
    Json.get_int_args.out = &n;
    Json.get_int(json_work);
    TEST_ASSERT_TRUE(Json.ok);
    TEST_ASSERT_EQUAL_INT(-7, n);
    Json.get_bool_args.json = SPACED;
    Json.get_bool_args.key = "b";
    Json.get_bool_args.out = &b;
    Json.get_bool(json_work);
    TEST_ASSERT_TRUE(Json.ok);
    TEST_ASSERT_TRUE(b);
}

// What the writer emits, the reader reads back unchanged: the identity that makes the pair usable.
void test_write_read_round_trip(void)
{
    protocore_json_writer *w = wr();
    Json.begin_object_args.w = w;
    Json.begin_object(json_work);
    Json.kv_str_args.w = w;
    Json.kv_str_args.k = "s";
    Json.kv_str_args.v = "a\"b\\c\nd";
    Json.kv_str(json_work);
    Json.kv_int_args.w = w;
    Json.kv_int_args.k = "n";
    Json.kv_int_args.v = -2147483647L;
    Json.kv_int(json_work);
    Json.kv_uint_args.w = w;
    Json.kv_uint_args.k = "u";
    Json.kv_uint_args.v = 65535UL;
    Json.kv_uint(json_work);
    Json.kv_bool_args.w = w;
    Json.kv_bool_args.k = "b";
    Json.kv_bool_args.v = PROTO_FALSE;
    Json.kv_bool(json_work);
    Json.end_object_args.w = w;
    Json.end_object(json_work);
    TEST_ASSERT_TRUE(protocore_json_ok(w));

    char out[32];
    long n = 0;
    proto_bool b = PROTO_TRUE;
    const char *doc = protocore_json_c_str(w);
    Json.get_str_args.json = doc;
    Json.get_str_args.key = "s";
    Json.get_str_args.out = out;
    Json.get_str_args.out_cap = sizeof(out);
    Json.get_str(json_work);
    TEST_ASSERT_TRUE(Json.ok);
    TEST_ASSERT_EQUAL_STRING("a\"b\\c\nd", out);
    Json.get_int_args.json = doc;
    Json.get_int_args.key = "n";
    Json.get_int_args.out = &n;
    Json.get_int(json_work);
    TEST_ASSERT_TRUE(Json.ok);
    TEST_ASSERT_EQUAL_INT(-2147483647L, n);
    Json.get_int_args.json = doc;
    Json.get_int_args.key = "u";
    Json.get_int_args.out = &n;
    Json.get_int(json_work);
    TEST_ASSERT_TRUE(Json.ok);
    TEST_ASSERT_EQUAL_INT(65535L, n);
    Json.get_bool_args.json = doc;
    Json.get_bool_args.key = "b";
    Json.get_bool_args.out = &b;
    Json.get_bool(json_work);
    TEST_ASSERT_TRUE(Json.ok);
    TEST_ASSERT_FALSE(b);
}

// A value of the wrong shape is refused rather than coerced: a quoted number is not an integer and a
// quoted "true" is not a boolean.
void test_reader_refuses_a_mismatched_type(void)
{
    long n = 0;
    proto_bool b = PROTO_FALSE;
    Json.get_int_args.json = "{\"n\":\"42\"}";
    Json.get_int_args.key = "n";
    Json.get_int_args.out = &n;
    Json.get_int(json_work);
    TEST_ASSERT_FALSE(Json.ok);
    Json.get_int_args.json = "{\"n\":abc}";
    Json.get_int_args.key = "n";
    Json.get_int_args.out = &n;
    Json.get_int(json_work);
    TEST_ASSERT_FALSE(Json.ok);
    Json.get_bool_args.json = "{\"b\":\"true\"}";
    Json.get_bool_args.key = "b";
    Json.get_bool_args.out = &b;
    Json.get_bool(json_work);
    TEST_ASSERT_FALSE(Json.ok);
    Json.get_bool_args.json = "{\"b\":1}";
    Json.get_bool_args.key = "b";
    Json.get_bool_args.out = &b;
    Json.get_bool(json_work);
    TEST_ASSERT_FALSE(Json.ok);
    Json.get_bool_args.json = "{\"b\":truthy}";
    Json.get_bool_args.key = "b";
    Json.get_bool_args.out = &b;
    Json.get_bool(json_work);
    TEST_ASSERT_FALSE(Json.ok);
}

// A missing member, a non-object body and a null argument are all reported, never guessed at.
void test_reader_guards(void)
{
    char out[8];
    long n = 0;
    proto_bool b = PROTO_FALSE;
    Json.get_str_args.json = "{\"a\":\"x\"}";
    Json.get_str_args.key = "b";
    Json.get_str_args.out = out;
    Json.get_str_args.out_cap = sizeof(out);
    Json.get_str(json_work);
    TEST_ASSERT_FALSE(Json.ok);
    Json.get_str_args.json = "[1,2]";
    Json.get_str_args.key = "a";
    Json.get_str_args.out = out;
    Json.get_str_args.out_cap = sizeof(out);
    Json.get_str(json_work);
    TEST_ASSERT_FALSE(Json.ok);
    Json.get_str_args.json = NULL;
    Json.get_str_args.key = "a";
    Json.get_str_args.out = out;
    Json.get_str_args.out_cap = sizeof(out);
    Json.get_str(json_work);
    TEST_ASSERT_FALSE(Json.ok);
    Json.get_str_args.json = "{\"a\":\"x\"}";
    Json.get_str_args.key = NULL;
    Json.get_str_args.out = out;
    Json.get_str_args.out_cap = sizeof(out);
    Json.get_str(json_work);
    TEST_ASSERT_FALSE(Json.ok);
    Json.get_str_args.json = "{\"a\":\"x\"}";
    Json.get_str_args.key = "a";
    Json.get_str_args.out = NULL;
    Json.get_str_args.out_cap = sizeof(out);
    Json.get_str(json_work);
    TEST_ASSERT_FALSE(Json.ok);
    Json.get_str_args.json = "{\"a\":\"x\"}";
    Json.get_str_args.key = "a";
    Json.get_str_args.out = out;
    Json.get_str_args.out_cap = 0;
    Json.get_str(json_work);
    TEST_ASSERT_FALSE(Json.ok);
    Json.get_int_args.json = "{\"a\":1}";
    Json.get_int_args.key = "a";
    Json.get_int_args.out = NULL;
    Json.get_int(json_work);
    TEST_ASSERT_FALSE(Json.ok);
    Json.get_bool_args.json = "{\"a\":true}";
    Json.get_bool_args.key = "a";
    Json.get_bool_args.out = NULL;
    Json.get_bool(json_work);
    TEST_ASSERT_FALSE(Json.ok);
    Json.get_str_args.json = "{\"a\" \"x\"}";
    Json.get_str_args.key = "a";
    Json.get_str_args.out = out;
    Json.get_str_args.out_cap = sizeof(out);
    Json.get_str(json_work);
    TEST_ASSERT_FALSE(Json.ok); // no ':'
    Json.get_int_args.json = "{}";
    Json.get_int_args.key = "a";
    Json.get_int_args.out = &n;
    Json.get_int(json_work);
    TEST_ASSERT_FALSE(Json.ok);
    Json.get_bool_args.json = "";
    Json.get_bool_args.key = "a";
    Json.get_bool_args.out = &b;
    Json.get_bool(json_work);
    TEST_ASSERT_FALSE(Json.ok);
}

// A destination shorter than the value truncates to capacity and still terminates, rather than
// running past the buffer.
void test_reader_truncates_to_capacity(void)
{
    char out[4];
    TEST_ASSERT_TRUE(get_str("{\"k\":\"abcdefgh\"}", "k", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("abc", out);

    // a multi-byte escape is emitted whole or not at all: two bytes do not fit in the one left here
    char tight[3];
    TEST_ASSERT_TRUE(get_str("{\"k\":\"a\\u00e9\"}", "k", tight, sizeof(tight)));
    TEST_ASSERT_EQUAL_STRING("a", tight);
}

// An output buffer that runs out latches ok false and leaves a terminated prefix behind, so a
// partial document is never mistaken for a whole one.
void test_writer_overflow_latches_and_terminates(void)
{
    char small[8];
    protocore_json_writer w;
    Json.init_args.w = &w;
    Json.init_args.buf = small;
    Json.init_args.cap = sizeof(small);
    Json.init(json_work);
    Json.begin_object_args.w = &w;
    Json.begin_object(json_work);
    Json.kv_str_args.w = &w;
    Json.kv_str_args.k = "key";
    Json.kv_str_args.v = "a long value that does not fit";
    Json.kv_str(json_work);
    Json.end_object_args.w = &w;
    Json.end_object(json_work);
    TEST_ASSERT_FALSE(protocore_json_ok(&w));
    TEST_ASSERT_EQUAL_UINT(sizeof(small) - 1, strlen(small));
    TEST_ASSERT_TRUE(protocore_json_length(&w) < sizeof(small));
}

// Nesting past JSON_MAX_DEPTH is a structural error, not a silent flattening.
void test_writer_depth_limit(void)
{
    protocore_json_writer *w = wr();
    for (int i = 0; i < JSON_MAX_DEPTH; i++)
    {
        Json.begin_array_args.w = w;
        Json.begin_array(json_work);
    }
    TEST_ASSERT_TRUE(protocore_json_ok(w));
    Json.begin_array_args.w = w;
    Json.begin_array(json_work);
    TEST_ASSERT_FALSE(protocore_json_ok(w));
}

// Closing a container that was never opened is the same structural error.
void test_writer_unbalanced_close(void)
{
    protocore_json_writer *w = wr();
    Json.begin_object_args.w = w;
    Json.begin_object(json_work);
    Json.end_object_args.w = w;
    Json.end_object(json_work);
    TEST_ASSERT_TRUE(protocore_json_ok(w));
    Json.end_object_args.w = w;
    Json.end_object(json_work);
    TEST_ASSERT_FALSE(protocore_json_ok(w));
}

// No storage means the writer never claims to be ok, so a caller that checks the flag writes nothing.
void test_writer_without_storage(void)
{
    protocore_json_writer w;
    Json.init_args.w = &w;
    Json.init_args.buf = NULL;
    Json.init_args.cap = 64;
    Json.init(json_work);
    TEST_ASSERT_FALSE(protocore_json_ok(&w));
    Json.begin_object_args.w = &w;
    Json.begin_object(json_work);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_json_length(&w));

    char one[1];
    Json.init_args.w = &w;
    Json.init_args.buf = one;
    Json.init_args.cap = 0;
    Json.init(json_work);
    TEST_ASSERT_FALSE(protocore_json_ok(&w));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_json_length(&w));
}

// put_raw copies a pre-formatted literal through untouched, which is how a number the writer has no
// form for reaches the document.
void test_writer_raw_literal(void)
{
    protocore_json_writer *w = wr();
    Json.begin_object_args.w = w;
    Json.begin_object(json_work);
    Json.kv_raw_args.w = w;
    Json.kv_raw_args.k = "f";
    Json.kv_raw_args.literal = "1.25e-3";
    Json.kv_raw(json_work);
    Json.key_args.w = w;
    Json.key_args.k = "arr";
    Json.key(json_work);
    Json.put_raw_args.w = w;
    Json.put_raw_args.literal = "[1,2]";
    Json.put_raw(json_work);
    Json.kv_null_args.w = w;
    Json.kv_null_args.k = "z";
    Json.kv_null(json_work);
    Json.end_object_args.w = w;
    Json.end_object(json_work);
    TEST_ASSERT_TRUE(protocore_json_ok(w));
    TEST_ASSERT_EQUAL_STRING("{\"f\":1.25e-3,\"arr\":[1,2],\"z\":null}", protocore_json_c_str(w));
}
