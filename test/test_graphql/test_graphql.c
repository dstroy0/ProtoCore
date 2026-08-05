// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the GraphQL query subset (services/iot/graphql): selection shaping,
// nesting, arguments collected along the path, scalar types + JSON escaping, the
// `query`/anonymous forms, and the fail-closed paths (parse error, unsupported
// operation, depth/overflow limits).

#include "services/iot/graphql/graphql.h"
#include <stdio.h>

#include <unity.h>

// Demo schema: a flat handful of fields + a nested `device` object and an
// argument-driven `sensor`/`greet`.
static proto_bool resolver(const char *path, const struct pc_gql_args *args, pc_gql_value *out)
{
    if (!strcmp(path, "name"))
    {
        out->type = PC_GQL_STR;
        out->s = "esp32";
        return PROTO_TRUE;
    }
    if (!strcmp(path, "uptime"))
    {
        out->type = PC_GQL_INT;
        out->i = 12345;
        return PROTO_TRUE;
    }
    if (!strcmp(path, "temp"))
    {
        out->type = PC_GQL_FLOAT;
        out->f = 21.5;
        return PROTO_TRUE;
    }
    if (!strcmp(path, "online"))
    {
        out->type = PC_GQL_BOOL;
        out->b = PROTO_TRUE;
        return PROTO_TRUE;
    }
    if (!strcmp(path, "device.name"))
    {
        out->type = PC_GQL_STR;
        out->s = "dev1";
        return PROTO_TRUE;
    }
    if (!strcmp(path, "device.uptime"))
    {
        out->type = PC_GQL_INT;
        out->i = 99;
        return PROTO_TRUE;
    }
    if (!strcmp(path, "sensor.value"))
    {
        long long id = 0;
        out->type = PC_GQL_INT;
        out->i = pc_gql_arg_int(args, "id", &id) ? id * 10 : -1;
        return PROTO_TRUE;
    }
    if (!strcmp(path, "greet"))
    {
        const char *who = "?";
        pc_gql_arg_str(args, "name", &who);
        static char b[64];
        snprintf(b, sizeof(b), "hi %s", who);
        out->type = PC_GQL_STR;
        out->s = b;
        return PROTO_TRUE;
    }
    if (!strcmp(path, "flag")) // reads a bool argument
    {
        proto_bool on = PROTO_FALSE;
        pc_gql_arg_bool(args, "on", &on);
        out->type = PC_GQL_BOOL;
        out->b = on;
        return PROTO_TRUE;
    }
    if (!strcmp(path, "ctrl")) // a string holding a control char (forces \uXXXX)
    {
        static const char c[] = {'a', 0x01, 'b', '\0'};
        out->type = PC_GQL_STR;
        out->s = c;
        return PROTO_TRUE;
    }
    if (!strcmp(path, "nullval")) // resolves true with a null-typed value (w_scalar default)
    {
        out->type = PC_GQL_NULL;
        return PROTO_TRUE;
    }
    if (!strcmp(path, "nullstr")) // reports a string value with a null pointer (w_scalar's ?: guard)
    {
        out->type = PC_GQL_STR;
        out->s = NULL;
        return PROTO_TRUE;
    }
    return PROTO_FALSE; // -> null
}

static char out[1024];

static const char *run(const char *q)
{
    pc_gql_result rc = pc_graphql_execute(q, strlen(q), resolver, out, sizeof(out));
    (void)rc;
    return out;
}

static pc_gql_result run_rc(const char *q)
{
    return pc_graphql_execute(q, strlen(q), resolver, out, sizeof(out));
}

void setUp()
{
    memset(out, 0, sizeof(out));
}
void tearDown()
{
}

void test_flat_selection()
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"name\":\"esp32\",\"uptime\":12345}}", run("{ name uptime }"));
}

void test_selection_is_honored()
{
    // Only the requested field appears.
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"name\":\"esp32\"}}", run("{ name }"));
}

void test_nested_object()
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"device\":{\"name\":\"dev1\",\"uptime\":99}}}",
                             run("{ device { name uptime } }"));
}

void test_args_collected_along_path()
{
    // `id` is on the object `sensor`; the leaf resolver `sensor.value` reads it.
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"sensor\":{\"value\":70}}}", run("{ sensor(id: 7) { value } }"));
}

void test_scalar_types()
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"temp\":21.5,\"online\":true}}", run("{ temp online }"));
}

void test_string_arg_and_escaping()
{
    // String arg is decoded, and the resolver's output string is JSON-escaped.
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"greet\":\"hi a\\\"b\"}}", run("{ greet(name: \"a\\\"b\") }"));
}

void test_unresolved_field_is_null()
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"mystery\":null}}", run("{ mystery }"));
}

void test_query_keyword_and_name()
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"online\":true}}", run("query Status { online }"));
}

void test_comments_and_commas()
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"name\":\"esp32\",\"uptime\":12345}}", run("{ name, # a comment\n uptime }"));
}

void test_parse_error_reports_errors()
{
    pc_gql_result rc = pc_graphql_execute("{ name ", 7, resolver, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, rc);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"errors\""));
}

void test_mutation_rejected()
{
    pc_gql_result rc = pc_graphql_execute("mutation { x }", 14, resolver, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, rc);
}

void test_depth_limit()
{
    pc_gql_result rc = pc_graphql_execute("{a{b{c{d{e{f{g}}}}}}}", 21, resolver, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_LIMIT, rc);
}

void test_overflow_fails_closed()
{
    char tiny[8];
    pc_gql_result rc = pc_graphql_execute("{ name uptime }", 15, resolver, tiny, sizeof(tiny));
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_OVERFLOW, rc);
}

void test_string_escapes_decoded()
{
    // \n \t \r \\ \/ and an unknown escape (\z) are all decoded by the arg lexer.
    TEST_ASSERT_EQUAL_INT(PC_GQL_OK, run_rc("{ greet(name: \"a\\nb\\tc\\rd\\\\e\\/f\\z\") }"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"greet\":\"hi a"));
}

void test_number_arg_variants_parse()
{
    // float, exponent, signed-exponent and negative-int argument values all parse
    // (the resolver only reads `id`, but every number branch is exercised).
    TEST_ASSERT_EQUAL_INT(PC_GQL_OK, run_rc("{ sensor(id: 7, x: 1.5, y: 2e3, z: -4, w: 1.2e-2) { value } }"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"value\":70"));
}

void test_bool_args()
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"flag\":true}}", run("{ flag(on: true) }"));
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"flag\":false}}", run("{ flag(on: false) }"));
}

void test_null_arg_value()
{
    // `null` parses; greet's name arg is then not a string, so it stays "?".
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"greet\":\"hi ?\"}}", run("{ greet(name: null) }"));
}

void test_control_char_is_unicode_escaped()
{
    TEST_ASSERT_NOT_NULL(strstr(run("{ ctrl }"), "\\u0001"));
}

void test_unterminated_string_arg_fails()
{
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("{ greet(name: \"oops) }"));
}

void test_arg_missing_colon_fails()
{
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("{ sensor(id 7) { value } }"));
}

void test_bad_arg_value_fails()
{
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("{ greet(name: @) }"));
}

void test_trailing_junk_fails()
{
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("{ name } junk"));
}

void test_long_field_name_hits_limit()
{
    char q[256];
    int n = 0;
    q[n++] = '{';
    q[n++] = ' ';
    for (int i = 0; i < 200; i++)
    {
        q[n++] = 'a';
    }
    q[n++] = ' ';
    q[n++] = '}';
    q[n] = '\0';
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_LIMIT, pc_graphql_execute(q, strlen(q), resolver, out, sizeof(out)));
}

void test_null_inputs_fail_closed()
{
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, pc_graphql_execute(0, 0, resolver, out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, pc_graphql_execute("{ name }", 8, resolver, 0, 0));
}

void test_unknown_operation_keyword_fails()
{
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("subscription { name }"));
}

// A numeric arg with no digits (a bare '-'), an arg with no name, and a field name
// that does not start with a name character are all parse errors.
void test_malformed_tokens_fail()
{
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("{ f(a: -) }"));
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("{ f(: 1) }"));
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("{ 1 }"));
}

// `query Op` with no selection set, and `query` followed by a non-name, are parse errors.
void test_query_keyword_forms_fail()
{
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("query Op"));  // no selection set follows
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("query 123")); // bad operation name
}

// Exceeding the argument, node and string-buffer pools each fails closed with a limit error.
void test_pool_limits()
{
    char q[600];
    int n = 0;
    n += snprintf(q + n, sizeof(q) - n, "{ f(");
    for (int i = 0; i < 25; i++) // > PC_GQL_MAX_ARGS (24)
    {
        n += snprintf(q + n, sizeof(q) - n, "a%d:1 ", i);
    }
    n += snprintf(q + n, sizeof(q) - n, ") }");
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_LIMIT, run_rc(q));

    n = 0;
    n += snprintf(q + n, sizeof(q) - n, "{ ");
    for (int i = 0; i < 49; i++) // > PC_GQL_MAX_NODES (48)
    {
        n += snprintf(q + n, sizeof(q) - n, "f%d ", i);
    }
    n += snprintf(q + n, sizeof(q) - n, "}");
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_LIMIT, run_rc(q));

    // One string argument longer than the decode buffer (256).
    n = 0;
    n += snprintf(q + n, sizeof(q) - n, "{ f(a:\"");
    for (int i = 0; i < 300; i++)
    {
        q[n++] = 'x';
    }
    q[n++] = '"';
    q[n++] = ')';
    q[n++] = '}';
    q[n] = '\0';
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_LIMIT, run_rc(q));
}

// Many string arguments together exhaust the intern pool (a value fails to intern).
void test_string_pool_exhaustion()
{
    char q[600];
    int n = 0;
    n += snprintf(q + n, sizeof(q) - n, "{ f(");
    for (int i = 0; i < 8; i++) // 8 x 40 bytes = 320 > PC_GQL_STRBUF (256)
    {
        n += snprintf(q + n, sizeof(q) - n, "a%d:\"", i);
        for (int k = 0; k < 40; k++)
        {
            q[n++] = 'y';
        }
        q[n++] = '"';
        q[n++] = ' ';
    }
    n += snprintf(q + n, sizeof(q) - n, ") }");
    q[n] = '\0';
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_LIMIT, run_rc(q));
}

// A resolver returning a null-typed value serializes as JSON null (w_scalar default).
void test_resolver_null_typed_value()
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"nullval\":null}}", run("{ nullval }"));
}

// Build a nested query { a...a { a...a { ... } } } from per-level name lengths.
static void build_nested_query(const int *lens, int levels, char *q)
{
    int p = 0;
    for (int i = 0; i < levels; i++)
    {
        q[p++] = '{';
        q[p++] = ' ';
        for (int k = 0; k < lens[i]; k++)
        {
            q[p++] = 'a';
        }
        q[p++] = ' ';
    }
    for (int i = 0; i < levels; i++)
    {
        q[p++] = '}';
        q[p++] = ' ';
    }
    q[p] = '\0';
}

// A resolver path longer than PC_GQL_PATH_MAX (96) fails closed as an overflow, both
// when the separator and when the segment would spill (names stay within NAME_MAX = 32).
void test_resolver_path_overflow()
{
    char q[256];
    // 31,31,31,31: the 4th separator check trips (plen reaches 95, then '.' -> 96).
    const int dot_case[4] = {31, 31, 31, 31};
    build_nested_query(dot_case, 4, q);
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_OVERFLOW, run_rc(q));

    // 31,31,6,31: the 4th segment copy trips (plen 71 + a 31-char name >= 96).
    const int seg_case[4] = {31, 31, 6, 31};
    build_nested_query(seg_case, 4, q);
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_OVERFLOW, run_rc(q));
}

// The argument accessors reject null args and a wrong-typed / missing argument.
void test_arg_accessors_edges()
{
    long long i = 0;
    const char *s = NULL;
    proto_bool b = PROTO_FALSE;
    TEST_ASSERT_FALSE(pc_gql_arg_int(NULL, "x", &i));
    TEST_ASSERT_FALSE(pc_gql_arg_str(NULL, "x", &s));
    TEST_ASSERT_FALSE(pc_gql_arg_bool(NULL, "x", &b));

    // id is a string, so arg_int does not match it -> resolver returns -1.
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"sensor\":{\"value\":-1}}}", run("{ sensor(id: \"x\") { value } }"));
    // on is an int, so arg_bool does not match it -> false.
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"flag\":false}}", run("{ flag(on: 1) }"));
    // no on argument at all -> arg_bool exhausts and returns false.
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"flag\":false}}", run("{ flag }"));
}

// Tab and carriage return are lexer whitespace, and a '#' comment may run to the end of the input
// with no closing newline: all three forms parse as the plain-spaced query does.
void test_lexer_whitespace_and_eof_comment()
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"name\":\"esp32\",\"uptime\":12345}}", run("{\tname\r\n\tuptime\r}"));
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"name\":\"esp32\"}}", run("{ name } # comment with no newline"));
}

// An underscore is a legal GraphQL name character, so `_priv` is a field (unresolved here, hence
// null) rather than a parse error.
void test_underscore_field_name()
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"_priv\":null}}", run("{ _priv }"));
}

// `query` with no operation name is the same document as the anonymous form. A bare `query` with
// nothing after it, and a document opening on neither '{' nor a name character, are parse errors.
void test_query_keyword_without_operation_name()
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"name\":\"esp32\"}}", run("query { name }"));
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("query"));
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("@ { name }"));
}

// A backslash as the last byte of the input has nothing to escape: it is taken literally, and the
// string then reaches end-of-input unterminated, which is a parse error rather than a read past
// the end of the buffer.
void test_trailing_backslash_in_string_arg()
{
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("{ greet(name: \"ab\\"));
}

// Every stage of the number scanner can end on end-of-input - the integer digits, the fraction
// digits, the exponent marker itself, and the exponent digits. Each truncation is a parse error
// (the argument list never closes) rather than a read past the end of the query.
void test_number_truncated_at_end_of_input()
{
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("{ f(a:12"));
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("{ f(a:1.5"));
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("{ f(a:1e"));
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("{ f(a:1e5"));
}

// Uppercase `E`, an explicit `+` exponent sign and a negative fraction are all accepted number
// forms; a digit run also stops at the first byte *above* '9', not only below '0'.
void test_number_exponent_and_sign_forms()
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"sensor\":{\"value\":70}}}",
                             run("{ sensor(id: 7, a: 1E2, b: 1e+2, c: -1.5) { value } }"));
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("{ f(a:1e2}"));
}

// The exponent accumulator is clamped, so a literal with an absurd exponent cannot run it away;
// the document still parses and the sibling integer argument is unaffected.
void test_number_huge_exponent_is_clamped()
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"sensor\":{\"value\":70}}}", run("{ sensor(id: 7, big: 1e4000) { value } }"));
}

// A value token that is neither a string, a number nor one of the three keywords is a parse error:
// both a byte below '0' and a bareword enum, which this subset does not support.
void test_unsupported_value_tokens_fail()
{
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("{ f(a: !) }"));
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("{ f(a: ENUM) }"));
}

// A resolver that reports a string value with a null pointer serialises as an empty JSON string
// rather than dereferencing it.
void test_resolver_null_string_pointer()
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"nullstr\":\"\"}}", run("{ nullstr }"));
}

// With no resolver installed at all every leaf is null; the document still parses and executes.
void test_no_resolver_yields_all_null()
{
    TEST_ASSERT_EQUAL_INT(PC_GQL_OK, pc_graphql_execute("{ name uptime }", 15, NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"name\":null,\"uptime\":null}}", out);
}

// An in-scope argument whose *name* does not match is walked past by every accessor: the int,
// string and bool lookups each report "absent" rather than matching on type alone.
void test_arg_name_mismatch_is_skipped()
{
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"sensor\":{\"value\":-1}}}", run("{ sensor(other: 7) { value } }"));
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"greet\":\"hi ?\"}}", run("{ greet(other: \"x\") }"));
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"flag\":false}}", run("{ flag(other: true) }"));
}

// A zero-capacity output buffer fails closed before a single byte is written.
void test_zero_capacity_output_fails()
{
    char buf[4] = {'x', 'x', 'x', 'x'};
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, pc_graphql_execute("{ name }", 8, resolver, buf, 0));
    TEST_ASSERT_EQUAL_CHAR('x', buf[0]); // untouched
}

// The error document obeys the same capacity discipline as the data document: a buffer too small
// to hold it emits nothing at all, and one that holds it exactly leaves no room for a terminator.
void test_error_document_capacity_edges()
{
    static const char kErr[] = "{\"errors\":[{\"message\":\"syntax error\"}]}"; // 39 bytes
    char buf[64];

    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, pc_graphql_execute("{", 1, resolver, buf, 8));
    TEST_ASSERT_EQUAL_CHAR('\0', buf[0]); // overflowed on the first write -> nothing emitted

    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, pc_graphql_execute("{", 1, resolver, buf, sizeof(kErr) - 1));
    TEST_ASSERT_EQUAL_STRING(kErr, buf); // exactly fills the buffer; the NUL is not written
}

// A data document that fills the buffer exactly is still an overflow: there is no room for the NUL
// terminator, so the caller is told rather than handed an unterminated string.
void test_data_document_exact_fit_is_overflow()
{
    char buf[32];
    memset(buf, 0, sizeof(buf));
    // `{"data":{"a":null}}` is exactly 19 bytes, so 19 fails and 20 succeeds.
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_OVERFLOW, pc_graphql_execute("{a}", 3, resolver, buf, 19));
    TEST_ASSERT_EQUAL_INT(PC_GQL_OK, pc_graphql_execute("{a}", 3, resolver, buf, 20));
    TEST_ASSERT_EQUAL_STRING("{\"data\":{\"a\":null}}", buf);
}

// A bareword (enum-style) argument value longer than parse_value's small keyword scratch. This is a
// REGRESSION TEST for a stack buffer overflow: parse_name() was bounded only by PC_GQL_NAME_MAX
// (32) while parse_value() handed it a char[8] that only ever needs "true"/"false"/"null", so a
// longer bareword - straight from untrusted query text - wrote up to 24 bytes past the end of a
// stack array. parse_name() now takes the destination capacity and rejects instead.
static void test_long_bareword_argument_does_not_overflow_keyword_scratch()
{
    // Rejected as a parse error - a bareword that is not true/false/null is not a value in this
    // subset, whatever its length - but the point is that it is rejected WITHOUT writing past kw[].
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("{ f(a: LONGENUMVALUE) }"));
    // Well past PC_GQL_NAME_MAX (32) as well, not just past the scratch.
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("{ f(a: AAAAAAAAAABBBBBBBBBBCCCCCCCCCCDDDDDDDDDD) }"));
    // Exactly at the old 8-byte scratch boundary.
    TEST_ASSERT_EQUAL_INT(PC_GQL_ERR_PARSE, run_rc("{ f(a: ENUMVALU) }"));
    // The keywords the scratch actually exists for still parse.
    TEST_ASSERT_EQUAL_INT(PC_GQL_OK, run_rc("{ f(a: true) }"));
    TEST_ASSERT_EQUAL_INT(PC_GQL_OK, run_rc("{ f(a: false) }"));
    TEST_ASSERT_EQUAL_INT(PC_GQL_OK, run_rc("{ f(a: null) }"));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_long_bareword_argument_does_not_overflow_keyword_scratch);
    RUN_TEST(test_lexer_whitespace_and_eof_comment);
    RUN_TEST(test_underscore_field_name);
    RUN_TEST(test_query_keyword_without_operation_name);
    RUN_TEST(test_trailing_backslash_in_string_arg);
    RUN_TEST(test_number_truncated_at_end_of_input);
    RUN_TEST(test_number_exponent_and_sign_forms);
    RUN_TEST(test_number_huge_exponent_is_clamped);
    RUN_TEST(test_unsupported_value_tokens_fail);
    RUN_TEST(test_resolver_null_string_pointer);
    RUN_TEST(test_no_resolver_yields_all_null);
    RUN_TEST(test_arg_name_mismatch_is_skipped);
    RUN_TEST(test_zero_capacity_output_fails);
    RUN_TEST(test_error_document_capacity_edges);
    RUN_TEST(test_data_document_exact_fit_is_overflow);
    RUN_TEST(test_malformed_tokens_fail);
    RUN_TEST(test_query_keyword_forms_fail);
    RUN_TEST(test_pool_limits);
    RUN_TEST(test_string_pool_exhaustion);
    RUN_TEST(test_resolver_null_typed_value);
    RUN_TEST(test_resolver_path_overflow);
    RUN_TEST(test_arg_accessors_edges);
    RUN_TEST(test_flat_selection);
    RUN_TEST(test_string_escapes_decoded);
    RUN_TEST(test_number_arg_variants_parse);
    RUN_TEST(test_bool_args);
    RUN_TEST(test_null_arg_value);
    RUN_TEST(test_control_char_is_unicode_escaped);
    RUN_TEST(test_unterminated_string_arg_fails);
    RUN_TEST(test_arg_missing_colon_fails);
    RUN_TEST(test_bad_arg_value_fails);
    RUN_TEST(test_trailing_junk_fails);
    RUN_TEST(test_long_field_name_hits_limit);
    RUN_TEST(test_null_inputs_fail_closed);
    RUN_TEST(test_unknown_operation_keyword_fails);
    RUN_TEST(test_selection_is_honored);
    RUN_TEST(test_nested_object);
    RUN_TEST(test_args_collected_along_path);
    RUN_TEST(test_scalar_types);
    RUN_TEST(test_string_arg_and_escaping);
    RUN_TEST(test_unresolved_field_is_null);
    RUN_TEST(test_query_keyword_and_name);
    RUN_TEST(test_comments_and_commas);
    RUN_TEST(test_parse_error_reports_errors);
    RUN_TEST(test_mutation_rejected);
    RUN_TEST(test_depth_limit);
    RUN_TEST(test_overflow_fails_closed);
    return UNITY_END();
}
